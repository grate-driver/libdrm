/*
 * Copyright © 2012, 2013 Thierry Reding
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <string.h>

#include <sys/mman.h>

#include <libdrm_lists.h>
#include <xf86atomic.h>
#include <xf86drm.h>

#include <tegra_drm.h>

#include "tegra.h"

struct drm_tegra {
	drmMMListHead bo_list;
	int fd;
};

struct tegra_bo {
	struct drm_tegra_bo base;
	drmMMListHead list;
	atomic_t ref;
};

static inline struct tegra_bo *tegra_bo(struct drm_tegra_bo *bo)
{
	return (struct tegra_bo *)bo;
}

static void drm_tegra_bo_free(struct drm_tegra_bo *bo)
{
	struct tegra_bo *priv = tegra_bo(bo);
	struct drm_tegra *drm = bo->drm;
	struct drm_gem_close args;

	DRMLISTDEL(&priv->list);

	if (bo->map)
		munmap(bo->map, bo->size);

	memset(&args, 0, sizeof(args));
	args.handle = bo->handle;

	drmIoctl(drm->fd, DRM_IOCTL_GEM_CLOSE, &args);

	free(bo);
}

int drm_tegra_open(int fd, struct drm_tegra **drmp)
{
	struct drm_tegra *drm;
	int err;

	if (fd < 0 || !drmp)
		return -EINVAL;

	drm = calloc(1, sizeof(*drm));
	if (!drm)
		return -ENOMEM;

	DRMINITLISTHEAD(&drm->bo_list);
	drm->fd = fd;

	*drmp = drm;

	return 0;
}

void drm_tegra_close(struct drm_tegra *drm)
{
	if (drm)
		free(drm);
}

int drm_tegra_bo_new(struct drm_tegra *drm, uint32_t flags, uint32_t size,
		     struct drm_tegra_bo **bop)
{
	struct drm_tegra_gem_create args;
	struct drm_tegra_bo *bo;
	struct tegra_bo *priv;
	int err;

	if (!drm || size == 0 || !bop)
		return -EINVAL;

	bo = calloc(1, sizeof(*bo));
	if (!bo)
		return -ENOMEM;

	priv = tegra_bo(bo);

	DRMINITLISTHEAD(&priv->list);
	atomic_set(&priv->ref, 1);
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

	DRMLISTADD(&priv->list, &drm->bo_list);
	bo->handle = args.handle;

	*bop = bo;

	return 0;
}

struct drm_tegra_bo *drm_tegra_bo_get(struct drm_tegra_bo *bo)
{
	if (bo) {
		struct tegra_bo *priv = tegra_bo(bo);
		atomic_inc(&priv->ref);
	}

	return bo;
}

void drm_tegra_bo_put(struct drm_tegra_bo *bo)
{
	if (bo) {
		struct tegra_bo *priv = tegra_bo(bo);

		if (atomic_dec_and_test(&priv->ref))
			drm_tegra_bo_free(bo);
	}
}

int drm_tegra_bo_map(struct drm_tegra_bo *bo)
{
	struct tegra_bo *priv = tegra_bo(bo);
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
	}

	return 0;
}
