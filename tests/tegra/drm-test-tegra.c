/*
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <stdio.h>

#include "drm-test-tegra.h"
#include "tegra.h"

int drm_tegra_gr2d_open(struct drm_tegra_gr2d **gr2dp, struct drm_tegra *drm)
{
	struct drm_tegra_gr2d *gr2d;
	int err;

	gr2d = calloc(1, sizeof(*gr2d));
	if (!gr2d)
		return -ENOMEM;

	gr2d->drm = drm;

	err = drm_tegra_channel_open(&gr2d->channel, drm, DRM_TEGRA_GR2D);
	if (err < 0) {
		free(gr2d);
		return err;
	}

	*gr2dp = gr2d;

	return 0;
}

int drm_tegra_gr2d_close(struct drm_tegra_gr2d *gr2d)
{
	if (!gr2d)
		return -EINVAL;

	drm_tegra_channel_close(gr2d->channel);
	free(gr2d);

	return 0;
}

int drm_tegra_gr2d_fill(struct drm_tegra_gr2d *gr2d, struct drm_framebuffer *fb,
			unsigned int x, unsigned int y, unsigned int width,
			unsigned int height, uint32_t color)
{
	struct drm_tegra_bo *fbo = fb->data;
	struct drm_tegra_pushbuf *pushbuf;
	struct drm_tegra_fence *fence;
	struct drm_tegra_job *job;
	struct drm_tegra_bo *bo;
	int err;

	err = drm_tegra_job_new(&job, gr2d->channel);
	if (err < 0)
		return err;

	err = drm_tegra_bo_new(&bo, gr2d->drm, 0, 4096);
	if (err < 0)
		return err;

	err = drm_tegra_pushbuf_new(&pushbuf, job, bo, 0);
	if (err < 0)
		return err;

	*pushbuf->ptr++ = HOST1X_OPCODE_SETCL(0, HOST1X_CLASS_GR2D, 0);

	*pushbuf->ptr++ = HOST1X_OPCODE_MASK(0x9, 0x9);
	*pushbuf->ptr++ = 0x0000003a;
	*pushbuf->ptr++ = 0x00000000;

	*pushbuf->ptr++ = HOST1X_OPCODE_MASK(0x1e, 0x7);
	*pushbuf->ptr++ = 0x00000000;
	*pushbuf->ptr++ = (2 << 16) | (1 << 6) | (1 << 2);
	*pushbuf->ptr++ = 0x000000cc;

	*pushbuf->ptr++ = HOST1X_OPCODE_MASK(0x2b, 0x9);
	drm_tegra_pushbuf_relocate(pushbuf, fbo, 0, 0);
	*pushbuf->ptr++ = 0xdeadbeef;
	*pushbuf->ptr++ = fb->pitch;

	*pushbuf->ptr++ = HOST1X_OPCODE_NONINCR(0x35, 1);
	*pushbuf->ptr++ = color;

	*pushbuf->ptr++ = HOST1X_OPCODE_NONINCR(0x46, 1);
	*pushbuf->ptr++ = 0x00000000;

	*pushbuf->ptr++ = HOST1X_OPCODE_MASK(0x38, 0x5);
	*pushbuf->ptr++ = height << 16 | width;
	*pushbuf->ptr++ = y << 16 | x;

	err = drm_tegra_job_submit(job, &fence);
	if (err < 0) {
		fprintf(stderr, "failed to submit job: %d\n", err);
		return err;
	}

	err = drm_tegra_fence_wait(fence);
	if (err < 0) {
		fprintf(stderr, "failed to wait for fence: %d\n", err);
		return err;
	}

	drm_tegra_pushbuf_free(pushbuf);
	drm_tegra_bo_put(bo);
	drm_tegra_job_free(job);

	return 0;
}
