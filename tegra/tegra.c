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

drm_private int drm_tegra_bo_free(struct drm_tegra_bo *bo)
{
	struct drm_tegra *drm = bo->drm;
	struct drm_gem_close args;
	int err;

	if (bo->reuse)
		VG_BO_FREE(bo);

	if (bo->map)
		munmap(bo->map, bo->size);

	memset(&args, 0, sizeof(args));
	args.handle = bo->handle;

	err = drmIoctl(drm->fd, DRM_IOCTL_GEM_CLOSE, &args);

	free(bo);

	return err;
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

	drm_tegra_bo_cache_cleanup(&drm->bo_cache, 0);

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

	bo = drm_tegra_bo_cache_alloc(&drm->bo_cache, &size, flags);
	if (bo)
		goto out;

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

	err = drmCommandWriteRead(drm->fd, DRM_TEGRA_GEM_CREATE, &args,
				  sizeof(args));
	if (err < 0) {
		err = -errno;
		free(bo);
		return err;
	}

	bo->handle = args.handle;

	VG_BO_ALLOC(bo);
out:
	*bop = bo;

	return 0;
}

drm_public int drm_tegra_bo_wrap(struct drm_tegra_bo **bop, struct drm_tegra *drm,
		      uint32_t handle, uint32_t flags, uint32_t size)
{
	struct drm_tegra_bo *bo;

	if (!drm || !bop)
		return -EINVAL;

	bo = calloc(1, sizeof(*bo));
	if (!bo)
		return -ENOMEM;

	DRMINITLISTHEAD(&bo->push_list);
	DRMINITLISTHEAD(&bo->bo_list);
	atomic_set(&bo->ref, 1);
	bo->handle = handle;
	bo->flags = flags;
	bo->size = size;
	bo->drm = drm;

	*bop = bo;

	return 0;
}

drm_public struct drm_tegra_bo *drm_tegra_bo_ref(struct drm_tegra_bo *bo)
{
	if (bo)
		atomic_inc(&bo->ref);

	return bo;
}

drm_public int drm_tegra_bo_unref(struct drm_tegra_bo *bo)
{
	struct drm_tegra *drm;
	int err = 0;

	if (!bo)
		return -EINVAL;

	if (!atomic_dec_and_test(&bo->ref))
		return 0;

	drm = bo->drm;

	pthread_mutex_lock(&table_lock);

	if (!bo->reuse || (drm_tegra_bo_cache_free(&drm->bo_cache, bo) != 0))
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

drm_public int drm_tegra_bo_map(struct drm_tegra_bo *bo, void **ptr)
{
	struct drm_tegra *drm = bo->drm;

	if (!bo->map) {
		struct drm_tegra_gem_mmap args;
		int err;

		memset(&args, 0, sizeof(args));
		args.handle = bo->handle;

		err = drmCommandWriteRead(drm->fd, DRM_TEGRA_GEM_MMAP, &args,
					  sizeof(args));
		if (err < 0)
			return -errno;

		bo->offset = args.offset;

		bo->map = mmap(0, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED,
			       drm->fd, bo->offset);
		if (bo->map == MAP_FAILED) {
			bo->map = NULL;
			return -errno;
		}

		atomic_set(&bo->mmap_ref, 1);
	} else {
		atomic_inc(&bo->mmap_ref);
	}

	if (ptr)
		*ptr = bo->map;

	return 0;
}

drm_public int drm_tegra_bo_unmap(struct drm_tegra_bo *bo)
{
	if (!bo)
		return -EINVAL;

	if (!bo->map)
		return 0;

	if (!atomic_dec_and_test(&bo->mmap_ref))
		return 0;

	if (munmap(bo->map, bo->size))
		return -errno;

	bo->map = NULL;

	return 0;
}

drm_public int drm_tegra_bo_get_flags(struct drm_tegra_bo *bo, uint32_t *flags)
{
	struct drm_tegra_gem_get_flags args;
	struct drm_tegra *drm = bo->drm;
	int err;

	if (!bo)
		return -EINVAL;

	memset(&args, 0, sizeof(args));
	args.handle = bo->handle;

	err = drmCommandWriteRead(drm->fd, DRM_TEGRA_GEM_GET_FLAGS, &args,
				  sizeof(args));
	if (err < 0)
		return -errno;

	if (flags)
		*flags = args.flags;

	return 0;
}

drm_public int drm_tegra_bo_set_flags(struct drm_tegra_bo *bo, uint32_t flags)
{
	struct drm_tegra_gem_get_flags args;
	struct drm_tegra *drm = bo->drm;
	int err;

	if (!bo)
		return -EINVAL;

	memset(&args, 0, sizeof(args));
	args.handle = bo->handle;
	args.flags = flags;

	err = drmCommandWriteRead(drm->fd, DRM_TEGRA_GEM_SET_FLAGS, &args,
				  sizeof(args));
	if (err < 0)
		return -errno;

	return 0;
}

drm_public int drm_tegra_bo_get_tiling(struct drm_tegra_bo *bo,
			    struct drm_tegra_bo_tiling *tiling)
{
	struct drm_tegra_gem_get_tiling args;
	struct drm_tegra *drm = bo->drm;
	int err;

	if (!bo)
		return -EINVAL;

	memset(&args, 0, sizeof(args));
	args.handle = bo->handle;

	err = drmCommandWriteRead(drm->fd, DRM_TEGRA_GEM_GET_TILING, &args,
				  sizeof(args));
	if (err < 0)
		return -errno;

	if (tiling) {
		tiling->mode = args.mode;
		tiling->value = args.value;
	}

	return 0;
}

drm_public int drm_tegra_bo_set_tiling(struct drm_tegra_bo *bo,
			    const struct drm_tegra_bo_tiling *tiling)
{
	struct drm_tegra_gem_set_tiling args;
	struct drm_tegra *drm = bo->drm;
	int err;

	if (!bo)
		return -EINVAL;

	memset(&args, 0, sizeof(args));
	args.handle = bo->handle;
	args.mode = tiling->mode;
	args.value = tiling->value;

	err = drmCommandWriteRead(drm->fd, DRM_TEGRA_GEM_SET_TILING, &args,
				  sizeof(args));
	if (err < 0)
		return -errno;

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
		if (err < 0)
			return -errno;

		bo->name = args.name;
		bo->reuse = false;
	}

	*name = bo->name;

	return 0;
}

