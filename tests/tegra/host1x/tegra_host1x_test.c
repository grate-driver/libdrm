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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Include the code file to access the internals of the library */
#include "tegra/tegra_drm.c"

#define HOST1X_OPCODE_NOP	host1x_opcode_nonincr(0, 0)

/*
 * test_oversized_submit(channel) - Do a submit that does not fit into
 *				    preallocated stream buffer
 */

int test_oversized_submit(struct tegra_channel *channel)
{
	struct tegra_stream *stream = NULL;
	struct tegra_fence fence;
	unsigned int diff_ms;
	int i;

	/* Create a really small buffer */
	if (!(stream = tegra_stream_create(channel, 4, 0, 0)))
		return -1;

	if (tegra_stream_begin(stream, 100, NULL, 0, 0,
	    HOST1X_CLASS_HOST1X))
		goto destroy;
	for (i = 0; i < 100; ++i) {
		if (tegra_stream_push(stream, HOST1X_OPCODE_NOP))
			goto destroy;
	}
	if (tegra_stream_end(stream))
		goto destroy;
	if (tegra_stream_flush(stream, &fence))
		goto destroy;
	if (!tegra_fence_is_valid(&fence))
		goto destroy;
	if (tegra_fence_waitex(channel, &fence, 15000, NULL))
		goto destroy;

	tegra_stream_destroy(stream);
	return 0;

destroy:
	tegra_stream_destroy(stream);
	return -1;

}

/*
 * test_huge_submit(channel) - Do single huge submit and wait for completion
 */

int test_huge_submit(struct tegra_channel *channel)
{
	struct tegra_stream *stream = NULL;
	struct tegra_fence fence;
	const unsigned int submit_count = 1000;
	struct timespec tp_begin, tp_end;
	unsigned int diff_ms;
	int i;

	clock_gettime(CLOCK_MONOTONIC, &tp_begin);

	if (!(stream = tegra_stream_create(channel, 0, 0, 0)))
		return -1;

	/* Create many small submits */
	for (i = 0; i < submit_count; ++i) {
		if (tegra_stream_begin(stream, 1, NULL, 0, 0,
		    HOST1X_CLASS_HOST1X))
			goto destroy;
		if (tegra_stream_push(stream, HOST1X_OPCODE_NOP))
			goto destroy;
		if (tegra_stream_end(stream))
			goto destroy;
	}

	/* Flush all at the same time */
	if (tegra_stream_flush(stream, &fence))
		goto destroy;
	if (!tegra_fence_is_valid(&fence))
		goto destroy;
	if (tegra_fence_waitex(channel, &fence, 15000, NULL))
		goto destroy;

	clock_gettime(CLOCK_MONOTONIC, &tp_end);
	diff_ms = (tp_end.tv_sec - tp_begin.tv_sec) * 1000 +
		(tp_end.tv_nsec - tp_begin.tv_nsec) / 1000000;

	printf("Doing %u iterations in a single submit took %ums\n",
	       submit_count, diff_ms);

	tegra_stream_destroy(stream);
	return 0;

destroy:
	tegra_stream_destroy(stream);
	return -1;

}

/*
 * test_many_small_submits(channel) - Do several small submits and wait for
 *				      completion
 */

int test_many_small_submits(struct tegra_channel *channel)
{
	struct tegra_stream *stream = NULL;
	struct tegra_fence fence;
	const unsigned int submit_count = 1000;
	struct timespec tp_begin, tp_end;
	unsigned int diff_ms;
	int i;

	clock_gettime(CLOCK_MONOTONIC, &tp_begin);

	if (!(stream = tegra_stream_create(channel, 0, 0, 0)))
		return -1;

	/* Create many small submits */
	for (i = 0; i < submit_count; ++i) {
		if (tegra_stream_begin(stream, 1, NULL, 0, 0,
		    HOST1X_CLASS_HOST1X))
			goto destroy;

		if (tegra_stream_push(stream, HOST1X_OPCODE_NOP))
			goto destroy;

		if (tegra_stream_end(stream))
			goto destroy;

		/* Flush each submit separately */
		if (tegra_stream_flush(stream, &fence))
			goto destroy;
	}

	if (!tegra_fence_is_valid(&fence))
		goto destroy;

	/* Wait until complete */
	if (tegra_fence_waitex(channel, &fence, 15000, NULL))
		goto destroy;

	clock_gettime(CLOCK_MONOTONIC, &tp_end);
	diff_ms = (tp_end.tv_sec - tp_begin.tv_sec) * 1000 +
		(tp_end.tv_nsec - tp_begin.tv_nsec) / 1000000;

	printf("Doing %u individual submits took %ums\n", submit_count,
	       diff_ms);

	tegra_stream_destroy(stream);
	return 0;

destroy:
	tegra_stream_destroy(stream);
	return -1;

}

