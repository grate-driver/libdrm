/*
 * Copyright © 2012, 2013 Thierry Reding
 * Copyright © 2013 Erik Faye-Lund
 * Copyright © 2014 NVIDIA Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>

#include <xf86drm.h>

#include "private.h"

drm_private pthread_mutex_t table_lock = PTHREAD_MUTEX_INITIALIZER;

/* lookup a buffer, call with table_lock mutex locked */
static struct drm_tegra_bo * lookup_bo(void *table, uint32_t key)
{
	struct drm_tegra_bo *bo;

	if (drmHashLookup(table, key, (void **)&bo))
		return NULL;

	/* found, increment reference count */
	atomic_inc(&bo->ref);

	/* don't break the bucket if this BO was found in one */
	DRMLISTDELINIT(&bo->bo_list);

	return bo;
}

static void drm_tegra_bo_setup_guards(struct drm_tegra_bo *bo)
{
#ifndef NDEBUG
	struct drm_tegra *drm = bo->drm;
	struct drm_tegra_gem_mmap args;
	uint64_t guard = 0x5351317315731757;
	uint8_t *map;
	size_t size;
	unsigned i;
	int err;

	if (!drm->debug_bo_front_guard && !drm->debug_bo_back_guard)
		return;

	size = bo->size;

	if (drm->debug_bo_back_guard)
		size += 4096;

	memset(&args, 0, sizeof(args));
	args.handle = bo->handle;

	err = drmCommandWriteRead(drm->fd, DRM_TEGRA_GEM_MMAP,
				  &args, sizeof(args));
	if (err < 0) {
		VDBG_BO(bo, "failed get mapping offset err %d (%s)\n",
			err, strerror(-err));
		abort();
	}

	map = mmap(0, bo->offset + size, PROT_READ | PROT_WRITE, MAP_SHARED,
		   drm->fd, args.offset);
	if (map == MAP_FAILED) {
		VDBG_BO(bo, "failed to map guard 0x%llX err %d (%s)\n",
			args.offset, -errno, strerror(errno));
		abort();
	}

	if (drm->debug_bo_front_guard)
		bo->guard_front = (uint64_t *)map;

	if (drm->debug_bo_back_guard)
		bo->guard_back = (uint64_t *)(map + bo->offset + bo->size);

	VDBG_BO(bo, "front %p back %p\n", bo->guard_front, bo->guard_back);

	/* we only interested in guards mapping, hence unmap the actual BO */
	if (bo->size >= 4096)
		munmap(map + bo->offset, align(bo->size - 4095, 4096));

	if (bo->handle & 1)
		guard = ~guard;

	if (drm->debug_bo_front_guard)
		for (i = 0; i < 512; i++)
			bo->guard_front[i] = guard;

	if (drm->debug_bo_back_guard)
		for (i = 0; i < 512; i++)
			bo->guard_back[i] = guard;
#endif
}

static void drm_tegra_bo_check_guards(struct drm_tegra_bo *bo)
{
#ifndef NDEBUG
	struct drm_tegra *drm = bo->drm;
	uint64_t guard_check = 0x5351317315731757;
	uint64_t guard;
	unsigned i;

	if (!drm->debug_bo_front_guard && !drm->debug_bo_back_guard)
		return;

	VDBG_BO(bo, "front %p back %p\n", bo->guard_front, bo->guard_back);

	if (bo->handle & 1)
		guard_check = ~guard_check;

	if (drm->debug_bo_front_guard) {
		for (i = 0; i < 512; i++) {
			guard = bo->guard_front[i];

			if (guard != guard_check) {
				VDBG_BO(bo, "front guard is corrupted: entry %u is 0x%16" PRIX64 ", should be 0x%16" PRIX64 "\n",
					i, guard, guard_check);
				abort();
			}
		}
	}

	if (drm->debug_bo_back_guard) {
		for (i = 0; i < 512; i++) {
			guard = bo->guard_back[i];

			if (guard != guard_check) {
				VDBG_BO(bo, "back guard is corrupted: entry %u is 0x%16" PRIX64 ", should be 0x%16" PRIX64 "\n",
					i, guard, guard_check);
				abort();
			}
		}
	}
#endif
}

