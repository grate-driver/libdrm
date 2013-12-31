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
#include <string.h>
#include <assert.h>

#include "private.h"

int host1x_pushbuf_create(struct host1x_job *job, struct host1x_pushbuf **pbp)
{
	struct host1x_pushbuf *pb;

	if (!job || !pbp)
		return -EINVAL;

	pb = malloc(sizeof(*pb));
	memset(pb, 0, sizeof(*pb));
	pb->job = job;

	*pbp = pb;
	return 0;
}

int host1x_pushbuf_free(struct host1x_pushbuf *pb)
{
	/* TODO: actually free */
}

static int host1x_pushbuf_realloc(struct host1x_pushbuf *pb, size_t words)
{
	struct drm_tegra_bo *bo;
	struct drm_tegra_cmdbuf *cmdbuf;
	int err;

	if (!pb)
		return -EINVAL;

	assert(pb->job && pb->job->channel && pb->job->channel->drm);

	err = drm_tegra_bo_create(pb->job->channel->drm, 0, sizeof(uint32_t) * words, &bo);
	if (err < 0)
		return err;

	err = drm_tegra_bo_map(bo, (void **)&pb->start);
	if (err < 0)
		goto failure;
	pb->ptr = pb->start;
	pb->end = pb->start + words;

	DRMLISTADD(&bo->list, &pb->job->bo_list);

	cmdbuf = realloc(pb->job->cmdbufs, (pb->job->num_cmdbufs + 1) * sizeof(*cmdbuf));
	if (!cmdbuf) {
		err = errno;
		goto failure;
	}
	pb->job->cmdbufs = cmdbuf;
	cmdbuf += pb->job->num_cmdbufs++;

	cmdbuf->handle = bo->handle;
	cmdbuf->offset = 0;
	cmdbuf->words = 0;
	pb->cmdbuf = cmdbuf;

	return 0;

failure:
	drm_tegra_bo_put(bo);
	return err;
}

int host1x_pushbuf_room(struct host1x_pushbuf *pb, int words)
{
	if (pb->ptr + words < pb->end)
		return 0;

	if (words < 4096)
		words = 4096;

	return host1x_pushbuf_realloc(pb, words);
}

int host1x_pushbuf_push(struct host1x_pushbuf *pb, uint32_t word)
{
	if (!pb)
		return -EINVAL;

	assert(pb->ptr < pb->end);

	*pb->ptr++ = word;
	pb->cmdbuf->words++;

	TRACE_PUSH("PUSH: %08x\n", word);

	return 0;
}

int host1x_pushbuf_relocate(struct host1x_pushbuf *pb,
			    struct drm_tegra_bo *target, unsigned long offset,
			    unsigned long shift)
{
	struct drm_tegra_reloc *reloc;
	size_t size;

	size = (pb->job->num_relocs + 1) * sizeof(*reloc);

	reloc = realloc(pb->job->relocs, size);
	if (!reloc)
		return -ENOMEM;

	pb->job->relocs = reloc;

	reloc = &pb->job->relocs[pb->job->num_relocs++];

	reloc->cmdbuf.handle = pb->cmdbuf->handle;
	reloc->cmdbuf.offset = (unsigned long)pb->ptr - (unsigned long)pb->start;
	reloc->target.handle = target->handle;
	reloc->target.offset = offset;
	reloc->shift = shift;

	return 0;
}

int host1x_pushbuf_sync(struct host1x_pushbuf *pb, enum host1x_syncpt_cond cond)
{
	int err;

	if (cond >= HOST1X_SYNCPT_COND_MAX)
		return -EINVAL;

	err = host1x_pushbuf_push(pb, cond << 8 | pb->job->syncpt);
	if (!err)
		pb->job->increments++;
	return err;
}