/*
 * test_wait_current_value(channel) - Test waiting the current value with 0
 *				      timeout
 */

int test_wait_current_value(struct tegra_channel *channel)
{
	struct tegra_drm_syncpt_read read_args = {channel->syncpt_id, 0};
	struct tegra_fence fence = {channel->syncpt_id, 0};
	int fd = channel->dev->fd;
	int err = 0;

	if (err = drmIoctl(fd, DRM_IOCTL_TEGRA_SYNCPT_READ, &read_args))
		goto exit;
	fence.value = read_args.value + 1;
	err = tegra_fence_waitex(channel, &fence, 0, NULL);
exit:
	return err;
}

/*
 * test_wait_future_value(channel) - Test waiting future value with 0
 *				     timeout
 */

int test_wait_future_value(struct tegra_channel *channel)
{
	struct tegra_stream *stream = NULL;
	struct tegra_fence fence;
	const unsigned int id = channel->syncpt_id;
	const unsigned int delay_len = 15;
	struct tegra_drm_syncpt_incr incr_args = {channel->syncpt_id, 0};
	int fd = channel->dev->fd;
	int i;

	if (!(stream = tegra_stream_create(channel, 0, 0, 0)))
		return -1;

	/* Wait for a syncpoint increment */
	if (tegra_stream_begin(stream, 2, NULL, 0, 0, HOST1X_CLASS_HOST1X))
		goto destroy;
	if (tegra_stream_push(stream, host1x_opcode_nonincr(
	    host1x_uclass_wait_syncpt_incr_r(), 1)))
		goto destroy;
	if (tegra_stream_push(stream,
	    host1x_uclass_wait_syncpt_incr_indx_f(id)))
		goto destroy;

	/* Tell the library that we're doing a lot of increments */
	stream->num_syncpt_incrs++;

	if (tegra_stream_end(stream))
		goto destroy;

	/* flush and validate fence */
	if (tegra_stream_flush(stream, &fence))
		goto destroy;
	if (!tegra_fence_is_valid(&fence))
		goto destroy;

	/* reading a future value should return an error */
	if (!tegra_fence_waitex(channel, &fence, 0, NULL))
		goto destroy;

	/* let the host continue */
	if (drmIoctl(fd, DRM_IOCTL_TEGRA_SYNCPT_INCR, &incr_args))
			goto destroy_wait_timeout;

	/* wait for the end of submit */
	if (tegra_fence_waitex(channel, &fence, 1000, NULL))
		goto destroy;

	tegra_stream_destroy(stream);
	return 0;

destroy_wait_timeout:
	tegra_fence_waitex(channel, &fence, 15000, NULL);
destroy:
	tegra_stream_destroy(stream);
	return -1;
}

/*
 * test_bad_increment(channel) - Try doing a bad increment
 */

int test_bad_increment(struct tegra_channel *channel)
{
	int fd = channel->dev->fd;
	const unsigned int id = channel->syncpt_id;
	struct tegra_drm_syncpt_incr incr_args = {channel->syncpt_id, 0};
	struct tegra_drm_syncpt_read read_args = {channel->syncpt_id, 0};
	unsigned int value_0, value_1;
	int err = 0;

	/* Read syncpoint value in the beginning */
	if ((err = drmIoctl(fd, DRM_IOCTL_TEGRA_SYNCPT_READ, &read_args)))
		goto exit;
	value_0 = read_args.value;

	/* Try doing an increment (it should not pass) */
	if (!drmIoctl(fd, DRM_IOCTL_TEGRA_SYNCPT_INCR, &incr_args)) {
		err = -1;
		goto exit;
	}

	/* Read the new syncpoint value */
	if ((err = drmIoctl(fd, DRM_IOCTL_TEGRA_SYNCPT_READ, &read_args)))
		goto exit;
	value_1 = read_args.value;

	/* Validate that the kernel did not do any increments */
	if (value_1 != value_0)
		err = -1;
exit:
	return err;
}

