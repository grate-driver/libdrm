#ifndef TEGRA_PRIVATE_H
#define TEGRA_PRIVATE_H

#include <libdrm_lists.h>
#include <xf86atomic.h>

#include "tegra.h"

struct drm_tegra_bo {
	struct drm_tegra *drm;
	drmMMListHead list;
	uint32_t handle;
	uint32_t offset;
	uint32_t flags;
	uint32_t size;
	uint32_t name;
	atomic_t ref;
	void *map;
};

struct drm_tegra {
	drmMMListHead bo_list;
	int fd;
};

struct host1x_syncpt {
	uint32_t id;
	int base_id;
};

#define HOST1X_MAX_SYNCPOINTS 32

struct drm_tegra_channel {
	struct drm_tegra *drm;

	enum host1x_class client;
	uint64_t context;

	struct host1x_syncpt *syncpts;
	unsigned int num_syncpts;
};

struct host1x_pushbuf_reloc {
	unsigned long source_offset;
	unsigned long target_handle;
	unsigned long target_offset;
	unsigned long shift;
};

struct host1x_pushbuf {
	struct host1x_syncpt *syncpt;
	unsigned int increments;

	struct drm_tegra_bo *bo;
	unsigned long offset;
	unsigned long length;

	struct host1x_pushbuf_reloc *relocs;
	unsigned int num_relocs;

	uint32_t *ptr;
};

struct host1x_fence {
	struct host1x_syncpt *syncpt;
	uint32_t value;
};

struct host1x_job {
	struct drm_tegra_channel *channel;

	struct host1x_syncpt *syncpt;
	unsigned int increments;

	struct host1x_pushbuf *pushbufs;
	unsigned int num_pushbufs;
};

#endif
