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

#ifndef __DRM_TEGRA_PRIVATE_H__
#define __DRM_TEGRA_PRIVATE_H__ 1

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include <libdrm_lists.h>
#include <libdrm_macros.h>
#include <xf86atomic.h>

#include "tegra.h"

#define container_of(ptr, type, member) ({				\
		const typeof(((type *)0)->member) *__mptr = (ptr);	\
		(type *)((char *)__mptr - offsetof(type, member));	\
	})

#define align(offset, align) \
	(((offset) + (align) - 1) & ~((align) - 1))

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

enum host1x_class {
	HOST1X_CLASS_HOST1X = 0x01,
	HOST1X_CLASS_GR2D = 0x51,
	HOST1X_CLASS_GR2D_SB = 0x52,
	HOST1X_CLASS_GR3D = 0x60,
};

struct drm_tegra_bo_bucket {
	uint32_t size;
	drmMMListHead list;
};

struct drm_tegra_bo_cache {
	struct drm_tegra_bo_bucket cache_bucket[14 * 4];
	int num_buckets;
	time_t time;
};

struct drm_tegra {
	struct drm_tegra_bo_cache bo_cache;
	bool close;
	int fd;
};

struct drm_tegra_bo {
	struct drm_tegra *drm;
	drmMMListHead push_list;
	uint32_t handle;
	uint32_t offset;
	uint32_t flags;
	uint32_t size;
	uint32_t name;
	atomic_t ref;
	atomic_t mmap_ref;
	void *map;

	bool reuse;
	drmMMListHead bo_list;	/* bucket-list entry */
	time_t free_time;	/* time when added to bucket-list */
};

struct drm_tegra_channel {
	struct drm_tegra *drm;
	enum host1x_class class;
	uint64_t context;
	uint32_t syncpt;
};

struct drm_tegra_fence {
	struct drm_tegra *drm;
	uint32_t syncpt;
	uint32_t value;
};

struct drm_tegra_pushbuf_private {
	struct drm_tegra_pushbuf base;
	struct drm_tegra_job *job;
	drmMMListHead list;
	drmMMListHead bos;

	struct drm_tegra_bo *bo;
	uint32_t *start;
	uint32_t *end;
};

static inline struct drm_tegra_pushbuf_private *
drm_tegra_pushbuf(struct drm_tegra_pushbuf *pb)
{
	return container_of(pb, struct drm_tegra_pushbuf_private, base);
}

int drm_tegra_pushbuf_queue(struct drm_tegra_pushbuf_private *pushbuf);

struct drm_tegra_job {
	struct drm_tegra_channel *channel;

	unsigned int increments;
	uint32_t syncpt;

	struct drm_tegra_reloc *relocs;
	unsigned int num_relocs;

	struct drm_tegra_cmdbuf *cmdbufs;
	unsigned int num_cmdbufs;

	struct drm_tegra_pushbuf_private *pushbuf;
	drmMMListHead pushbufs;
};

int drm_tegra_job_add_reloc(struct drm_tegra_job *job,
			    const struct drm_tegra_reloc *reloc);
int drm_tegra_job_add_cmdbuf(struct drm_tegra_job *job,
			     const struct drm_tegra_cmdbuf *cmdbuf);

int drm_tegra_bo_free(struct drm_tegra_bo *bo);

void drm_tegra_bo_cache_init(struct drm_tegra_bo_cache *cache, bool coarse);
void drm_tegra_bo_cache_cleanup(struct drm_tegra_bo_cache *cache, time_t time);
struct drm_tegra_bo * drm_tegra_bo_cache_alloc(
		struct drm_tegra_bo_cache *cache,
		uint32_t *size, uint32_t flags);
int drm_tegra_bo_cache_free(struct drm_tegra_bo_cache *cache,
			    struct drm_tegra_bo *bo);

#ifdef HAVE_VALGRIND
#  include <memcheck.h>

/*
 * For tracking the backing memory (if valgrind enabled, we force a mmap
 * for the purposes of tracking)
 */
static inline void VG_BO_ALLOC(struct drm_tegra_bo *bo)
{
	void *map;

	if (bo && RUNNING_ON_VALGRIND) {
		drm_tegra_bo_map(bo, &map);
		VALGRIND_MALLOCLIKE_BLOCK(map, bo->size, 0, 1);
	}
}

static inline void VG_BO_FREE(struct drm_tegra_bo *bo)
{
	VALGRIND_FREELIKE_BLOCK(bo->map, 0);
}

/*
 * For tracking bo structs that are in the buffer-cache, so that valgrind
 * doesn't attribute ownership to the first one to allocate the recycled
 * bo.
 *
 * Note that the bo_list in drm_tegra_bo is used to track the buffers in cache
 * so disable error reporting on the range while they are in cache so
 * valgrind doesn't squawk about list traversal.
 *
 */
static inline void VG_BO_RELEASE(struct drm_tegra_bo *bo)
{
	if (RUNNING_ON_VALGRIND) {
		VALGRIND_DISABLE_ADDR_ERROR_REPORTING_IN_RANGE(bo, sizeof(*bo));
		VALGRIND_MAKE_MEM_NOACCESS(bo, sizeof(*bo));
		VALGRIND_FREELIKE_BLOCK(bo->map, 0);
	}
}
static inline void VG_BO_OBTAIN(struct drm_tegra_bo *bo)
{
	if (RUNNING_ON_VALGRIND) {
		VALGRIND_MAKE_MEM_DEFINED(bo, sizeof(*bo));
		VALGRIND_ENABLE_ADDR_ERROR_REPORTING_IN_RANGE(bo, sizeof(*bo));
		VALGRIND_MALLOCLIKE_BLOCK(bo->map, bo->size, 0, 1);
	}
}
#else
static inline void VG_BO_ALLOC(struct drm_tegra_bo *bo)   {}
static inline void VG_BO_FREE(struct drm_tegra_bo *bo)    {}
static inline void VG_BO_RELEASE(struct drm_tegra_bo *bo) {}
static inline void VG_BO_OBTAIN(struct drm_tegra_bo *bo)  {}
#endif

#endif /* __DRM_TEGRA_PRIVATE_H__ */
