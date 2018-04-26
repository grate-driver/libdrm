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

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
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

#ifndef NDEBUG
#define VDBG_DRM(DRM, FMT, ...) do {					\
	if (DRM->debug_bo)						\
		fprintf(stderr, "%s: %d: " FMT,				\
		__func__, __LINE__, __VA_ARGS__);			\
} while (0)

#define DBG_BO_STATS(DRM) do {						\
	if (DRM->debug_bo)						\
		fprintf(stderr,						\
			"%s: %d:\tstats: "				\
			"total BO's allocated %d (%d bytes, "		\
						 "%d BO's cached) "	\
			"total BO's mapped %d (%d pages, "		\
					      "%d pages cached of %d BO's)\n", \
			__func__, __LINE__,				\
			 drm->debug_bos_allocated,			\
			 drm->debug_bos_total_size,			\
			 drm->debug_bos_cached,				\
			 drm->debug_bos_mapped,				\
			 drm->debug_bos_total_pages,			\
			 drm->debug_bos_cached_pages,			\
			 drm->debug_bos_mappings_cached);		\
} while (0)

#define VDBG_BO(BO, FMT, ...) do {					\
	if (BO->drm->debug_bo)						\
		fprintf(stderr,						\
			"%s: %d:\tBO %p size %u handle %u name %u "	\
			"flags 0x%08X refcnt %d map %p mmap_ref %u "	\
			"map_cached %p: "				\
			FMT,						\
			__func__, __LINE__, BO, BO->size, BO->handle,	\
			BO->name, BO->flags, atomic_read(&BO->ref),	\
			BO->map, BO->mmap_ref, BO->map_cached,		\
			__VA_ARGS__);					\
} while (0)

#define DBG_BO(BO, FMT) VDBG_BO(BO, FMT "%s", "")
#else
#define VDBG_DRM(DRM, FMT, ...)	do {} while (0)
#define DBG_BO_STATS(DRM)	do {} while (0)
#define VDBG_BO(BO, FMT, ...)	do {} while (0)
#define DBG_BO(BO, FMT)		do {} while (0)
#endif

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

struct drm_tegra_bo_mmap_cache {
	drmMMListHead list;
	time_t time;
};

struct drm_tegra {
	/* tables to keep track of bo's, to avoid "evil-twin" buffer objects:
	 *
	 *   handle_table: maps handle to fd_bo
	 *   name_table: maps flink name to fd_bo
	 *
	 * We end up needing two tables, because DRM_IOCTL_GEM_OPEN always
	 * returns a new handle.  So we need to figure out if the bo is already
	 * open in the process first, before calling gem-open.
	 */
	void *handle_table, *name_table;

	struct drm_tegra_bo_cache bo_cache;
	struct drm_tegra_bo_mmap_cache mmap_cache;
	bool close;
	int fd;

#ifndef NDEBUG
	bool debug_bo;
	bool debug_bo_back_guard;
	bool debug_bo_front_guard;
	int32_t debug_bos_allocated;
	int32_t debug_bos_total_size;
	int32_t debug_bos_cached;
	int32_t debug_bos_mapped;
	int32_t debug_bos_total_pages;
	int32_t debug_bos_cached_pages;
	int32_t debug_bos_mappings_cached;
#endif
};

struct drm_tegra_bo {
	struct drm_tegra *drm;
	drmMMListHead push_list;
	uint32_t offset;
	uint32_t handle;
	uint32_t flags;
	uint32_t size;
	uint32_t name;
	atomic_t ref;
	uint32_t mmap_ref;
	void *map;

#ifdef HAVE_VALGRIND
	void *map_vg;
#endif

	bool reuse;
	/*
	 * Cache-accessible fields must be at the end of structure
	 * due to protection of the rest of the fields by valgrind.
	 */
	drmMMListHead bo_list;	/* bucket-list entry */
	time_t free_time;	/* time when added to bucket-list */

	drmMMListHead mmap_list;	/* mmap cache-list entry */
	time_t unmap_time;		/* time when added to cache-list */
	void *map_cached;		/* holds cached mmap pointer */

	bool custom_tiling;
	bool custom_flags;

#ifndef NDEBUG
	uint32_t debug_size;

