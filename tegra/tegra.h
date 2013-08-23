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

#ifndef __DRM_TEGRA_H__
#define __DRM_TEGRA_H__ 1

#include <stdint.h>
#include <stdlib.h>

enum host1x_class {
	HOST1X_CLASS_HOST1X = 0x01,
	HOST1X_CLASS_GR2D = 0x51,
	HOST1X_CLASS_GR2D_SB = 0x52,
	HOST1X_CLASS_GR3D = 0x60,
};

#define HOST1X_OPCODE_SETCL(offset, classid, mask) \
	((0x0 << 28) | (((offset) & 0xfff) << 16) | (((classid) & 0x3ff) << 6) | ((mask) & 0x3f))
#define HOST1X_OPCODE_INCR(offset, count) \
	((0x1 << 28) | (((offset) & 0xfff) << 16) | ((count) & 0xffff))
#define HOST1X_OPCODE_NONINCR(offset, count) \
	((0x2 << 28) | (((offset) & 0xfff) << 16) | ((count) & 0xffff))
#define HOST1X_OPCODE_MASK(offset, mask) \
	((0x3 << 28) | (((offset) & 0xfff) << 16) | ((mask) & 0xffff))
#define HOST1X_OPCODE_IMM(offset, data) \
	((0x4 << 28) | (((offset) & 0xfff) << 16) | ((data) & 0xffff))
#define HOST1X_OPCODE_EXTEND(subop, value) \
	((0xe << 28) | (((subop) & 0xf) << 24) | ((value) & 0xffffff))

enum host1x_syncpt_cond {
	HOST1X_SYNCPT_COND_IMMEDIATE,
	HOST1X_SYNCPT_COND_OP_DONE,
	HOST1X_SYNCPT_COND_RD_DONE,
};

struct host1x_pushbuf;
struct host1x_syncpt;
struct host1x_fence;
struct host1x_job;

struct drm_tegra_bo;
struct drm_tegra_channel;
struct drm_tegra;

int drm_tegra_open(int fd, struct drm_tegra **drmp);
void drm_tegra_close(struct drm_tegra *drm);
int drm_tegra_submit(struct drm_tegra *drm, struct host1x_job *job,
		     struct host1x_fence **fencep);
int drm_tegra_wait(struct drm_tegra *drm, struct host1x_fence *fence,
		   uint32_t timeout);
int drm_tegra_signaled(struct drm_tegra *drm, struct host1x_fence *fence);

int drm_tegra_channel_open(struct drm_tegra *drm, enum host1x_class client,
			   struct drm_tegra_channel **channelp);
int drm_tegra_channel_close(struct drm_tegra_channel *channel);

int drm_tegra_bo_create(struct drm_tegra *drm, uint32_t flags, uint32_t size,
			struct drm_tegra_bo **bop);
int drm_tegra_bo_open(struct drm_tegra *drm, uint32_t name,
		      struct drm_tegra_bo **bop);
int drm_tegra_bo_get_handle(struct drm_tegra_bo *bo, uint32_t *handle);
int drm_tegra_bo_get_name(struct drm_tegra_bo *bo, uint32_t *name);
int drm_tegra_bo_map(struct drm_tegra_bo *bo, void **ptr);
int drm_tegra_bo_unmap(struct drm_tegra_bo *bo);
struct drm_tegra_bo *drm_tegra_bo_get(struct drm_tegra_bo *bo);
void drm_tegra_bo_put(struct drm_tegra_bo *bo);

int host1x_job_create(struct drm_tegra_channel *channel,
		      struct host1x_job **jobp);
void host1x_job_free(struct host1x_job *job);
int host1x_job_reset(struct host1x_job *job);
int host1x_job_append(struct host1x_job *job, struct drm_tegra_bo *bo,
		      unsigned long offset, struct host1x_pushbuf **pbp);
int host1x_pushbuf_push(struct host1x_pushbuf *pb, uint32_t word);
int host1x_pushbuf_relocate(struct host1x_pushbuf *pb,
			    struct drm_tegra_bo *target, unsigned long offset,
			    unsigned long shift);
int host1x_pushbuf_sync(struct host1x_pushbuf *pb,
			enum host1x_syncpt_cond cond);

#endif /* __DRM_TEGRA_H__ */
