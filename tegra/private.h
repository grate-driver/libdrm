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

#include <libdrm_lists.h>
#include <libdrm_macros.h>
#include <xf86atomic.h>

#include "tegra_drm.h"
#include "tegra.h"

#define container_of(ptr, type, member) ({				\
		const typeof(((type *)0)->member) *__mptr = (ptr);	\
		(type *)((char *)__mptr - offsetof(type, member));	\
	})

#define align(offset, align) \
	(((offset) + (align) - 1) & ~((align) - 1))

enum host1x_class {
	HOST1X_CLASS_HOST1X = 0x01,
	HOST1X_CLASS_GR2D = 0x51,
	HOST1X_CLASS_GR2D_SB = 0x52,
	HOST1X_CLASS_GR3D = 0x60,
};

struct drm_tegra {
	bool close;
	int fd;
};

struct drm_tegra_bo {
	struct drm_tegra *drm;
	drmMMListHead list;
	uint32_t handle;
	uint32_t offset;
	uint32_t flags;
	uint32_t size;
	atomic_t ref;
	void *map;
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

#endif /* __DRM_TEGRA_PRIVATE_H__ */
