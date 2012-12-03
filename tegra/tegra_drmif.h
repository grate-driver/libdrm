/*
 * Copyright (C) 2012-2013 NVIDIA Corporation.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *	Arto Merilainen <amerilainen@nvidia.com>
 */

#ifndef TEGRA_DRMIF_H_
#define TEGRA_DRMIF_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct tegra_channel;
struct tegra_bo;
struct tegra_stream;
struct tegra_device;

struct tegra_fence {
	uint32_t id;
	uint32_t value;
};

struct tegra_reloc {
	const void *addr;
	struct tegra_bo *h;
	uint32_t offset;
};

enum tegra_module_id {
	TEGRADRM_MODULEID_2D
};

/* Device operations */
struct tegra_device *tegra_device_create(int fd);
void tegra_device_destroy(struct tegra_device *dev);

/* Memory operations */
uint32_t tegra_bo_gethandle(struct tegra_bo *handle);
struct tegra_bo *tegra_bo_allocate(struct tegra_device *dev,
				   uint32_t num_bytes, uint32_t alignment);
void tegra_bo_free(struct tegra_bo * handle);
void * tegra_bo_map(struct tegra_bo * handle);
void tegra_bo_unmap(struct tegra_bo * handle);
void tegra_bo_get(struct tegra_bo *handle);
void tegra_bo_put(struct tegra_bo *handle);

/* Channel operations */
struct tegra_channel *tegra_channel_open(struct tegra_device *dev,
					 enum tegra_module_id module_id);
void tegra_channel_close(struct tegra_channel *channel);

/* Stream operations */
struct tegra_stream *tegra_stream_create(struct tegra_channel *channel,
					 uint32_t buffer_size,
					 int num_buffers, int num_max_relocs);
void tegra_stream_destroy(struct tegra_stream *stream);
int tegra_stream_begin(struct tegra_stream *stream, int num_words,
		       struct tegra_fence *fence, int num_fences,
		       int num_relocs, uint32_t class_id);
int tegra_stream_end(struct tegra_stream *stream);
int tegra_stream_flush(struct tegra_stream *stream, struct tegra_fence *fence);
int tegra_stream_push(struct tegra_stream *stream, uint32_t word);
int tegra_stream_push_incr(struct tegra_stream *stream, uint32_t cond);
int tegra_stream_push_setclass(struct tegra_stream *stream, uint32_t class_id);
int tegra_stream_push_reloc(struct tegra_stream *stream,
			    struct tegra_bo *handle, int offset);
struct tegra_reloc tegra_reloc(const void *var, const struct tegra_bo *handle,
			       const uint32_t offset);
int tegra_stream_push_words(struct tegra_stream *stream, const void *addr,
			    int words, int num_relocs, int num_syncpt_incrs,
			    ...);

/* Fence operations */
int tegra_fence_wait(struct tegra_channel *channel, struct tegra_fence *fence);
int tegra_fence_wait_timeout(struct tegra_channel *channel,
			     struct tegra_fence *fence, long timeout);
int tegra_fence_waitex(struct tegra_channel *channel,
		       struct tegra_fence *fence, long timeout, long *value);
int tegra_fence_is_valid(const struct tegra_fence *fence);
void tegra_fence_clear(struct tegra_fence *fence);
void tegra_fence_copy(struct tegra_fence *dst, const struct tegra_fence *src);

#ifdef __cplusplus
};
#endif

#endif