int test_host_incr(struct tegra_channel *channel)
{
	struct tegra_stream *stream = NULL;
	struct tegra_fence fence;
	int i;

	if (!(stream = tegra_stream_create(channel, 0, 0, 0)))
		return -1;

	if (tegra_stream_begin(stream, 2, NULL, 0, 0, HOST1X_CLASS_HOST1X))
		goto destroy;
	if (tegra_stream_push_incr(stream, 0))
		goto destroy;
	if (tegra_stream_end(stream))
		goto destroy;

	if (tegra_stream_flush(stream, &fence))
		goto destroy;
	if (!tegra_fence_is_valid(&fence))
		goto destroy;
	if (tegra_fence_waitex(channel, &fence, 15000, NULL))
		goto destroy;

	tegra_stream_destroy(stream);
	return 0;

destroy:
	tegra_stream_destroy(stream);
	return -1;
}

/*
 * test_host_wait(channel) - Make host wait for cpu increments
 */

int test_host_wait(struct tegra_channel *channel)
{
	int fd = channel->dev->fd;
	struct tegra_stream *stream = NULL;
	struct tegra_fence fence;
	const unsigned int syncpt_incrs = 15;
	const unsigned int id = channel->syncpt_id;
	struct tegra_drm_syncpt_incr incr_args = {channel->syncpt_id, 0};
	struct tegra_drm_syncpt_read read_args = {channel->syncpt_id, 0};
	unsigned int value_0, value_1;
	int i;

	/*
	 * Stream construction
	 */

	/* Create stream */
	if (!(stream = tegra_stream_create(channel, 0, 0, 0)))
		return -1;

	/* Start constructing a cmd buffer */
	if (tegra_stream_begin(stream, 1 + syncpt_incrs, NULL, 0, 0,
		HOST1X_CLASS_HOST1X))
		goto destroy;

	/* Wait for syncpoint increments */
	if (tegra_stream_push(stream, host1x_opcode_nonincr(
		host1x_uclass_wait_syncpt_incr_r(), syncpt_incrs)))
		goto destroy;
	for (i = 0; i < syncpt_incrs; ++i)
		if (tegra_stream_push(stream,
		    host1x_uclass_wait_syncpt_incr_indx_f(id)))
			goto destroy;

	/* Tell the library that we're doing a lot of increments */
	stream->num_syncpt_incrs += syncpt_incrs;

	/* End and flush */
	if (tegra_stream_end(stream))
		goto destroy;
	if (tegra_stream_flush(stream, &fence))
		goto destroy;
	if (!tegra_fence_is_valid(&fence))
		goto destroy;

	/*
	 * Act as a client for host1x (i.e. do syncpoint increments)
	 */

	/* Read syncpoint value in the beginning */
	if (drmIoctl(fd, DRM_IOCTL_TEGRA_SYNCPT_READ, &read_args))
		goto destroy_wait_timeout;
	value_0 = read_args.value;

	/* Do increments */
	for (i = 0; i < syncpt_incrs; ++i)
		if (drmIoctl(fd, DRM_IOCTL_TEGRA_SYNCPT_INCR, &incr_args))
			goto destroy_wait_timeout;

	/* The kernel should return immdiately */
	if (tegra_fence_waitex(channel, &fence, 100, NULL))
		goto destroy;

	/* Read the new syncpoint value */
	if (drmIoctl(fd, DRM_IOCTL_TEGRA_SYNCPT_READ, &read_args))
		goto destroy;
	value_1 = read_args.value;

	/* Validate that increments were made correctly */
	if (value_1 != value_0 + syncpt_incrs + 1)
		goto destroy;

	tegra_stream_destroy(stream);
	return 0;

destroy_wait_timeout:
	tegra_fence_waitex(channel, &fence, 15000, NULL);
destroy:
	tegra_stream_destroy(stream);
	return -1;
}

/*
 * test_wait_base(channel) - Make host to do a wait against a base
 */

