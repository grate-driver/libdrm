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

#include <xf86drm.h>
#include <tegra_drm.h>

#include "private.h"

static int drm_tegra_channel_setup(struct drm_tegra_channel *channel)
{
	struct drm_tegra *drm = channel->drm;
	unsigned int i;

	for (i = 0; i < HOST1X_MAX_SYNCPOINTS; i++) {
		struct drm_tegra_get_syncpt get_id;
		struct drm_tegra_get_syncpt_base get_base;
		struct host1x_syncpt *syncpt;
		unsigned int size;
		int err;

		memset(&get_id, 0, sizeof(get_id));
		get_id.context = channel->context;
		get_id.index = i;

		err = drmIoctl(drm->fd, DRM_IOCTL_TEGRA_GET_SYNCPT, &get_id);
		if (err < 0)
			break;

		size = (i + 1) * sizeof(*syncpt);

		syncpt = realloc(channel->syncpts, size);
		if (!syncpt) {
			free(channel->syncpts);
			return -ENOMEM;
		}

		channel->syncpts = syncpt;

		syncpt = &channel->syncpts[i];
		memset(syncpt, 0, sizeof(syncpt));
		syncpt->id = get_id.id;

		memset(&get_base, 0, sizeof(get_base));
		get_base.context = channel->context;
		get_base.index = i;

		if (!drmIoctl(drm->fd, DRM_IOCTL_TEGRA_GET_SYNCPT_BASE, &get_base))
			syncpt->base_id = get_base.base_id;
		else
			syncpt->base_id = -1;
	}

	channel->num_syncpts = i;

	fprintf(stdout, "syncpoints: %u\n", channel->num_syncpts);

	for (i = 0; i < channel->num_syncpts; i++)
		fprintf(stdout, "  %u: %u\n", i, channel->syncpts[i].id);

	return 0;
}

int drm_tegra_channel_open(struct drm_tegra *drm, enum host1x_class client,
			   struct drm_tegra_channel **channelp)
{
	struct drm_tegra_open_channel args;
	struct drm_tegra_channel *channel;
	int err;

	channel = calloc(1, sizeof(*channel));
	if (!channel)
		return -ENOMEM;

	channel->drm = drm;

	memset(&args, 0, sizeof(args));
	args.client = client;

	err = drmIoctl(drm->fd, DRM_IOCTL_TEGRA_OPEN_CHANNEL, &args);
	if (err < 0)
		return -errno;

	channel->context = args.context;
	channel->client = client;

	err = drm_tegra_channel_setup(channel);
	if (err < 0)
		return err;

	*channelp = channel;

	return 0;
}

int drm_tegra_channel_close(struct drm_tegra_channel *channel)
{
}

int drm_tegra_channel_get_syncpt_idx(struct drm_tegra_channel *channel)
{
	return channel->syncpts[0].id;
}

int drm_tegra_channel_get_syncpt_base(struct drm_tegra_channel *channel)
{
	return channel->syncpts[0].base_id;
}
