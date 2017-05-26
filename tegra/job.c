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

#include <xf86drm.h>

#include "private.h"

drm_private
int drm_tegra_job_add_reloc(struct drm_tegra_job *job,
			    const struct drm_tegra_reloc *reloc)
{
	struct drm_tegra_reloc *relocs;
	size_t size;

	size = (job->num_relocs + 1) * sizeof(*reloc);

	relocs = realloc(job->relocs, size);
	if (!reloc)
		return -ENOMEM;

	job->relocs = relocs;

	job->relocs[job->num_relocs++] = *reloc;

	return 0;
}

drm_private
int drm_tegra_job_add_cmdbuf(struct drm_tegra_job *job,
			     const struct drm_tegra_cmdbuf *cmdbuf)
{
	struct drm_tegra_cmdbuf *cmdbufs;
	size_t size;

	size = (job->num_cmdbufs + 1) * sizeof(*cmdbuf);

	cmdbufs = realloc(job->cmdbufs, size);
	if (!cmdbufs)
		return -ENOMEM;

	cmdbufs[job->num_cmdbufs++] = *cmdbuf;
	job->cmdbufs = cmdbufs;

	return 0;
}

drm_public
int drm_tegra_job_new(struct drm_tegra_job **jobp,
		      struct drm_tegra_channel *channel)
{
	struct drm_tegra_job *job;

	job = calloc(1, sizeof(*job));
	if (!job)
		return -ENOMEM;

	DRMINITLISTHEAD(&job->pushbufs);
	job->channel = channel;
	job->syncpt = channel->syncpt;

	*jobp = job;

	return 0;
}

drm_public
int drm_tegra_job_free(struct drm_tegra_job *job)
{
	struct drm_tegra_pushbuf_private *pushbuf;
	struct drm_tegra_pushbuf_private *temp;

	if (!job)
		return -EINVAL;

	DRMLISTFOREACHENTRYSAFE(pushbuf, temp, &job->pushbufs, list)
		drm_tegra_pushbuf_free(&pushbuf->base);

	free(job->cmdbufs);
	free(job->relocs);
	free(job);

	return 0;
}

drm_public
int drm_tegra_job_submit(struct drm_tegra_job *job,
			 struct drm_tegra_fence **fencep)
{
	struct drm_tegra *drm = job->channel->drm;
	struct drm_tegra_fence *fence = NULL;
	struct drm_tegra_syncpt *syncpts;
	struct drm_tegra_submit args;
	int err;

	/*
	 * Make sure the current command stream buffer is queued for
	 * submission.
	 */
	err = drm_tegra_pushbuf_queue(job->pushbuf);
	if (err < 0)
		return err;

	job->pushbuf = NULL;

	if (fencep) {
		fence = calloc(1, sizeof(*fence));
		if (!fence)
			return -ENOMEM;
	}

	syncpts = calloc(1, sizeof(*syncpts));
	if (!syncpts) {
		free(fence);
		return -ENOMEM;
	}

	syncpts[0].id = job->syncpt;
	syncpts[0].incrs = job->increments;

	memset(&args, 0, sizeof(args));
	args.context = job->channel->context;
	args.num_syncpts = 1;
	args.num_cmdbufs = job->num_cmdbufs;
	args.num_relocs = job->num_relocs;
	args.num_waitchks = 0;
	args.waitchk_mask = 0;
	args.timeout = 1000;

	args.syncpts = (uintptr_t)syncpts;
	args.cmdbufs = (uintptr_t)job->cmdbufs;
	args.relocs = (uintptr_t)job->relocs;
	args.waitchks = 0;

	err = drmIoctl(drm->fd, DRM_IOCTL_TEGRA_SUBMIT, &args);
	if (err < 0) {
		free(syncpts);
		free(fence);
		return -errno;
	}

	if (fence) {
		fence->syncpt = job->syncpt;
		fence->value = args.fence;
		fence->drm = drm;
		*fencep = fence;
	}

	free(syncpts);

	return 0;
}
