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
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "private.h"

static inline unsigned long drm_tegra_bo_get_offset(struct drm_tegra_bo *bo,
						    void *ptr)
{
	return (unsigned long)ptr - (unsigned long)bo->map;
}

int host1x_job_create(struct drm_tegra_channel *channel,
		      struct host1x_job **jobp)
{
	struct host1x_syncpt *syncpt;
	struct host1x_job *job;

	if (!channel || !jobp)
		return -EINVAL;

	job = calloc(1, sizeof(*job));
	if (!job)
		return -ENOMEM;

	job->syncpt = channel->syncpts[0];
	job->channel = channel;

	*jobp = job;

	return 0;
}

void host1x_job_free(struct host1x_job *job)
{
	unsigned int i;

	for (i = 0; i < job->num_pushbufs; i++) {
		struct host1x_pushbuf *pb = &job->pushbufs[i];
		drm_tegra_bo_put(pb->bo);
		free(pb->relocs);
	}

	free(job->pushbufs);
	free(job);
}

int host1x_job_reset(struct host1x_job *job)
{
	int i;

	if (!job)
		return -EINVAL;

	for (i = 0; i < job->num_pushbufs; i++) {
		struct host1x_pushbuf *pb = &job->pushbufs[i];
		drm_tegra_bo_put(pb->bo);
		free(pb->relocs);
	}
	free(job->pushbufs);

	job->pushbufs = NULL;
	job->num_pushbufs = 0;
	job->increments = 0;

	return 0;
}

int host1x_job_append(struct host1x_job *job, struct drm_tegra_bo *bo,
		      unsigned long offset, struct host1x_pushbuf **pbp)
{
	struct host1x_pushbuf *pb;
	size_t size;
	void *ptr;
	int err;

	if (!job || !bo || !pbp)
		return -EINVAL;

	err = drm_tegra_bo_map(bo, &ptr);
	if (err < 0)
		return err;

	size = (job->num_pushbufs + 1) * sizeof(*pb);

	pb = realloc(job->pushbufs, size);
	if (!pb)
		return -ENOMEM;

	job->pushbufs = pb;

	pb = &job->pushbufs[job->num_pushbufs++];
	memset(pb, 0, sizeof(*pb));

	pb->syncpt = job->syncpt;
	pb->bo = drm_tegra_bo_get(bo);
	pb->ptr = ptr + offset;
	pb->end = pb->ptr + pb->bo->size / sizeof(uint32_t);
	pb->offset = offset;

	*pbp = pb;

	return 0;
}

int host1x_pushbuf_push(struct host1x_pushbuf *pb, uint32_t word)
{
	if (!pb)
		return -EINVAL;

	assert(pb->ptr <= pb->end);
	if (pb->ptr == pb->end)
		return -EINVAL;

	*pb->ptr++ = word;
	pb->length++;

	TRACE_PUSH("PUSH: %08x\n", word);

	return 0;
}

int host1x_pushbuf_relocate(struct host1x_pushbuf *pb,
			    struct drm_tegra_bo *target, unsigned long offset,
			    unsigned long shift)
{
	struct host1x_pushbuf_reloc *reloc;
	size_t size;

	size = (pb->num_relocs + 1) * sizeof(*reloc);

	reloc = realloc(pb->relocs, size);
	if (!reloc)
		return -ENOMEM;

	pb->relocs = reloc;

	reloc = &pb->relocs[pb->num_relocs++];

	reloc->source_offset = drm_tegra_bo_get_offset(pb->bo, pb->ptr);
	reloc->target_handle = target->handle;
	reloc->target_offset = offset;
	reloc->shift = shift;

	return 0;
}

int host1x_pushbuf_sync(struct host1x_pushbuf *pb, enum host1x_syncpt_cond cond)
{
	int err;

	if (cond >= HOST1X_SYNCPT_COND_MAX)
		return -EINVAL;

	err = host1x_pushbuf_push(pb, cond << 8 | pb->syncpt);
	if (!err)
		pb->increments++;
	return err;
}