int test_wait_base(struct tegra_channel *channel)
{
	int fd = channel->dev->fd;
	struct tegra_stream *stream = NULL;
	struct tegra_fence fence;
	const unsigned int syncpt_incrs = 15;
	unsigned int id, base_id;
	struct tegra_drm_syncpt_incr incr_args = {channel->syncpt_id, 0};
	struct tegra_drm_syncpt_read read_args = {channel->syncpt_id, 0};
	struct tegra_drm_get_syncpt get_syncpt_args = {channel->context, 0, 0};
	struct tegra_drm_get_syncpt_base get_base_args = {channel->context, 0, 0};
	unsigned int value_0, value_1;
	int i;

	/* Note: The stream library does not yet have additions for bases.
	 * Get base here */

	if (drmIoctl(fd, DRM_IOCTL_TEGRA_GET_SYNCPT, &get_syncpt_args))
		return -1;

	if (drmIoctl(fd, DRM_IOCTL_TEGRA_GET_SYNCPT_BASE, &get_base_args)) {
		printf("The device does not support syncpoint base. skipping test %s\n",
		       __func__);
		return 0;
	}

	id = get_syncpt_args.id;
	base_id = get_base_args.base_id;

	/*
	 * Stream construction
	 */

	/* Create stream */
	if (!(stream = tegra_stream_create(channel, 0, 0, 0)))
		return -1;

	/* Start constructing a cmd buffer */
	if (tegra_stream_begin(stream, 1 + syncpt_incrs, NULL, 0, 0,
		HOST1X_CLASS_HOST1X))
		goto destroy;

	/* Wait for syncpoint increments */
	if (tegra_stream_push(stream, host1x_opcode_nonincr(
		host1x_uclass_wait_syncpt_incr_r(), syncpt_incrs)))
		goto destroy;

	for (i = 1; i <= syncpt_incrs; ++i)
		if (tegra_stream_push(stream,
		    host1x_uclass_wait_syncpt_base_indx_f(id) |
		    host1x_uclass_wait_syncpt_base_base_indx_f(base_id) |
		    host1x_uclass_wait_syncpt_base_offset_f(i)))
			goto destroy;

	/* Tell the library that we're doing a lot of increments */
	stream->num_syncpt_incrs += syncpt_incrs;

	/* End and flush */
	if (tegra_stream_end(stream))
		goto destroy;
	if (tegra_stream_flush(stream, &fence))
		goto destroy;
	if (!tegra_fence_is_valid(&fence))
		goto destroy;

	/*
	 * Act as a client for host1x (i.e. do syncpoint increments)
	 */

	/* Read syncpoint value in the beginning */
	if (drmIoctl(fd, DRM_IOCTL_TEGRA_SYNCPT_READ, &read_args))
		goto destroy_wait_timeout;
	value_0 = read_args.value;

	/* Do increments */
	for (i = 0; i < syncpt_incrs; ++i)
		if (drmIoctl(fd, DRM_IOCTL_TEGRA_SYNCPT_INCR, &incr_args))
			goto destroy_wait_timeout;

	/* The kernel should return immdiately */
	if (tegra_fence_waitex(channel, &fence, 100, NULL))
		goto destroy;

	/* Read the new syncpoint value */
	if (drmIoctl(fd, DRM_IOCTL_TEGRA_SYNCPT_READ, &read_args))
		goto destroy;
	value_1 = read_args.value;

	/* Validate that increments were made correctly */
	if (value_1 != value_0 + syncpt_incrs + 1)
		goto destroy;

	tegra_stream_destroy(stream);
	return 0;

destroy_wait_timeout:
	tegra_fence_waitex(channel, &fence, 15000, NULL);
destroy:
	tegra_stream_destroy(stream);
	return -1;
}

/*
 * test_pool(channel) - Test stream pool
 *
 * The stream library supports pooling of streams. This means that there is
 * a small pool of preallocated stream buffers. If these buffers run out,
 * the library waits until one is released. This routine tests that we
 * actually can access the preallocated buffers immediately and if no
 * buffers are available, we actually wait until one is free.
 */