static void drm_tegra_bo_unmap_guards(struct drm_tegra_bo *bo)
{
#ifndef NDEBUG
	unsigned long unaligned = (unsigned long)bo->guard_back;
	unsigned long aligned = align(unaligned - 4095, 4096);
	void *guard_back = (void *)aligned;
	unsigned guard_back_size = 4096;
	struct drm_tegra *drm = bo->drm;

	if (!drm->debug_bo_front_guard && !drm->debug_bo_back_guard)
		return;

	VDBG_BO(bo, "front %p back %p (%u)\n",
		bo->guard_front, guard_back, guard_back_size);

	if (bo->size & 4095)
		guard_back_size *= 2;

	if (drm->debug_bo_front_guard)
		munmap(bo->guard_front, 4096);

	if (drm->debug_bo_back_guard)
		munmap(guard_back, guard_back_size);

	bo->guard_front = NULL;
	bo->guard_back = NULL;
#endif
}

drm_private int drm_tegra_bo_free(struct drm_tegra_bo *bo)
{
	struct drm_tegra *drm = bo->drm;
	struct drm_gem_close args;
	int err;

	DBG_BO(bo, "\n");

#ifndef NDEBUG
	if (drm->debug_bo) {
		drm->debug_bos_allocated--;
		drm->debug_bos_total_size -= bo->debug_size;
	}
#endif
	drm_tegra_bo_unmap_guards(bo);

	if (bo->map) {
		if (RUNNING_ON_VALGRIND)
			VG_BO_UNMMAP(bo);
		else
			munmap(bo->map, bo->offset + bo->size);

	} else if (bo->map_cached) {
		if (!RUNNING_ON_VALGRIND)
			munmap(bo->map_cached, bo->offset + bo->size);

		DRMLISTDEL(&bo->mmap_list);
#ifndef NDEBUG
		if (drm->debug_bo) {
			drm->debug_bos_mappings_cached--;
			drm->debug_bos_cached_pages -= bo->debug_size / 4096;
		}
#endif
	} else {
		goto vg_free;
	}

#ifndef NDEBUG
	if (drm->debug_bo) {
		drm->debug_bos_mapped--;
		drm->debug_bos_total_pages -= bo->debug_size / 4096;
	}
#endif
vg_free:
	VG_BO_FREE(bo);

	if (bo->name)
		drmHashDelete(drm->name_table, bo->name);

	drmHashDelete(drm->handle_table, bo->handle);

	memset(&args, 0, sizeof(args));
	args.handle = bo->handle;

	err = drmIoctl(drm->fd, DRM_IOCTL_GEM_CLOSE, &args);
	if (err < 0)
		err = -errno;

	free(bo);

	DBG_BO_STATS(drm);

	return err;
}

static void drm_tegra_setup_debug(struct drm_tegra *drm)
{
#ifndef NDEBUG
	char *str;

	str = getenv("LIBDRM_TEGRA_DEBUG_BO");
	drm->debug_bo = (str && strcmp(str, "1") == 0);

	str = getenv("LIBDRM_TEGRA_DEBUG_BO_BACK_GUARD");
	drm->debug_bo_back_guard = (str && strcmp(str, "1") == 0);

	str = getenv("LIBDRM_TEGRA_DEBUG_BO_FRONT_GUARD");
	drm->debug_bo_front_guard = (str && strcmp(str, "1") == 0);
#endif
}

static int drm_tegra_wrap(struct drm_tegra **drmp, int fd, bool close)
{
	struct drm_tegra *drm;

	if (fd < 0 || !drmp)
		return -EINVAL;

	drm = calloc(1, sizeof(*drm));
	if (!drm)
		return -ENOMEM;

	drm->close = close;
	drm->fd = fd;

	drm_tegra_bo_cache_init(&drm->bo_cache, false);
	drm->handle_table = drmHashCreate();
	drm->name_table = drmHashCreate();
	DRMINITLISTHEAD(&drm->mmap_cache.list);

	if (!drm->handle_table || !drm->name_table)
		return -ENOMEM;

	drm_tegra_setup_debug(drm);

	*drmp = drm;

	return 0;
}

