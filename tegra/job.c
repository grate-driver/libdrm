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

#include "private.h"

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

	DRMINITLISTHEAD(&job->bo_list);

	job->syncpt = channel->syncpts[0];
	job->channel = channel;

	*jobp = job;

	return 0;
}

void host1x_job_free(struct host1x_job *job)
{
	struct drm_tegra_bo *bo;

	DRMLISTFOREACHENTRY(bo, &job->bo_list, list)
		drm_tegra_bo_put(bo);

	free(job->relocs);
	free(job->cmdbufs);
	free(job);
}

int host1x_job_reset(struct host1x_job *job)
{
	struct drm_tegra_bo *bo;

	if (!job)
		return -EINVAL;

	DRMLISTFOREACHENTRY(bo, &job->bo_list, list)
		drm_tegra_bo_put(bo);

	free(job->relocs);
	free(job->cmdbufs);

	job->relocs = NULL;
	job->num_relocs = 0;
}
