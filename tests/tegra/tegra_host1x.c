#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <errno.h>
#include <assert.h>

#include <xf86drm.h>

int main()
{
	int fd, err;
	struct drm_tegra *drm;

	fd = drmOpen("tegra", NULL);
	if (fd < 0) {
		fprintf(stderr, "failed to open.\n");
		return -1;
	}

	err = drm_tegra_open(fd, &drm);
	assert(err >= 0);
	assert(drm);

	return 0;
}