int test_pool(struct tegra_channel *channel)
{
	struct tegra_stream *stream = NULL;
	struct tegra_fence fence;
	struct timespec tp_curr, tp_prev;
	const unsigned int pool_size = 3;
	int i;

	if (!(stream = tegra_stream_create(channel, 0, pool_size, 0)))
		return -1;

	for (i = 0; i < pool_size * 2; ++i) {
		unsigned int diff_ms;

		/* measure how long it takes to begin a stream */
		clock_gettime(CLOCK_MONOTONIC, &tp_prev);
		if (tegra_stream_begin(stream, 3, NULL, 0, 0,
		    HOST1X_CLASS_HOST1X))
			goto destroy;
		clock_gettime(CLOCK_MONOTONIC, &tp_curr);
		diff_ms = (tp_curr.tv_sec - tp_prev.tv_sec) * 1000 +
			(tp_curr.tv_nsec - tp_prev.tv_nsec) / 1000000;

		/* it should not be too much as long as we have buffers
		 * available */
		if (diff_ms > 500 && i < pool_size)
			goto destroy;

		/* ..and it should be quite large if no buffers are available
		 * because the library needs to wait for a free buffer */
		if (diff_ms < 500 && i >= pool_size)
			goto destroy;

		/* Make host1x wait 1 sec */
		if (tegra_stream_push(stream, host1x_opcode_nonincr(
		    host1x_uclass_delay_usec_r(), 1)))
			goto destroy;
		if (tegra_stream_push(stream, 0xFFFFF))
			goto destroy;

		/* End and flush */
		if (tegra_stream_end(stream))
			goto destroy;
		if (tegra_stream_flush(stream, &fence))
			goto destroy;
	}

	if (!tegra_fence_is_valid(&fence))
		goto destroy;
	if (tegra_fence_waitex(channel, &fence, 15000, NULL))
		goto destroy;

	tegra_stream_destroy(stream);
	return 0;

destroy:
	tegra_stream_destroy(stream);
	return -1;
}

/*
 * test_push_words(channel) - Test push_words -API
 */

int test_push_words(struct tegra_channel *channel)
{
	struct tegra_stream *stream = NULL;
	struct tegra_fence fence;
	struct tegra_bo *bo = NULL;
	uint32_t *reloc_ptr, reloc_entry_val;
	struct {
		uint32_t nonincr;
		uint32_t reloc;
	} words;

	/* Allocate memory for a relocation */
	if (!(bo = tegra_bo_allocate(channel->dev, 1, 4)))
		goto destroy;

	/* Create and begin a stream */
	if (!(stream = tegra_stream_create(channel, 0, 0, 0)))
		goto destroy;
	if (tegra_stream_begin(stream, 2, NULL, 0, 1, HOST1X_CLASS_GR2D))
		goto destroy;

	/* Push data to dstba register */
	words.nonincr = host1x_opcode_nonincr(0x2b, 1);

	/* Push reloc. Store the temporary value from the library */
	if (tegra_stream_push_words(stream, &words, 2, 1, 0,
				    tegra_reloc(&words.reloc, bo, 0)))
		goto destroy;
	reloc_ptr =
		&stream->active_buffer->data[stream->active_buffer->cmd_ptr - 1];
	reloc_entry_val = *reloc_ptr;

	/* end stream */
	if (tegra_stream_end(stream))
		goto destroy;

	/* push the command buffer to the kernel  */
	if (tegra_stream_flush(stream, &fence))
		goto destroy;

	/* the kernel should have patched the memory address */
	if (reloc_entry_val == *reloc_ptr)
		goto destroy;

	if (!tegra_fence_is_valid(&fence))
		goto destroy;
	if (tegra_fence_waitex(channel, &fence, 15000, NULL))
		goto destroy;

	tegra_bo_free(bo);
	tegra_stream_destroy(stream);
	return 0;

destroy:
	tegra_bo_free(bo);
	tegra_stream_destroy(stream);
	return -1;
}

/*
 * test_reloc_bad_reloc(channel) - Test relocation to a non-address
 *				   register
 */

int test_reloc_bad_reloc(struct tegra_channel *channel)
{
	struct tegra_stream *stream = NULL;
	struct tegra_fence fence;
	struct tegra_bo *bo = NULL;

	/* Allocate memory for a relocation */
	if (!(bo = tegra_bo_allocate(channel->dev, 4096, 4)))
		goto destroy;

	/* Create and begin a stream */
	if (!(stream = tegra_stream_create(channel, 0, 0, 0)))
		goto destroy;
	if (tegra_stream_begin(stream, 2, NULL, 0, 1, HOST1X_CLASS_GR2D))
		goto destroy;

	/* push data to dstba register */
	if (tegra_stream_push(stream, host1x_opcode_nonincr(0x2b, 1)))
		goto destroy;

	/* Push an arbitrary address to that register */
	if (tegra_stream_push(stream, 0xDEADBEEF))
		goto destroy;

	/* end stream */
	if (tegra_stream_end(stream))
		goto destroy;

	/* push the command buffer to the kernel. this should fail */
	if (!tegra_stream_flush(stream, &fence))
		goto destroy;

	tegra_bo_free(bo);
	tegra_stream_destroy(stream);
	return 0;

destroy:
	tegra_bo_free(bo);
	tegra_stream_destroy(stream);
	return -1;
}