drm_public int drm_tegra_new(struct drm_tegra **drmp, int fd)
{
	bool supported = false;
	drmVersionPtr version;

	version = drmGetVersion(fd);
	if (!version)
		return -ENOMEM;

	if (!strncmp(version->name, "tegra", version->name_len))
		supported = true;

	drmFreeVersion(version);

	if (!supported)
		return -ENOTSUP;

	return drm_tegra_wrap(drmp, fd, false);
}

drm_public void drm_tegra_close(struct drm_tegra *drm)
{
	if (!drm)
		return;

	drm_tegra_bo_cache_cleanup(drm, 0);
	drmHashDestroy(drm->handle_table);
	drmHashDestroy(drm->name_table);

	if (drm->close)
		close(drm->fd);

	free(drm);
}

drm_public int drm_tegra_bo_new(struct drm_tegra_bo **bop, struct drm_tegra *drm,
		     uint32_t flags, uint32_t size)
{
	struct drm_tegra_gem_create args;
	struct drm_tegra_bo *bo;
	int err;

	if (!drm || size == 0 || !bop)
		return -EINVAL;

	bo = drm_tegra_bo_cache_alloc(drm, &size, flags);
	if (bo) {
		DBG_BO(bo, "success from cache\n");
		goto out;
	}

	bo = calloc(1, sizeof(*bo));
	if (!bo)
		return -ENOMEM;

	DRMINITLISTHEAD(&bo->push_list);
	DRMINITLISTHEAD(&bo->bo_list);
	atomic_set(&bo->ref, 1);
	bo->reuse = true;
	bo->flags = flags;
	bo->size = size;
	bo->drm = drm;

	memset(&args, 0, sizeof(args));
	args.flags = flags;
	args.size = size;

#ifndef NDEBUG
	if (drm->debug_bo_front_guard) {
		bo->offset += 4096;
		args.size += 4096;
	}

	if (drm->debug_bo_back_guard)
		args.size += 4096;
#endif
	err = drmCommandWriteRead(drm->fd, DRM_TEGRA_GEM_CREATE, &args,
				  sizeof(args));
	if (err < 0) {
		VDBG_DRM(drm, "failed size %u bytes flags 0x%08X err %d (%s)\n",
			 size, flags, err, strerror(-err));
		free(bo);
		return err;
	}

	bo->handle = args.handle;

	DBG_BO(bo, "success new\n");
	VG_BO_ALLOC(bo);

#ifndef NDEBUG
	if (drm->debug_bo) {
		bo->debug_size = align(bo->size, 4096);
		drm->debug_bos_total_size += bo->debug_size;
		drm->debug_bos_allocated++;
	}
#endif
	drm_tegra_bo_setup_guards(bo);
	DBG_BO_STATS(drm);

	pthread_mutex_lock(&table_lock);
	/* add ourselves into the handle table */
	drmHashInsert(drm->handle_table, args.handle, bo);
	pthread_mutex_unlock(&table_lock);
out:
	*bop = bo;

	return 0;
}

drm_public int drm_tegra_bo_wrap(struct drm_tegra_bo **bop, struct drm_tegra *drm,
		      uint32_t handle, uint32_t flags, uint32_t size)
{
	struct drm_tegra_bo *bo;
	int err = 0;

	if (!drm || !bop)
		return -EINVAL;

	pthread_mutex_lock(&table_lock);

	/* check handle table to see if BO is already open */
	bo = lookup_bo(drm->handle_table, handle);
	if (bo)
		goto unlock;

	bo = calloc(1, sizeof(*bo));
	if (!bo) {
		err = -ENOMEM;
		goto unlock;
	}

	DRMINITLISTHEAD(&bo->push_list);
	DRMINITLISTHEAD(&bo->bo_list);
	atomic_set(&bo->ref, 1);
	bo->handle = handle;
	bo->flags = flags;
	bo->size = size;
	bo->drm = drm;

	VG_BO_ALLOC(bo);

	/* add ourselves into the handle table */
	drmHashInsert(drm->handle_table, handle, bo);

	DBG_BO(bo, "success\n");
unlock:
	pthread_mutex_unlock(&table_lock);

	*bop = bo;

	return err;
}

