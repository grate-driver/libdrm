#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <errno.h>
#include <assert.h>

#include <xf86drm.h>

#include <tegra.h>

static int test_host1x_push(struct drm_tegra *drm, struct drm_tegra_channel *channel)
{
	int err, i, pushes = 16;
	struct host1x_pushbuf *pb;
	struct host1x_job *job;
	struct drm_tegra_bo *cmdbuf;

	err = host1x_job_create(channel, &job);
	if (err < 0) {
		fprintf(stderr, "host1x_job_create() failed: %d\n", err);
		return -1;
	}
	assert(job);

	err = drm_tegra_bo_create(drm, 0, sizeof(uint32_t) * pushes, &cmdbuf);
	if (err < 0) {
		fprintf(stderr, "drm_tegra_bo_create() failed: %d\n", err);
		return -1;
	}
	assert(cmdbuf);

	err = host1x_job_append(job, cmdbuf, 0, &pb);
	if (err < 0) {
		fprintf(stderr, "host1x_job_append() failed: %d\n", err);
		return -1;
	}
	assert(pb);

	/* thsee should push just fine */
	for (i = 0; i < pushes; ++i)
		if (host1x_pushbuf_push(pb, 0) < 0) {
			fprintf(stderr, "host1x_pushbuf_push failed!\n");
			return -1;
		}

	/* but this one should overflow the bo */
	if (host1x_pushbuf_push(pb, 0) == 0) {
		fprintf(stderr, "host1x_pushbuf_push failed!\n");
		return -1;
	}

	drm_tegra_bo_put(cmdbuf);

	return 0;
}

int main()
{
	int fd, err;
	struct drm_tegra *drm;
	struct drm_tegra_channel *channel;

	fd = drmOpen("tegra", NULL);
	if (fd < 0) {
		fprintf(stderr, "drmOpem failed\n");
		return -1;
	}

	err = drm_tegra_open(fd, &drm);
	if (err < 0) {
		fprintf(stderr, "drm_tegra_open() failed: %d\n", err);
		return -1;
	}
	assert(drm);

	err = drm_tegra_channel_open(drm, HOST1X_CLASS_GR2D, &channel);
	if (err < 0) {
		fprintf(stderr, "drm_tegra_channel_open failed: %d\n", err);
		return -1;
	}
	assert(channel);

	err = test_host1x_push(drm, channel);
	if (err < 0) {
		fprintf(stderr, "test_gr2d_fill failed\n");
		return -1;
	}

	return 0;
}
