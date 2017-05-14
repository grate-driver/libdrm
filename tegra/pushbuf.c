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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "private.h"

#define HOST1X_OPCODE_NONINCR(offset, count) \
	((0x2 << 28) | (((offset) & 0xfff) << 16) | ((count) & 0xffff))

static inline unsigned long
drm_tegra_pushbuf_get_offset(struct drm_tegra_pushbuf *pushbuf)
{
	struct drm_tegra_pushbuf_private *priv = drm_tegra_pushbuf(pushbuf);

	return (unsigned long)pushbuf->ptr - (unsigned long)priv->start;
}

drm_private
int drm_tegra_pushbuf_queue(struct drm_tegra_pushbuf_private *pushbuf)
{
	struct drm_tegra_cmdbuf cmdbuf;
	int err;

	if (!pushbuf || !pushbuf->bo)
		return 0;

	/* unmap buffer object since it won't be accessed anymore */
	drm_tegra_bo_unmap(pushbuf->bo);

	/* add buffer object as command buffers for this job */
	memset(&cmdbuf, 0, sizeof(cmdbuf));
	cmdbuf.words = pushbuf->base.ptr - pushbuf->start;
	cmdbuf.handle = pushbuf->bo->handle;
	cmdbuf.offset = 0;

	err = drm_tegra_job_add_cmdbuf(pushbuf->job, &cmdbuf);
	if (err < 0)
		return err;

	return 0;
}

drm_public
int drm_tegra_pushbuf_new(struct drm_tegra_pushbuf **pushbufp,
			  struct drm_tegra_job *job)
{
	struct drm_tegra_pushbuf_private *pushbuf;

	pushbuf = calloc(1, sizeof(*pushbuf));
	if (!pushbuf)
		return -ENOMEM;

	DRMINITLISTHEAD(&pushbuf->list);
	DRMINITLISTHEAD(&pushbuf->bos);
	pushbuf->job = job;

	*pushbufp = &pushbuf->base;

	DRMLISTADDTAIL(&pushbuf->list, &job->pushbufs);
	job->pushbuf = pushbuf;

	return 0;
}

drm_public
int drm_tegra_pushbuf_free(struct drm_tegra_pushbuf *pushbuf)
{
	struct drm_tegra_pushbuf_private *priv = drm_tegra_pushbuf(pushbuf);
	struct drm_tegra_bo *bo, *tmp;

	if (!pushbuf)
		return -EINVAL;

	drm_tegra_bo_unmap(priv->bo);

	DRMLISTFOREACHENTRYSAFE(bo, tmp, &priv->bos, list)
		drm_tegra_bo_unref(priv->bo);

	DRMLISTDEL(&priv->list);
	free(priv);

	return 0;
}

/**
 * drm_tegra_pushbuf_prepare() - prepare push buffer for a series of pushes
 * @pushbuf: push buffer
 * @words: maximum number of words in series of pushes to follow
 */
drm_public
int drm_tegra_pushbuf_prepare(struct drm_tegra_pushbuf *pushbuf,
			      unsigned int words)
{
	struct drm_tegra_pushbuf_private *priv = drm_tegra_pushbuf(pushbuf);
	struct drm_tegra_channel *channel = priv->job->channel;
	struct drm_tegra_bo *bo;
	void *ptr;
	int err;

	if (priv->bo && (pushbuf->ptr + words < priv->end))
		return 0;

	/*
	 * Align to full pages, since buffer object allocations are page
	 * granular anyway.
	 */
	words = align(words, 1024);

	err = drm_tegra_bo_new(&bo, channel->drm, 0, words * sizeof(uint32_t));
	if (err < 0)
		return err;

	err = drm_tegra_bo_map(bo, &ptr);
	if (err < 0) {
		drm_tegra_bo_unref(bo);
		return err;
	}

	/* queue current command stream buffer for submission */
	err = drm_tegra_pushbuf_queue(priv);
	if (err < 0) {
		drm_tegra_bo_unmap(bo);
		drm_tegra_bo_unref(bo);
		return err;
	}

	DRMLISTADD(&bo->list, &priv->bos);

	priv->start = priv->base.ptr = ptr;
	priv->end = priv->start + bo->size;
	priv->bo = bo;

	return 0;
}

drm_public
int drm_tegra_pushbuf_relocate(struct drm_tegra_pushbuf *pushbuf,
			       struct drm_tegra_bo *target,
			       unsigned long offset,
			       unsigned long shift)
{
	struct drm_tegra_pushbuf_private *priv = drm_tegra_pushbuf(pushbuf);
	struct drm_tegra_reloc reloc;
	int err;

	memset(&reloc, 0, sizeof(reloc));
	reloc.cmdbuf.handle = priv->bo->handle;
	reloc.cmdbuf.offset = drm_tegra_pushbuf_get_offset(pushbuf);
	reloc.target.handle = target->handle;
	reloc.target.offset = offset;
	reloc.shift = shift;

	err = drm_tegra_job_add_reloc(priv->job, &reloc);
	if (err < 0)
		return err;

	*pushbuf->ptr++ = 0xdeadbeef;

	return 0;
}

drm_public
int drm_tegra_pushbuf_sync(struct drm_tegra_pushbuf *pushbuf,
			   enum drm_tegra_syncpt_cond cond)
{
	struct drm_tegra_pushbuf_private *priv = drm_tegra_pushbuf(pushbuf);
	int err;

	if (cond >= DRM_TEGRA_SYNCPT_COND_MAX)
		return -EINVAL;

	err = drm_tegra_pushbuf_prepare(pushbuf, 2);
	if (err < 0)
		return err;

	*pushbuf->ptr++ = HOST1X_OPCODE_NONINCR(0x0, 0x1);
	*pushbuf->ptr++ = cond << 8 | priv->job->syncpt;
	priv->job->increments++;

	return 0;
}