drm_public struct drm_tegra_bo *drm_tegra_bo_ref(struct drm_tegra_bo *bo)
{
	if (bo) {
		DBG_BO(bo, "\n");

		atomic_inc(&bo->ref);
	}

	return bo;
}

drm_public int drm_tegra_bo_unref(struct drm_tegra_bo *bo)
{
	int err = 0;

	if (!bo)
		return -EINVAL;

	DBG_BO(bo, "\n");

	if (!atomic_dec_and_test(&bo->ref))
		return 0;

	drm_tegra_bo_check_guards(bo);

	pthread_mutex_lock(&table_lock);

	if (!bo->reuse || drm_tegra_bo_cache_free(bo))
		err = drm_tegra_bo_free(bo);

	pthread_mutex_unlock(&table_lock);

	return err;
}

drm_public int drm_tegra_bo_get_handle(struct drm_tegra_bo *bo, uint32_t *handle)
{
	if (!bo || !handle)
		return -EINVAL;

	*handle = bo->handle;

	return 0;
}

drm_private int __drm_tegra_bo_map(struct drm_tegra_bo *bo, void **ptr)
{
	struct drm_tegra *drm = bo->drm;
	struct drm_tegra_gem_mmap args;
	uint8_t *map;
	int err;

	map = drm_tegra_bo_cache_map(bo);
	if (map) {
		DBG_BO(bo, "success from cache\n");
		goto out;
	}

#if HAVE_VALGRIND
	if (RUNNING_ON_VALGRIND && bo->map_vg) {
		map = bo->map_vg;
		goto map_cnt;
	}
#endif

	memset(&args, 0, sizeof(args));
	args.handle = bo->handle;

	err = drmCommandWriteRead(drm->fd, DRM_TEGRA_GEM_MMAP,
				  &args, sizeof(args));
	if (err < 0) {
		VDBG_BO(bo, "failed get mapping offset err %d (%s)\n",
			err, strerror(-err));
		return err;
	}

	map = mmap(0, bo->offset + bo->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		   drm->fd, args.offset);
	if (map == MAP_FAILED) {
		VDBG_BO(bo, "failed to map offset 0x%llX err %d (%s)\n",
			args.offset, -errno, strerror(errno));
		*ptr = NULL;
		return -errno;
	}

	map += bo->offset;
#if HAVE_VALGRIND
map_cnt:
#endif
#ifndef NDEBUG
	if (drm->debug_bo && ptr == &bo->map) {
		drm->debug_bos_mapped++;
		drm->debug_bos_total_pages += bo->debug_size / 4096;
	}
#endif
	DBG_BO(bo, "success\n");
out:
	if (ptr == &bo->map)
		DBG_BO_STATS(drm);

	*ptr = map;

	return 0;
}

drm_public int drm_tegra_bo_map(struct drm_tegra_bo *bo, void **ptr)
{
	int err = 0;

	if (!bo)
		return -EINVAL;

	pthread_mutex_lock(&table_lock);

	if (!bo->map) {
		err = __drm_tegra_bo_map(bo, &bo->map);
		if (err)
			goto out;

		VG_BO_MMAP(bo);

		bo->mmap_ref = 1;
	} else {
		DBG_BO(bo, "\n");
		bo->mmap_ref++;
	}
out:
	if (ptr)
		*ptr = bo->map;

	pthread_mutex_unlock(&table_lock);

	return err;
}

drm_public int drm_tegra_bo_unmap(struct drm_tegra_bo *bo)
{
	if (!bo)
		return -EINVAL;

	DBG_BO(bo, "\n");

	pthread_mutex_lock(&table_lock);

	if (bo->mmap_ref == 0)
		goto unlock;

	if (--bo->mmap_ref > 0)
		goto unlock;

	VG_BO_UNMMAP(bo);

	drm_tegra_bo_cache_unmap(bo);
	bo->map = NULL;
unlock:
	pthread_mutex_unlock(&table_lock);

	return 0;
}

