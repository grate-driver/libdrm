/*
 * Copyright (C) 2016 Rob Clark <robclark@freedesktop.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <assert.h>
#include <pthread.h>
#include <string.h>

#include "private.h"

drm_private extern pthread_mutex_t table_lock;

static void
add_bucket(struct drm_tegra_bo_cache *cache, int size)
{
	unsigned int i = cache->num_buckets;

	assert(i < ARRAY_SIZE(cache->cache_bucket));

	DRMINITLISTHEAD(&cache->cache_bucket[i].list);
	cache->cache_bucket[i].size = size;
	cache->num_buckets++;
}

/**
 * @coarse: if true, only power-of-two bucket sizes, otherwise
 *    fill in for a bit smoother size curve..
 */
drm_private void
drm_tegra_bo_cache_init(struct drm_tegra_bo_cache *cache, bool course)
{
	unsigned long size, cache_max_size = 64 * 1024 * 1024;

	/* OK, so power of two buckets was too wasteful of memory.
	 * Give 3 other sizes between each power of two, to hopefully
	 * cover things accurately enough.  (The alternative is
	 * probably to just go for exact matching of sizes, and assume
	 * that for things like composited window resize the tiled
	 * width/height alignment and rounding of sizes to pages will
	 * get us useful cache hit rates anyway)
	 */
	add_bucket(cache, 4096);
	add_bucket(cache, 4096 * 2);
	if (!course)
		add_bucket(cache, 4096 * 3);

	/* Initialize the linked lists for BO reuse cache. */
	for (size = 4 * 4096; size <= cache_max_size; size *= 2) {
		add_bucket(cache, size);
		if (!course) {
			add_bucket(cache, size + size * 1 / 4);
			add_bucket(cache, size + size * 2 / 4);
			add_bucket(cache, size + size * 3 / 4);
		}
	}
}

/* Frees older cached buffers.  Called under table_lock */
drm_private void
drm_tegra_bo_cache_cleanup(struct drm_tegra_bo_cache *cache, time_t time)
{
	int i;

	if (cache->time == time)
		return;

	for (i = 0; i < cache->num_buckets; i++) {
		struct drm_tegra_bo_bucket *bucket = &cache->cache_bucket[i];
		struct drm_tegra_bo *bo;

		while (!DRMLISTEMPTY(&bucket->list)) {
			bo = DRMLISTENTRY(struct drm_tegra_bo,
					  bucket->list.next, bo_list);

			/* keep things in cache for at least 1 second: */
			if (time && ((time - bo->free_time) <= 1))
				break;

			VG_BO_OBTAIN(bo);
			DRMLISTDEL(&bo->bo_list);
			drm_tegra_bo_free(bo);
		}
	}

	cache->time = time;
}

static struct drm_tegra_bo_bucket * get_bucket(struct drm_tegra_bo_cache *cache,
					       uint32_t size)
{
	int i;

	/* hmm, this is what intel does, but I suppose we could calculate our
	 * way to the correct bucket size rather than looping..
	 */
	for (i = 0; i < cache->num_buckets; i++) {
		struct drm_tegra_bo_bucket *bucket = &cache->cache_bucket[i];
		if (bucket->size >= size) {
			return bucket;
		}
	}

	return NULL;
}

static int is_idle(struct drm_tegra_bo *bo)
{
	/* TODO implement drm_tegra_bo_cpu_prep() */
	return 1;
}

static struct drm_tegra_bo *find_in_bucket(struct drm_tegra_bo_bucket *bucket,
					   uint32_t flags)
{
	struct drm_tegra_bo *bo = NULL;

	/* TODO .. if we had an ALLOC_FOR_RENDER flag like intel, we could
	 * skip the busy check.. if it is only going to be a render target
	 * then we probably don't need to stall..
	 *
	 * NOTE that intel takes ALLOC_FOR_RENDER bo's from the list tail
	 * (MRU, since likely to be in GPU cache), rather than head (LRU)..
	 */
	pthread_mutex_lock(&table_lock);
	if (!DRMLISTEMPTY(&bucket->list)) {
		bo = DRMLISTENTRY(struct drm_tegra_bo, bucket->list.next,
				  bo_list);
		/* TODO check for compatible flags? */
		if (is_idle(bo)) {
			DRMLISTDELINIT(&bo->bo_list);
		} else {
			bo = NULL;
		}
	}
	pthread_mutex_unlock(&table_lock);

	return bo;
}