/*
 * test_reloc_bad_offset(channel) - Test relocation with a bad offset
 */

int test_reloc_bad_offset(struct tegra_channel *channel)
{
	struct tegra_stream *stream = NULL;
	struct tegra_fence fence;
	struct tegra_bo *bo = NULL;

	/* Allocate memory for a relocation */
	if (!(bo = tegra_bo_allocate(channel->dev, 4096, 4)))
		goto destroy;

	/* Create and begin a stream */
	if (!(stream = tegra_stream_create(channel, 0, 0, 0)))
		goto destroy;
	if (tegra_stream_begin(stream, 2, NULL, 0, 1, HOST1X_CLASS_GR2D))
		goto destroy;

	/* push data to dstba register */
	if (tegra_stream_push(stream, host1x_opcode_nonincr(0x2b, 1)))
		goto destroy;

	/* Push reloc with bad offset */
	if (tegra_stream_push_reloc(stream, bo, 0x100000))
		goto destroy;

	/* end stream */
	if (tegra_stream_end(stream))
		goto destroy;

	/* push the command buffer to the kernel. this should fail */
	if (!tegra_stream_flush(stream, &fence))
		goto destroy;

	tegra_bo_free(bo);
	tegra_stream_destroy(stream);
	return 0;

destroy:
	tegra_bo_free(bo);
	tegra_stream_destroy(stream);
	return -1;
}

/*
 * test_reloc(channel) - Test relocations
 */

int test_reloc(struct tegra_channel *channel)
{
	struct tegra_stream *stream = NULL;
	struct tegra_fence fence;
	struct tegra_bo *bo = NULL;
	uint32_t *reloc_ptr, reloc_entry_val;

	/* Allocate memory for a relocation */
	if (!(bo = tegra_bo_allocate(channel->dev, 1, 4)))
		goto destroy;

	/* Create and begin a stream */
	if (!(stream = tegra_stream_create(channel, 0, 0, 0)))
		goto destroy;
	if (tegra_stream_begin(stream, 2, NULL, 0, 1, HOST1X_CLASS_GR2D))
		goto destroy;

	/* push data to dstba register */
	if (tegra_stream_push(stream, host1x_opcode_nonincr(0x2b, 1)))
		goto destroy;

	/* Push reloc. Store the temporary value from the library */
	reloc_ptr =
		&stream->active_buffer->data[stream->active_buffer->cmd_ptr];
	if (tegra_stream_push_reloc(stream, bo, 0))
		goto destroy;
	reloc_entry_val = *reloc_ptr;

	/* end stream */
	if (tegra_stream_end(stream))
		goto destroy;

	/* push the command buffer to the kernel  */
	if (tegra_stream_flush(stream, &fence))
		goto destroy;

	/* the kernel should have patched the memory address */
	if (reloc_entry_val == *reloc_ptr)
		goto destroy;

	if (!tegra_fence_is_valid(&fence))
		goto destroy;
	if (tegra_fence_waitex(channel, &fence, 15000, NULL))
		goto destroy;

	tegra_bo_free(bo);
	tegra_stream_destroy(stream);
	return 0;

destroy:
	tegra_bo_free(bo);
	tegra_stream_destroy(stream);
	return -1;
}

/*
 * test_timeout(channel) - Test that the kernel survives from timeouts
 */

int test_timeout(struct tegra_channel *channel)
{
	struct tegra_stream *stream = NULL;
	struct tegra_fence fence;
	const unsigned int delay_len = 15;
	int i;

	if (!(stream = tegra_stream_create(channel, 0, 0, 0)))
		return -1;

	if (tegra_stream_begin(stream, 1 + delay_len, NULL, 0, 0,
	    HOST1X_CLASS_HOST1X))
		goto destroy;

	/* make the host wait ~15 seconds. This should trigger the timeout */
	if (tegra_stream_push(stream, host1x_opcode_nonincr(
	    host1x_uclass_delay_usec_r(), delay_len)))
		goto destroy;
	for (i = 0; i < delay_len; ++i)
		if (tegra_stream_push(stream, 0xFFFFF))
			goto destroy;

	if (tegra_stream_end(stream))
		goto destroy;
	if (tegra_stream_flush(stream, &fence))
		goto destroy;
	if (!tegra_fence_is_valid(&fence))
		goto destroy;
	if (tegra_fence_waitex(channel, &fence, 15000, NULL))
		goto destroy;

	tegra_stream_destroy(stream);
	return 0;

destroy:
	tegra_stream_destroy(stream);
	return -1;
}