drm_public
int drm_tegra_bo_from_name(struct drm_tegra_bo **bop, struct drm_tegra *drm,
			   uint32_t name, uint32_t flags)
{
	struct drm_gem_open args;
	struct drm_tegra_bo *bo;
	int err;

	if (!drm || !name || !bop)
		return -EINVAL;

	bo = calloc(1, sizeof(*bo));
	if (!bo)
		return -ENOMEM;

	memset(&args, 0, sizeof(args));
	args.name = name;

	err = drmIoctl(drm->fd, DRM_IOCTL_GEM_OPEN, &args);
	if (err < 0) {
		free(bo);
		return -errno;
	}

	atomic_set(&bo->ref, 1);
	bo->handle = args.handle;
	bo->flags = flags;
	bo->size = args.size;
	bo->drm = drm;

	*bop = bo;

	return 0;
}

drm_public
int drm_tegra_bo_to_dmabuf(struct drm_tegra_bo *bo, uint32_t *handle)
{
	struct drm_tegra *drm = bo->drm;
	int prime_fd;
	int err;

	if (!bo || !handle)
		return -EINVAL;

	err = drmPrimeHandleToFD(drm->fd, bo->handle, DRM_CLOEXEC, &prime_fd);
	if (err)
		return err;

	bo->reuse = false;

	*handle = prime_fd;

	return 0;
}

drm_public
int drm_tegra_bo_from_dmabuf(struct drm_tegra_bo **bop, struct drm_tegra *drm,
			     int fd, uint32_t flags)
{
	struct drm_tegra_bo *bo;
	uint32_t handle;
	uint32_t size;
	int err;

	if (!drm || !bop)
		return -EINVAL;

	bo = calloc(1, sizeof(*bo));
	if (!bo)
		return -ENOMEM;

	err = drmPrimeFDToHandle(drm->fd, fd, &handle);
	if (err) {
		free(bo);
		return err;
	}

	/* lseek() to get bo size */
	size = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_CUR);

	atomic_set(&bo->ref, 1);
	bo->handle = handle;
	bo->flags = flags;
	bo->size = size;
	bo->drm = drm;

	*bop = bo;

	return 0;
}