static void reset_bo(struct drm_tegra_bo *bo, uint32_t flags)
{
	struct drm_tegra_bo_tiling tiling;
	bool mapped;

	VG_BO_OBTAIN(bo);

	/* XXX: Error handling? */
	drm_tegra_bo_set_flags(bo, flags);

	/* reset tiling mode */
	memset(&tiling, 0, sizeof(tiling));

	/* XXX: Error handling? */
	drm_tegra_bo_set_tiling(bo, &tiling);

	mapped = (RUNNING_ON_VALGRIND && bo->mmap_ref > 1);

	/* reset reference counters */
	atomic_set(&bo->ref, 1);
	bo->mmap_ref = RUNNING_ON_VALGRIND ? 1 : 0;

	/* mark BO as unmapped */
	if (mapped)
		VG_BO_UNMMAP(bo);
}

/* NOTE: size is potentially rounded up to bucket size: */
drm_private struct drm_tegra_bo *
drm_tegra_bo_cache_alloc(struct drm_tegra_bo_cache *cache,
			 uint32_t *size, uint32_t flags)
{
	struct drm_tegra_bo *bo = NULL;
	struct drm_tegra_bo_bucket *bucket;

	*size = align(*size, 4096);
	bucket = get_bucket(cache, *size);

	/* see if we can be green and recycle: */
	if (bucket) {
		*size = bucket->size;
		bo = find_in_bucket(bucket, flags);
		if (bo) {
			reset_bo(bo, flags);
			return bo;
		}
	}

	return NULL;
}

drm_private int
drm_tegra_bo_cache_free(struct drm_tegra_bo_cache *cache,
			struct drm_tegra_bo *bo)
{
	struct drm_tegra_bo_bucket *bucket = get_bucket(cache, bo->size);

	/* see if we can be green and recycle: */
	if (bucket) {
		struct timespec time;

		clock_gettime(CLOCK_MONOTONIC, &time);

		bo->free_time = time.tv_sec;
		VG_BO_RELEASE(bo);
		DRMLISTADDTAIL(&bo->bo_list, &bucket->list);
		drm_tegra_bo_cache_cleanup(cache, time.tv_sec);

		return 0;
	}

	return -1;
}

static void
drm_tegra_bo_mmap_cache_cleanup(struct drm_tegra_bo_mmap_cache *cache,
				time_t time)
{
	struct drm_tegra_bo *bo;

	if (cache->time == time)
		return;

	while (!DRMLISTEMPTY(&cache->list)) {
		bo = DRMLISTENTRY(struct drm_tegra_bo, cache->list.next,
				  mmap_list);

		/* keep things in cache for at least 3 seconds: */
		if (time && ((time - bo->unmap_time) <= 3))
			break;

		munmap(bo->map_cached, bo->size);
		DRMLISTDEL(&bo->mmap_list);
		bo->map_cached = NULL;
	}

	cache->time = time;
}

drm_private void
drm_tegra_bo_cache_unmap(struct drm_tegra_bo *bo)
{
	struct drm_tegra_bo_mmap_cache *cache = &bo->drm->mmap_cache;
	struct timespec time;

	clock_gettime(CLOCK_MONOTONIC, &time);

	bo->unmap_time = time.tv_sec;
	bo->map_cached = bo->map;

	drm_tegra_bo_mmap_cache_cleanup(cache, time.tv_sec);
	DRMLISTADDTAIL(&bo->mmap_list, &cache->list);
}

drm_private void *
drm_tegra_bo_cache_map(struct drm_tegra_bo *bo)
{
	void *map_cached = bo->map_cached;

	if (map_cached) {
		DRMLISTDEL(&bo->mmap_list);
		bo->map_cached = NULL;
	}

	return map_cached;
}
