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
#include <string.h>

#include <xf86drm.h>

#include "private.h"

static int drm_tegra_channel_setup(struct drm_tegra_channel *channel)
{
	struct drm_tegra *drm = channel->drm;
	struct drm_tegra_get_syncpt args;
	int err;

	memset(&args, 0, sizeof(args));
	args.context = channel->context;
	args.index = 0;

	err = drmCommandWriteRead(drm->fd, DRM_TEGRA_GET_SYNCPT, &args,
				  sizeof(args));
	if (err < 0)
		return err;

	channel->syncpt = args.id;

	return 0;
}

drm_public
int drm_tegra_channel_open(struct drm_tegra_channel **channelp,
			   struct drm_tegra *drm,
			   enum drm_tegra_class client)
{
	struct drm_tegra_open_channel args;
	struct drm_tegra_channel *channel;
	enum host1x_class class;
	int err;

	if (!channelp || !drm)
		return -EINVAL;

	switch (client) {
	case DRM_TEGRA_GR2D:
		class = HOST1X_CLASS_GR2D;
		break;

	case DRM_TEGRA_GR3D:
		class = HOST1X_CLASS_GR3D;
		break;

	default:
		return -EINVAL;
	}

	channel = calloc(1, sizeof(*channel));
	if (!channel)
		return -ENOMEM;

	channel->drm = drm;

	memset(&args, 0, sizeof(args));
	args.client = class;

	err = drmCommandWriteRead(drm->fd, DRM_TEGRA_OPEN_CHANNEL, &args,
				  sizeof(args));
	if (err < 0) {
		free(channel);
		return err;
	}

	channel->context = args.context;
	channel->class = class;

	err = drm_tegra_channel_setup(channel);
	if (err < 0) {
		free(channel);
		return err;
	}

	*channelp = channel;

	return 0;
}

drm_public
int drm_tegra_channel_close(struct drm_tegra_channel *channel)
{
	struct drm_tegra_close_channel args;
	struct drm_tegra *drm;
	int err;

	if (!channel)
		return -EINVAL;

	drm = channel->drm;

	memset(&args, 0, sizeof(args));
	args.context = channel->context;

	err = drmCommandWriteRead(drm->fd, DRM_TEGRA_CLOSE_CHANNEL, &args,
				  sizeof(args));
	if (err < 0)
		return err;

	free(channel);

	return 0;
}
