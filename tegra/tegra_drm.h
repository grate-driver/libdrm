/*
 * Copyright (c) 2012-2013, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TEGRA_DRM_H_
#define TEGRA_DRM_H_

struct tegra_drm_gem_create {
	__u64 size;
	__u32 flags;
	__u32 handle;
};

struct tegra_drm_gem_mmap {
	__u32 handle;
	__u32 offset;
};

struct tegra_drm_syncpt_read {
	__u32 id;
	__u32 value;
};

struct tegra_drm_syncpt_incr {
	__u32 id;
	__u32 pad;
};

struct tegra_drm_syncpt_wait {
	__u32 id;
	__u32 thresh;
	__u32 timeout;
	__u32 value;
};

#define DRM_TEGRA_NO_TIMEOUT	(0xffffffff)

struct tegra_drm_open_channel {
	__u32 client;
	__u32 pad;
	__u64 context;
};

struct tegra_drm_close_channel {
	__u64 context;
};

struct tegra_drm_get_syncpt {
	__u64 context;
	__u32 index;
	__u32 id;
};

struct tegra_drm_get_syncpt_base {
	__u64 context;
	__u32 index;
	__u32 base_id;
};

struct tegra_drm_syncpt {
	__u32 id;
	__u32 incrs;
};

struct tegra_drm_cmdbuf {
	__u32 handle;
	__u32 offset;
	__u32 words;
	__u32 pad;
};

struct tegra_drm_reloc {
	struct {
		__u32 handle;
		__u32 offset;
	} cmdbuf;
	struct {
		__u32 handle;
		__u32 offset;
	} target;
	__u32 shift;
	__u32 pad;
};

struct tegra_drm_waitchk {
	__u32 handle;
	__u32 offset;
	__u32 syncpt;
	__u32 thresh;
};

struct tegra_drm_submit {
	__u64 context;
	__u32 num_syncpts;
	__u32 num_cmdbufs;
	__u32 num_relocs;
	__u32 num_waitchks;
	__u32 waitchk_mask;
	__u32 timeout;
	__u32 pad;
	__u64 syncpts;
	__u64 cmdbufs;
	__u64 relocs;
	__u64 waitchks;
	__u32 fence;		/* Return value */

	__u32 reserved[5];	/* future expansion */
};

#define DRM_TEGRA_GEM_CREATE		0x00
#define DRM_TEGRA_GEM_MMAP		0x01
#define DRM_TEGRA_SYNCPT_READ		0x02
#define DRM_TEGRA_SYNCPT_INCR		0x03
#define DRM_TEGRA_SYNCPT_WAIT		0x04
#define DRM_TEGRA_OPEN_CHANNEL		0x05
#define DRM_TEGRA_CLOSE_CHANNEL		0x06
#define DRM_TEGRA_GET_SYNCPT		0x07
#define DRM_TEGRA_SUBMIT		0x08
#define DRM_TEGRA_GET_SYNCPT_BASE	0x09

#define DRM_IOCTL_TEGRA_GEM_CREATE DRM_IOWR(DRM_COMMAND_BASE + DRM_TEGRA_GEM_CREATE, struct tegra_drm_gem_create)
#define DRM_IOCTL_TEGRA_GEM_MMAP DRM_IOWR(DRM_COMMAND_BASE + DRM_TEGRA_GEM_MMAP, struct tegra_drm_gem_mmap)
#define DRM_IOCTL_TEGRA_SYNCPT_READ DRM_IOWR(DRM_COMMAND_BASE + DRM_TEGRA_SYNCPT_READ, struct tegra_drm_syncpt_read)
#define DRM_IOCTL_TEGRA_SYNCPT_INCR DRM_IOWR(DRM_COMMAND_BASE + DRM_TEGRA_SYNCPT_INCR, struct tegra_drm_syncpt_incr)
#define DRM_IOCTL_TEGRA_SYNCPT_WAIT DRM_IOWR(DRM_COMMAND_BASE + DRM_TEGRA_SYNCPT_WAIT, struct tegra_drm_syncpt_wait)
#define DRM_IOCTL_TEGRA_OPEN_CHANNEL DRM_IOWR(DRM_COMMAND_BASE + DRM_TEGRA_OPEN_CHANNEL, struct tegra_drm_open_channel)
#define DRM_IOCTL_TEGRA_CLOSE_CHANNEL DRM_IOWR(DRM_COMMAND_BASE + DRM_TEGRA_CLOSE_CHANNEL, struct tegra_drm_open_channel)
#define DRM_IOCTL_TEGRA_GET_SYNCPT DRM_IOWR(DRM_COMMAND_BASE + DRM_TEGRA_GET_SYNCPT, struct tegra_drm_get_syncpt)
#define DRM_IOCTL_TEGRA_SUBMIT DRM_IOWR(DRM_COMMAND_BASE + DRM_TEGRA_SUBMIT, struct tegra_drm_submit)
#define DRM_IOCTL_TEGRA_GET_SYNCPT_BASE DRM_IOWR(DRM_COMMAND_BASE + DRM_TEGRA_GET_SYNCPT_BASE, struct tegra_drm_get_syncpt_base)

#endif