	uint64_t *guard_front;
	uint64_t *guard_back;
#endif
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
int __drm_tegra_bo_map(struct drm_tegra_bo *bo, void **ptr);

void drm_tegra_bo_cache_init(struct drm_tegra_bo_cache *cache, bool coarse);
void drm_tegra_bo_cache_cleanup(struct drm_tegra *drm, time_t time);
struct drm_tegra_bo * drm_tegra_bo_cache_alloc(struct drm_tegra *drm,
					       uint32_t *size, uint32_t flags);
int drm_tegra_bo_cache_free(struct drm_tegra_bo *bo);
void drm_tegra_bo_cache_unmap(struct drm_tegra_bo *bo);
void *drm_tegra_bo_cache_map(struct drm_tegra_bo *bo);

#if HAVE_VALGRIND
#  include <memcheck.h>

/*
 * For tracking the backing memory (if valgrind enabled, we force a mmap
 * for the purposes of tracking)
 */
static void __attribute__((unused)) VG_BO_ALLOC(struct drm_tegra_bo *bo)
{
	if (RUNNING_ON_VALGRIND) {
		__drm_tegra_bo_map(bo, &bo->map_vg);
		VALGRIND_MALLOCLIKE_BLOCK(bo->map_vg, bo->size, 0, 1);
		VALGRIND_FREELIKE_BLOCK(bo->map_vg, 0);
	}
}

static void __attribute__((unused)) VG_BO_FREE(struct drm_tegra_bo *bo)
{
	if (RUNNING_ON_VALGRIND)
		munmap(bo->map_vg, bo->size);
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
static void __attribute__((unused)) VG_BO_RELEASE(struct drm_tegra_bo *bo)
{
	if (RUNNING_ON_VALGRIND) {
		/*
		 * Disable access in case of an unbalanced BO mmappings to
		 * simulate the unmap that we perform on BO freeing and
		 * avoid double freelike marking that would be reported
		 * by valgrind.
		 */
		if (bo->map)
			VALGRIND_FREELIKE_BLOCK(bo->map_vg, 0);
		/*
		 * Nothing should touch BO now, disable BO memory accesses
		 * to catch them in valgrind, but leave cache related stuff
		 * accessible.
		 */
		VALGRIND_MAKE_MEM_NOACCESS(bo, offsetof(typeof(*bo), bo_list));
	}
}
static void __attribute__((unused)) VG_BO_OBTAIN(struct drm_tegra_bo *bo)
{
	if (RUNNING_ON_VALGRIND) {
		/* restore BO memory accesses in valgrind */
		VALGRIND_MAKE_MEM_DEFINED(bo, offsetof(typeof(*bo), bo_list));

		if (bo->map)
			VALGRIND_MALLOCLIKE_BLOCK(bo->map_vg, bo->size, 0, 1);
	}
}

/*
 * Since we don't really unmap BO under valgrind, we need to mark the
 * mapped region as freed on BO unmapping in order to catch invalid
 * memory accesses.
 */
static void __attribute__((unused)) VG_BO_MMAP(struct drm_tegra_bo *bo)
{
	if (RUNNING_ON_VALGRIND)
		VALGRIND_MALLOCLIKE_BLOCK(bo->map_vg, bo->size, 0, 1);
}
static void __attribute__((unused)) VG_BO_UNMMAP(struct drm_tegra_bo *bo)
{
	if (RUNNING_ON_VALGRIND)
		VALGRIND_FREELIKE_BLOCK(bo->map_vg, 0);
}
#else
static inline void VG_BO_ALLOC(struct drm_tegra_bo *bo)   {}
static inline void VG_BO_FREE(struct drm_tegra_bo *bo)    {}
static inline void VG_BO_RELEASE(struct drm_tegra_bo *bo) {}
static inline void VG_BO_OBTAIN(struct drm_tegra_bo *bo)  {}
static inline void VG_BO_MMAP(struct drm_tegra_bo *bo)    {}
static inline void VG_BO_UNMMAP(struct drm_tegra_bo *bo)  {}
#define RUNNING_ON_VALGRIND 0
#endif

#endif /* __DRM_TEGRA_PRIVATE_H__ */