drm_public int drm_tegra_bo_get_flags(struct drm_tegra_bo *bo, uint32_t *flags)
{
	struct drm_tegra_gem_get_flags args;
	int err;

	if (!bo)
		return -EINVAL;

	memset(&args, 0, sizeof(args));
	args.handle = bo->handle;

	err = drmCommandWriteRead(bo->drm->fd, DRM_TEGRA_GEM_GET_FLAGS, &args,
				  sizeof(args));
	if (err < 0) {
		VDBG_BO(bo, "failed err %d strerror(%s)\n",
			err, strerror(-err));
		return err;
	}

	if (flags)
		*flags = args.flags;

	VDBG_BO(bo, "success flags 0x%08X\n", args.flags);

	return 0;
}

drm_public int drm_tegra_bo_set_flags(struct drm_tegra_bo *bo, uint32_t flags)
{
	struct drm_tegra_gem_get_flags args;
	int err;

	if (!bo)
		return -EINVAL;

	memset(&args, 0, sizeof(args));
	args.handle = bo->handle;
	args.flags = flags;

	err = drmCommandWriteRead(bo->drm->fd, DRM_TEGRA_GEM_SET_FLAGS, &args,
				  sizeof(args));
	if (err < 0) {
		VDBG_BO(bo, "failed err %d strerror(%s)\n",
			err, strerror(-err));
		return err;
	}

	DBG_BO(bo, "success\n");

	return 0;
}

drm_public int drm_tegra_bo_get_tiling(struct drm_tegra_bo *bo,
			    struct drm_tegra_bo_tiling *tiling)
{
	struct drm_tegra_gem_get_tiling args;
	int err;

	if (!bo)
		return -EINVAL;

	memset(&args, 0, sizeof(args));
	args.handle = bo->handle;

	err = drmCommandWriteRead(bo->drm->fd, DRM_TEGRA_GEM_GET_TILING, &args,
				  sizeof(args));
	if (err < 0) {
		VDBG_BO(bo, "failed err %d strerror(%s)\n",
			err, strerror(-err));
		return err;
	}

	if (tiling) {
		tiling->mode = args.mode;
		tiling->value = args.value;
	}

	VDBG_BO(bo, "success mode %u value %u\n", args.mode, args.value);

	return 0;
}

drm_public int drm_tegra_bo_set_tiling(struct drm_tegra_bo *bo,
			    const struct drm_tegra_bo_tiling *tiling)
{
	struct drm_tegra_gem_set_tiling args;
	int err;

	if (!bo || !tiling)
		return -EINVAL;

	memset(&args, 0, sizeof(args));
	args.handle = bo->handle;
	args.mode = tiling->mode;
	args.value = tiling->value;

	err = drmCommandWriteRead(bo->drm->fd, DRM_TEGRA_GEM_SET_TILING,
				  &args, sizeof(args));
	if (err < 0) {
		VDBG_BO(bo, "failed mode %u value %u err %d strerror(%s)\n",
			tiling->mode, tiling->value, err, strerror(-err));
		return err;
	}

	VDBG_BO(bo, "success mode %u value %u\n", tiling->mode, tiling->value);

	bo->custom_tiling = (tiling->mode || tiling->value);

	return 0;
}

drm_public
int drm_tegra_bo_get_name(struct drm_tegra_bo *bo, uint32_t *name)
{
	if (!bo || !name)
		return -EINVAL;

	if (!bo->name) {
		struct drm_gem_flink args;
		int err;

		memset(&args, 0, sizeof(args));
		args.handle = bo->handle;

		err = drmIoctl(bo->drm->fd, DRM_IOCTL_GEM_FLINK, &args);
		if (err < 0) {
			VDBG_BO(bo, "err %d strerror(%s)\n",
				err, strerror(-err));
			return -errno;
		}

		pthread_mutex_lock(&table_lock);

		drmHashInsert(bo->drm->name_table, args.name, bo);
		bo->name = args.name;

		pthread_mutex_unlock(&table_lock);
	}

	*name = bo->name;

	DBG_BO(bo, "\n");

	return 0;
}

