#ifndef TEGRA_PRIVATE_H
#define TEGRA_PRIVATE_H

#include <libdrm_lists.h>
#include <xf86atomic.h>

#include "tegra.h"
#include "tegra_drm.h"

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
	int fd;
};

typedef uint32_t host1x_syncpt_t;

#define HOST1X_MAX_SYNCPOINTS 32

struct drm_tegra_channel {
	struct drm_tegra *drm;

	enum host1x_class client;
	uint64_t context;

	host1x_syncpt_t *syncpts;
	unsigned int num_syncpts;
};

struct host1x_pushbuf {
	struct host1x_job *job;

	struct drm_tegra_cmdbuf *cmdbuf;

	uint32_t *start, *ptr, *end;
};

struct host1x_fence {
	host1x_syncpt_t syncpt;
	uint32_t value;
};

struct host1x_job {
	struct drm_tegra_channel *channel;

	host1x_syncpt_t syncpt;
	unsigned int increments;

	struct drm_tegra_reloc *relocs;
	unsigned int num_relocs;

	struct drm_tegra_cmdbuf *cmdbufs;
	unsigned int num_cmdbufs;

	drmMMListHead bo_list;
};

#if 1
# define TRACE_IOCTL(fmt, ...) drmMsg("TEGRA IOCTL: " fmt, ##__VA_ARGS__)
# define TRACE_PUSH(fmt, ...) drmMsg("TEGRA PUSH: " fmt, ##__VA_ARGS__)
#else
# define TRACE_IOCTL(fmt, ...)
# define TRACE_PUSH(fmt, ...)
#endif

#endif