/*
 * test_drain(channel) - Allocate, use and release memory. Check that we can do
 *			 that again.
 */

int test_bo_drain(struct tegra_channel *channel)
{
	const unsigned int alloc_size = (1024 * 1024);
	const unsigned int num_allocs = 1024;
	const unsigned int num_trials = 5;

	unsigned int first_time_blocks_allocated = 0;
	int i, j, err = 0;

	for (i = 0; i < num_trials; ++i) {
		struct tegra_bo *bos[num_allocs];
		unsigned int blocks_allocated;

		for (j = 0; j < num_allocs; ++j) {
			bos[j] = tegra_bo_allocate(channel->dev, alloc_size,
						   4);
			if (!bos[j])
				break;
			if (!tegra_bo_map(bos[j]))
				break;
			memset(bos[j]->vaddr, j % 256, alloc_size);
		}

		blocks_allocated = j;

		while (j-- > 0) {
			int k = 0;
			for (k = 0; k < alloc_size; ++k)
				if (((char *)bos[j]->vaddr)[k] != (j % 256))
					err = -1;

			/* Test put/get for every 3th allocation */
			if (!(j % 3)) {
				tegra_bo_get(bos[j]);
				tegra_bo_put(bos[j]);
			}

			/* Test that both free and put release the memory */
			if (!(j % 2))
				tegra_bo_free(bos[j]);
			else
				tegra_bo_put(bos[j]);
		}

		if (!first_time_blocks_allocated)
			first_time_blocks_allocated = blocks_allocated;
		else if (first_time_blocks_allocated != blocks_allocated) {
			err = -1;
			break;
		}
	}
	return err;
}

struct test_data {
	const char *name;
	int (*func)(struct tegra_channel *channel);
	int known_failure;
};

#define TEST(test_name)		{#test_name, test_name, 0}
#define FAILING_TEST(test_name)	{#test_name, test_name, 1}

struct test_data tests[] = {
	FAILING_TEST(test_bad_increment),
	TEST(test_wait_current_value),
	FAILING_TEST(test_wait_future_value),
	TEST(test_host_wait),
	TEST(test_wait_base),
	TEST(test_many_small_submits),
	TEST(test_huge_submit),
	TEST(test_oversized_submit),
	TEST(test_bo_drain),
	TEST(test_timeout),
	TEST(test_reloc),
	TEST(test_reloc_bad_reloc),
	FAILING_TEST(test_reloc_bad_offset),
	TEST(test_push_words),
	TEST(test_pool),
	TEST(test_host_incr)
};
const unsigned int num_tests = sizeof(tests) / sizeof(*tests);

int main(int argc, char *argv[])
{
	struct tegra_device *dev;
	struct tegra_channel *channel;
	int fd, i, num_unknown_failures = 0, num_failures = 0;

	fd = drmOpen("tegra", NULL);
	if (fd < 0) {
		printf("Failed to open tegra device!\n");
		goto err_drm_open;
	}

	if (!(dev = tegra_device_create(fd))) {
		printf("Failed to create tegra device!\n");
		goto err_tegra_device_create;
	}

	if (!(channel = tegra_channel_open(dev, TEGRADRM_MODULEID_2D))) {
		printf("Failed to open 2d channel!\n");
		goto err_tegra_channel_open;
	}

	for (i = 0; i < num_tests; ++i) {
		int err = tests[i].func(channel);

		printf("%s: %s\n", tests[i].name, err ? "fail" : "pass");

		num_failures += err ? 1 : 0;
		num_unknown_failures +=
			(err && !tests[i].known_failure) ? 1 : 0;
	}

	printf("\nFailed %d/%d tests\n", num_failures, num_tests);
	if (num_unknown_failures)
		printf("FAILED\n");
	else if (num_failures)
		printf("PASSED with known failures\n");
	else
		printf("PASSED\n");

	tegra_channel_close(channel);
	tegra_device_destroy(dev);
	close(fd);

	return num_unknown_failures ? -1 : 0;

err_tegra_channel_open:
	tegra_device_destroy(dev);
err_tegra_device_create:
	close(fd);
err_drm_open:
	return -1;
}