drm_public
int drm_tegra_bo_from_name(struct drm_tegra_bo **bop, struct drm_tegra *drm,
			   uint32_t name, uint32_t flags)
{
	struct drm_gem_open args;
	struct drm_tegra_bo *dup;
	struct drm_tegra_bo *bo;
	int err = 0;

	if (!drm || !name || !bop)
		return -EINVAL;

	pthread_mutex_lock(&table_lock);

	/* check name table first, to see if BO is already open */
	bo = lookup_bo(drm->name_table, name);
	if (bo)
		goto unlock;

	bo = calloc(1, sizeof(*bo));
	if (!bo) {
		err = -ENOMEM;
		goto unlock;
	}

	memset(&args, 0, sizeof(args));
	args.name = name;

	err = drmIoctl(drm->fd, DRM_IOCTL_GEM_OPEN, &args);
	if (err < 0) {
		VDBG_DRM(drm, "failed name 0x%08X err %d strerror(%s)\n",
			 name, err, strerror(-err));
		err = -errno;
		free(bo);
		bo = NULL;
		goto unlock;
	}

	/* check handle table second, to see if BO is already open */
	dup = lookup_bo(drm->handle_table, args.handle);
	if (dup) {
		VDBG_BO(dup, "success reused name 0x%08X\n", name);
		free(bo);
		bo = dup;
		goto unlock;
	}

	drmHashInsert(drm->name_table, name, bo);
	atomic_set(&bo->ref, 1);
	bo->name = name;
	bo->handle = args.handle;
	bo->flags = flags;
	bo->size = args.size;
	bo->drm = drm;

	DBG_BO(bo, "success\n");

	VG_BO_ALLOC(bo);

unlock:
	pthread_mutex_unlock(&table_lock);

	*bop = bo;

	return err;
}

drm_public
int drm_tegra_bo_to_dmabuf(struct drm_tegra_bo *bo, uint32_t *handle)
{
	int prime_fd;
	int err;

	if (!bo || !handle)
		return -EINVAL;

	err = drmPrimeHandleToFD(bo->drm->fd, bo->handle, DRM_CLOEXEC,
				 &prime_fd);
	if (err) {
		VDBG_BO(bo, "faile err %d strerror(%s)\n",
			err, strerror(-err));
		return err;
	}

	*handle = prime_fd;

	VDBG_BO(bo, "success prime_fd %u\n",  prime_fd);

	return 0;
}

drm_public
int drm_tegra_bo_from_dmabuf(struct drm_tegra_bo **bop, struct drm_tegra *drm,
			     int fd, uint32_t flags)
{
	struct drm_tegra_bo *dup;
	struct drm_tegra_bo *bo;
	uint32_t handle;
	uint32_t size;
	int err;

	if (!drm || !bop)
		return -EINVAL;

	pthread_mutex_lock(&table_lock);

	bo = calloc(1, sizeof(*bo));
	if (!bo) {
		err = -ENOMEM;
		goto unlock;
	}

	err = drmPrimeFDToHandle(drm->fd, fd, &handle);
	if (err) {
		free(bo);
		bo = NULL;
		goto unlock;
	}

	/* check handle table to see if BO is already open */
	dup = lookup_bo(drm->handle_table, handle);
	if (dup) {
		DBG_BO(dup, "success reused\n");
		free(bo);
		bo = dup;
		goto unlock;
	}

	errno = 0;
	/* lseek() to get bo size */
	size = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_CUR);
	/* store lseek() error number */
	err = -errno;

	atomic_set(&bo->ref, 1);
	bo->handle = handle;
	bo->flags = flags;
	bo->size = size;
	bo->drm = drm;

	VG_BO_ALLOC(bo);

	/* add ourself into the handle table: */
	drmHashInsert(drm->handle_table, handle, bo);

	/* handle lseek() error */
	if (err) {
		VDBG_BO(bo, "lseek failed %d (%s)\n", err, strerror(-err));
		drm_tegra_bo_free(bo);
		bo = NULL;
	} else {
		DBG_BO(bo, "success\n");
	}

unlock:
	pthread_mutex_unlock(&table_lock);

	*bop = bo;

	return err;
}

drm_public
int drm_tegra_bo_get_size(struct drm_tegra_bo *bo, uint32_t *size)
{
	if (!bo || !size)
		return -EINVAL;

	*size = bo->size;

	return 0;
}

drm_public
int drm_tegra_bo_forbid_caching(struct drm_tegra_bo *bo)
{
	if (!bo)
		return -EINVAL;

	bo->reuse = false;

	DBG_BO(bo, "\n");

	return 0;
}
