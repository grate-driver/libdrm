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

#include <linux/errno.h>
#include <linux/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <drm.h>
#include <xf86drm.h>
#include <xf86atomic.h>

#include "hw_host1x01_uclass.h"
#include "host1x01_hardware.h"
#include "tegra_drmif.h"
#include "tegra_drm.h"
#include "class_ids.h"

#define TEGRA_SYNCPT_INVALID	((uint32_t)(-1))

/*
 * Default configuration for new streams
 *
 * NUMBER_OF_BUFFERS - Determine the number of preallocated command buffers
 * RELOC_TABLE_SIZE  - Maximum number of memory references in a command buffer
 * BUFFER_SIZE_WORDS - Define the size of command buffers
 */

#define NUMBER_OF_BUFFERS		4
#define RELOC_TABLE_SIZE		128
#define BUFFER_SIZE_WORDS		1024

enum tegra_stream_status {
	TEGRADRM_STREAM_FREE,
	TEGRADRM_STREAM_CONSTRUCT,
	TEGRADRM_STREAM_READY
};

struct tegra_device {
	int				fd;
};

struct tegra_bo {
	struct tegra_device		*dev;
	void				*vaddr;
	uint32_t			gem_handle;
	unsigned int			offset;
	uint32_t			size;
	atomic_t			refcount;
};

struct tegra_channel {
	struct tegra_device		*dev;
	uint64_t			context;

	enum tegra_module_id		module_id;
	uint32_t			default_class_id;
	uint32_t			syncpt_id;
};

struct tegra_command_buffer {

	struct tegra_bo			*mem;

	struct tegra_drm_reloc		*reloc_table;
	uint32_t			*data;

	uint32_t			cmd_ptr;
	uint32_t			reloc_ptr;
	uint64_t			syncpt_max;

	int				flushed;
	int				preallocated;
};

struct tegra_stream {

	enum tegra_stream_status	status;

	struct tegra_channel		*channel;
	int				num_words;
	int				num_relocs;
	int				num_syncpt_incrs;

	int				num_buffers;
	int				num_max_relocs;
	uint32_t			buffer_size;

	struct tegra_command_buffer	*buffers;
	struct tegra_command_buffer	*active_buffer;
	unsigned int			active_buffer_idx;

	uint32_t			current_class_id;
};

/*
 * tegra_next_buffer(stream)
 *
 * Move to use next command buffer. NOTE! This routine does not verify that the
 * new buffer is ready to use.
 */

static void tegra_next_buffer(struct tegra_stream *stream)
{
	stream->active_buffer_idx =
		(stream->active_buffer_idx + 1) % stream->num_buffers;
	stream->active_buffer = &stream->buffers[stream->active_buffer_idx];
}

/*
 * tegra_release_cmdbuf(buffer)
 *
 * This function releases given command buffer.
 */

static void tegra_release_cmdbuf(struct tegra_command_buffer *buffer)
{
	free(buffer->reloc_table);
	tegra_bo_free(buffer->mem);
	memset(buffer, 0, sizeof(*buffer));
}

/*
 * tegra_allocate_cmdbuf(buffer)
 *
 * This function allocates and initializes a command buffer. cmduf
 * structure must be zeroed before calling this function!
 */

static int tegra_allocate_cmdbuf(struct tegra_device *dev,
				 struct tegra_command_buffer *buffer,
				 uint32_t buffer_size, uint32_t num_relocs)
{

	/* Allocate and map memory for opcodes */

	if (!(buffer->mem =
	    tegra_bo_allocate(dev, sizeof(uint32_t) * buffer_size, 4)))
		goto err_buffer_create;

	if(!(buffer->data = tegra_bo_map(buffer->mem)))
		goto err_buffer_create;

	/* Allocate reloc_table */
	if (!(buffer->reloc_table =
	    malloc(num_relocs * sizeof(struct tegra_drm_reloc))))
		goto err_buffer_create;

	/* Initialize rest of the struct */
	buffer->reloc_ptr = 0;
	buffer->cmd_ptr = 0;

	return 0;

err_buffer_create:
	tegra_release_cmdbuf(buffer);
	return -ENOMEM;
}

/*
 * tegra_device_create(fd)
 *
 * Create a device "object" representing tegra drm device. The device should be
 * opened using i.e. drmOpen(). If object cannot be created, NULL is returned
 */

struct tegra_device *tegra_device_create(int fd)
{
	struct tegra_device *dev;

	if (!(dev = malloc(sizeof(dev))))
		goto err_dev_alloc;
	dev->fd = fd;

	return dev;

err_dev_alloc:
	return NULL;
}

/*
 * tegra_device_destroy(dev)
 *
 * Remove device object created using tegra_device_create(). The caller is
 * responsible for calling drmClose().
 */

void tegra_device_destroy(struct tegra_device *dev)
{
	if (!dev)
		return;
	free(dev);
}

/*
 * tegra_channel_open(dev, module_id)
 *
 * Reserve channel resources for given module. Host1x has several channels
 * each of which is dedicated for a certain hardware module. The opened
 * channel is used by streams for delivering command buffers.
 */

struct tegra_channel *tegra_channel_open(struct tegra_device *dev,
					 enum tegra_module_id module_id)
{
	struct tegra_channel *channel;
	struct tegra_drm_get_syncpt get_args;
	struct tegra_drm_open_channel open_args;
	uint32_t default_class_id;

	if (!(channel = malloc(sizeof(*channel))))
		goto err_channel_alloc;

	switch (module_id) {
	case TEGRADRM_MODULEID_2D:
		default_class_id = HOST1X_CLASS_GR2D;
		break;
	default:
		return NULL;
	}

	channel->dev = dev;
	channel->module_id = module_id;
	channel->default_class_id = default_class_id;

	/* Open the channel */
	open_args.client = default_class_id;
	if (drmIoctl(dev->fd, DRM_IOCTL_TEGRA_OPEN_CHANNEL, &open_args))
		goto err_channel_open;
	channel->context = open_args.context;

	/* Get a syncpoint for the channel */
	memset(&get_args, 0, sizeof(get_args));
	get_args.context = open_args.context;
	if (drmIoctl(dev->fd, DRM_IOCTL_TEGRA_GET_SYNCPT, &get_args))
		goto err_tegra_ioctl;
	channel->syncpt_id = get_args.id;

	return channel;

err_tegra_ioctl:
	drmIoctl(dev->fd, DRM_IOCTL_TEGRA_CLOSE_CHANNEL, &open_args);
err_channel_open:
	free(channel);
err_channel_alloc:
	return NULL;
}

/*
 * tegra_channel_close(channel)
 *
 * Close a channel.
 */

void tegra_channel_close(struct tegra_channel *channel)
{
	struct tegra_drm_close_channel close_args;

	if (!channel)
		return;

	close_args.context = channel->context;
	drmIoctl(channel->dev->fd, DRM_IOCTL_TEGRA_CLOSE_CHANNEL, &close_args);

	free(channel);
}


/*
 * tegra_stream_create(channel)
 *
 * Create a stream for given channel. This function preallocates several
 * command buffers for later usage to improve performance. Streams are
 * used for generating command buffers opcode by opcode using
 * tegra_stream_push().
 */

struct tegra_stream *tegra_stream_create(struct tegra_channel *channel,
					 uint32_t buffer_size,
					 int num_buffers, int num_max_relocs)
{
	struct tegra_stream *stream;
	int i;

	if (!channel)
		goto err_bad_channel;

	if (!(stream = malloc(sizeof(*stream))))
		goto err_alloc_stream;

	memset(stream, 0, sizeof(*stream));
	stream->channel = channel;
	stream->status = TEGRADRM_STREAM_FREE;

	stream->buffer_size = buffer_size ? buffer_size : BUFFER_SIZE_WORDS;
	stream->num_buffers = num_buffers ? num_buffers : NUMBER_OF_BUFFERS;
	stream->num_max_relocs =
		num_max_relocs ? num_max_relocs : RELOC_TABLE_SIZE;

	if (!(stream->buffers =
	    malloc(sizeof(struct tegra_command_buffer) * stream->num_buffers)))
		goto err_alloc_cmdbufs;

	/* tegra_allocate_cmdbuf() assumes buffer elements to be zeroed */
	memset(stream->buffers, 0,
	       sizeof(struct tegra_command_buffer) * stream->num_buffers);

	for (i = 0; i < stream->num_buffers; i++) {
		if (tegra_allocate_cmdbuf(channel->dev, &stream->buffers[i],
			stream->buffer_size, stream->num_max_relocs))
			goto err_buffer_create;

		stream->buffers[i].preallocated = 1;
	}

	stream->active_buffer_idx = 0;
	stream->active_buffer = &stream->buffers[0];

	return stream;

err_buffer_create:
	for (i = 0; i < stream->num_buffers; i++)
		tegra_release_cmdbuf(&stream->buffers[i]);
	free(stream->buffers);
err_alloc_cmdbufs:
	free(stream);
err_alloc_stream:
err_bad_channel:
	return NULL;
}

/*
 * tegra_stream_destroy(stream)
 *
 * Destroy the given stream object. All resrouces are released.
 */

void tegra_stream_destroy(struct tegra_stream *stream)
{
	int i;

	if (!stream)
		return;

	for (i = 0; i < stream->num_buffers; i++) {
		free(stream->buffers[i].reloc_table);
		tegra_bo_free(stream->buffers[i].mem);
	}

	free(stream->buffers);
	free(stream);
}

/*
 * tegra_fence_is_valid(fence)
 *
 * Check validity of a fence.
 */

int tegra_fence_is_valid(const struct tegra_fence *fence)
{
	int valid = 1;
	valid = valid && fence ? 1 : 0;
	valid = valid && fence->id != TEGRA_SYNCPT_INVALID;
	return valid;
}

/*
 * tegra_fence_clear(fence)
 *
 * Clear (=invalidate) given fence
 */

void tegra_fence_clear(struct tegra_fence *fence)
{
	fence->id = TEGRA_SYNCPT_INVALID;
	fence->value = 0;
}

/*
 * tegra_fence_copy(dst, src)
 *
 * Copy fence
 */

void tegra_fence_copy(struct tegra_fence *dst, const struct tegra_fence *src)
{
	*dst = *src;
}

/*
 * tegra_fence_waitex(channel, fence, timeout, value)
 *
 * Wait for a given syncpoint value with timeout. The end value is returned in
 * "value" variable. The function returns 0 if the syncpoint value was
 * reached before timeout, otherwise an error code.
 */

int tegra_fence_waitex(struct tegra_channel *channel,
		       struct tegra_fence *fence, long timeout, long *value)
{
	struct tegra_drm_syncpt_wait args;
	int err;

	if (!tegra_fence_is_valid(fence))
		return -EINVAL;

	args.timeout = timeout;
	args.id = fence->id;
	args.thresh = fence->value;
	args.value = 0;

	err = drmIoctl(channel->dev->fd, DRM_IOCTL_TEGRA_SYNCPT_WAIT, &args);

	if (value)
		*value = args.value;

	return err;
}

/*
 * tegra_fence_wait_timeout(channel, fence, timeout)
 *
 * Wait for a given syncpoint value with timeout. The function returns 0 if
 * the syncpoint value was reached before timeout, otherwise an error code.
 */

int tegra_fence_wait_timeout(struct tegra_channel *channel,
			     struct tegra_fence *fence, long timeout)
{
	return tegra_fence_waitex(channel, fence, timeout, NULL);
}

/*
 * tegra_fence_wait(channel, wait)
 *
 * Wait for a given syncpoint value without timeout.
 */

int tegra_fence_wait(struct tegra_channel *channel, struct tegra_fence *fence)
{
	return tegra_fence_waitex(channel, fence, DRM_TEGRA_NO_TIMEOUT, NULL);
}

/*
 * tegra_stream_push_reloc(stream, h, offset)
 *
 * Push a memory reference to the stream.
 */

int tegra_stream_push_reloc(struct tegra_stream *stream, struct tegra_bo *h,
			    int offset)
{
	struct tegra_drm_reloc reloc;

	if (!(stream && h && stream->num_words >= 1 && stream->num_relocs >= 1))
		return -EINVAL;

	reloc.cmdbuf.handle = stream->active_buffer->mem->gem_handle;
	reloc.cmdbuf.offset = stream->active_buffer->cmd_ptr * 4;
	reloc.target.handle = h->gem_handle;
	reloc.target.offset = offset;
	reloc.shift = 0;

	stream->num_words--;
	stream->num_relocs--;
	stream->active_buffer->data[stream->active_buffer->cmd_ptr++] =
		0xDEADBEEF;
	stream->active_buffer->reloc_table[stream->active_buffer->reloc_ptr++] =
		reloc;

	return 0;
}

/*
 * tegra_bo_gethandle(h)
 *
 * Get drm memory handle. This is required if the object is used as a
 * framebuffer.
 */

uint32_t tegra_bo_gethandle(struct tegra_bo *h)
{
	return h->gem_handle;
}

/*
 * tegra_bo_allocate(dev, num_bytes, alignment)
 *
 * Allocate num_bytes for host1x device operations. The memory is not
 * automatically mapped for the application.
 */

struct tegra_bo *tegra_bo_allocate(struct tegra_device *dev,
				   uint32_t num_bytes, uint32_t alignment)
{
	struct tegra_drm_gem_create create;
	struct tegra_bo *h;

	if (!(h = malloc(sizeof(*h))))
		goto err_alloc_memory_handle;

	/* Allocate memory */
	memset(&create, 0, sizeof(create));
	create.size = num_bytes;
	if (drmIoctl(dev->fd, DRM_IOCTL_TEGRA_GEM_CREATE, &create))
		goto err_alloc_memory;

	h->gem_handle = create.handle;
	h->size = create.size;
	h->offset = 0;
	h->vaddr = NULL;
	h->dev = dev;
	atomic_set(&h->refcount, 1);

	return h;

err_alloc_memory:
	free(h);
err_alloc_memory_handle:
	return NULL;
}

/*
 * tegra_bo_free(h)
 *
 * Release given memory handle. Memory is unmapped if it is mapped. Kernel
 * takes care of reference counting, so the memory area will not be freed
 * unless the kernel actually has finished using the area.
 */

void tegra_bo_free(struct tegra_bo * h)
{
	struct drm_gem_close unref;

	if (!h)
		return;

	tegra_bo_unmap(h);
	unref.handle = h->gem_handle;
	drmIoctl(h->dev->fd, DRM_IOCTL_GEM_CLOSE, &unref);
	free(h);
}

/*
 * tegra_bo_get(h)
 *
 * Increase reference counting to the given bo handle
 */

void tegra_bo_get(struct tegra_bo *h)
{
	if (!h)
		return;
	atomic_inc(&h->refcount);
}

/*
 * tegra_bo_put(h)
 *
 * Decrease reference counting to the given bo handle. The buffer is freed
 * if all references to the buffer object are dropped.
 */

void tegra_bo_put(struct tegra_bo *h)
{
	if (!h)
		return;
	if (atomic_dec_and_test(&h->refcount))
		tegra_bo_free(h);
}
/*
 * tegra_bo_map(h)
 *
 * Map the given handle for the application.
 */

void * tegra_bo_map(struct tegra_bo * h)
{
	if (!h->offset) {
		struct tegra_drm_gem_mmap args = {h->gem_handle, 0};
		int ret;

		ret = drmIoctl(h->dev->fd, DRM_IOCTL_TEGRA_GEM_MMAP, &args);
		if (ret)
			return NULL;
		h->offset = args.offset;
	}

	if (!h->vaddr)
		h->vaddr = mmap(NULL, h->size, PROT_READ | PROT_WRITE,
				MAP_SHARED, h->dev->fd, h->offset);

	return h->vaddr;
}

/*
 * tegra_bo_unmap(h)
 *
 * Unmap memory from the application. The contents of the memory region is
 * automatically flushed to the memory
 */

void tegra_bo_unmap(struct tegra_bo * h)
{
	if (!(h && h->vaddr))
		return;

	munmap(h->vaddr, h->size);
	h->vaddr = NULL;
}

/*
 * tegra_stream_flush(stream, fence)
 *
 * Send the current contents of stream buffer. The stream must be
 * synchronized correctly (we cannot send partial streams). If
 * pointer to fence is given, the fence will contain the syncpoint value
 * that is reached when operations in the buffer are finished.
 */

int tegra_stream_flush(struct tegra_stream *stream, struct tegra_fence *fence)
{
	struct tegra_channel *ch = stream->channel;
	struct tegra_drm_cmdbuf cmdbuf;
	struct tegra_drm_submit submit;
	struct tegra_drm_syncpt syncpt_incr;
	struct tegra_command_buffer * buffer = stream->active_buffer;
	int err;

	if (!stream)
		return -EINVAL;

	/* Reflushing is fine */
	if (stream->status == TEGRADRM_STREAM_FREE)
		return 0;

	/* Return error if stream is constructed badly */
	if(stream->status != TEGRADRM_STREAM_READY)
		return -EINVAL;

	/* Clean args */
	memset(&submit, 0, sizeof(submit));

	/* Construct cmd buffer */
	cmdbuf.handle = buffer->mem->gem_handle;
	cmdbuf.offset = 0;
	cmdbuf.words = buffer->cmd_ptr;

	/* Construct syncpoint increments struct */
	syncpt_incr.id = ch->syncpt_id;
	syncpt_incr.incrs = stream->num_syncpt_incrs;

	/* Create submit */
	submit.context = ch->context;
	submit.num_relocs = buffer->reloc_ptr;
	submit.num_syncpts = 1;
	submit.num_cmdbufs = 1;
	submit.relocs = (uint32_t)buffer->reloc_table;
	submit.syncpts = (uint32_t)&syncpt_incr;
	submit.cmdbufs = (uint32_t)&cmdbuf;

	/* Push submits to the channel */
	if ((err = drmIoctl(ch->dev->fd, DRM_IOCTL_TEGRA_SUBMIT, &submit))) {
		tegra_fence_clear(fence);
		return err;
	}

	/* Return fence */
	if (fence) {
		fence->id = ch->syncpt_id;
		fence->value = submit.fence;
	}

	stream->num_syncpt_incrs = 0;
	stream->active_buffer->syncpt_max = submit.fence;
	stream->active_buffer->flushed = 1;

	/* Release non-preallocated buffers */
	if (!stream->active_buffer->preallocated) {
		tegra_release_cmdbuf(stream->active_buffer);
		free(stream->active_buffer);

		/* Restore the original active buffer */
		stream->active_buffer =
			&stream->buffers[stream->active_buffer_idx];
	}

	stream->status = TEGRADRM_STREAM_FREE;
	return 0;
}

/*
 * tegra_stream_begin(stream, num_words, fence, num_fences, num_syncpt_incrs,
 *		  num_relocs, class_id)
 *
 * Start constructing a stream.
 *  - num_words refer to the maximum number of words the stream can contain.
 *  - fence is a pointer to a table that contains syncpoint preconditions
 *	before the stream execution can start.
 *  - num_fences indicate the number of elements in the fence table.
 *  - num_relocs indicate the number of memory references in the buffer.
 *  - class_id refers to the class_id that is selected in the beginning of a
 *	stream. If no class id is given, the default class id (=usually the
 *	client device's class) is selected.
 *
 * This function verifies that the current buffer has enough room for holding
 * the whole stream (this is computed using num_words and num_relocs). The
 * function blocks until the stream buffer is ready for use.
 */

int tegra_stream_begin(struct tegra_stream *stream, int num_words,
		       struct tegra_fence *fence, int num_fences,
		       int num_relocs, uint32_t class_id)
{
	int i;

	/* check stream and its state */
	if (!(stream && (stream->status == TEGRADRM_STREAM_FREE ||
	    stream->status == TEGRADRM_STREAM_READY)))
		return -EINVAL;

	/* check fence validity */
	for (i = 0; i < num_fences; i++)
		if(!tegra_fence_is_valid(fence + i))
			return -EINVAL;

	/* handle class id */
	if (!class_id && stream->channel->default_class_id)
		class_id = stream->channel->default_class_id;

	/* include following in num words:
	 *  - fence waits in the beginningi ( 1 + num_fences)
	 *  - setclass in the beginning (1 word)
	 *  - syncpoint increment at the end of the stream (2 words)
	 */

	num_words += 2;
	num_words += class_id ? 1 : 0;
	num_words += num_fences ? 1 + num_fences : 0;

	/* Flush, if the current buffer is full */

	if ((stream->active_buffer->cmd_ptr + num_words + num_relocs >
	    stream->buffer_size) ||
	    (stream->active_buffer->reloc_ptr + num_relocs >
	    (uint32_t)stream->num_max_relocs))
		tegra_stream_flush(stream, NULL);

	/* Check if we cannot use a preallocated buffer */

	if ((uint32_t)(num_words + num_relocs) > stream->buffer_size ||
	    num_relocs > stream->num_max_relocs) {
		struct tegra_command_buffer *cmdbuf;

		/* The new stream does not fit into a preallocated buffer.
		 * Allocate a new buffer */

		if (!(cmdbuf = malloc(sizeof(*cmdbuf))))
			return -ENOMEM;
		memset(cmdbuf, 0, sizeof(*cmdbuf));

		if (tegra_allocate_cmdbuf(stream->channel->dev, cmdbuf,
			num_words, num_relocs)) {
			free(cmdbuf);
			return -ENOMEM;
		}

		stream->active_buffer = cmdbuf;

	} else if (stream->active_buffer->flushed) {

		/* We can use preallocated buffer. Make sure the buffer is
		 * actually free */

		struct tegra_fence fence;

		tegra_next_buffer(stream);

		fence.id = stream->channel->syncpt_id;
		fence.value = stream->active_buffer->syncpt_max;
		tegra_fence_wait(stream->channel, &fence);

		stream->active_buffer->cmd_ptr = 0;
		stream->active_buffer->reloc_ptr = 0;
		stream->active_buffer->flushed = 0;
	}

	stream->status = TEGRADRM_STREAM_CONSTRUCT;
	stream->current_class_id = class_id;
	stream->num_relocs = num_relocs;
	stream->num_words = num_words;

	/* Add fences */
	if (num_fences) {
		tegra_stream_push(stream,
				  host1x_opcode_setclass(HOST1X_CLASS_HOST1X,
				  host1x_uclass_wait_syncpt_r(), num_fences));

		for (i = 0; i < num_fences; i++)
			tegra_stream_push(stream,
					  host1x_class_host_wait_syncpt(
					  fence[i].id, fence[i].value));
	}

	if (class_id)
		tegra_stream_push(stream,
				  host1x_opcode_setclass(class_id, 0, 0));

	return 0;
}

/*
 * tegra_stream_push_incr(stream, cond)
 *
 * Push "increment syncpt" opcode to the stream. This function maintains
 * the counter of pushed increments
 */

int tegra_stream_push_incr(struct tegra_stream *stream, uint32_t cond)
{
	if (!(stream && stream->num_words >= 2 &&
	    stream->status == TEGRADRM_STREAM_CONSTRUCT))
		return -EINVAL;

	/* Add syncpoint increment on cond */
	tegra_stream_push(stream, host1x_opcode_nonincr(
					host1x_uclass_incr_syncpt_r(), 1));
	tegra_stream_push(stream, host1x_class_host_incr_syncpt(
					cond, stream->channel->syncpt_id));

	stream->num_syncpt_incrs += 1;

	return 0;
}

/*
 * tegra_stream_push_setclass(stream, class_id)
 *
 * Push "set class" opcode to the stream. Do nothing if the class is already
 * active
 */

int tegra_stream_push_setclass(struct tegra_stream *stream, uint32_t class_id)
{
	if (!(stream && stream->num_words >= 1 &&
	    stream->status == TEGRADRM_STREAM_CONSTRUCT))
		return -EINVAL;

	if (stream->current_class_id == class_id)
		return 0;

	tegra_stream_push(stream, host1x_opcode_setclass(class_id, 0, 0));

	stream->current_class_id = class_id;
	return 0;
}

/*
 * tegra_stream_end(stream)
 *
 * Mark end of stream. This function pushes last syncpoint increment for
 * marking end of stream.
 */

int tegra_stream_end(struct tegra_stream *stream)
{
	if (!(stream && stream->status == TEGRADRM_STREAM_CONSTRUCT &&
	    stream->num_words >= 2))
		return -EINVAL;

	/* Add last syncpoint increment on OP_DONE */
	tegra_stream_push_incr(stream,
			       host1x_uclass_incr_syncpt_cond_op_done_v());

	stream->status = TEGRADRM_STREAM_READY;
	return 0;
}

/*
 * tegra_stream_push(stream, word)
 *
 * Push a single word to given stream.
 */

int tegra_stream_push(struct tegra_stream *stream, uint32_t word)
{
	if (!(stream && stream->num_words >= 1 &&
	    stream->status == TEGRADRM_STREAM_CONSTRUCT))
		return -EINVAL;

	stream->num_words--;
	stream->active_buffer->data[stream->active_buffer->cmd_ptr++] = word;

	return 0;
}

/*
 * tegra_reloc (variable, handle, offset)
 *
 * This function creates a reloc allocation. The function should be used in
 * conjunction with tegra_stream_push_words.
 */

struct tegra_reloc tegra_reloc(const void *var, const struct tegra_bo *h,
			       const uint32_t offset)
{
	struct tegra_reloc reloc = {var, (struct tegra_bo *)h, offset};
	return reloc;

}

/*
 * tegra_stream_push_words(stream, addr, words, ...)
 *
 * Push words from given address to stream. The function takes
 * reloc structs as its argument. You can generate the structs with tegra_reloc
 * function.
 */

int tegra_stream_push_words(struct tegra_stream *stream, const void *addr,
			    int words, int num_relocs, int num_syncpt_incrs,
			    ...)
{
	va_list ap;
	struct tegra_reloc reloc_arg;
	struct tegra_command_buffer *buffer;

	if (!(stream && stream->num_words >= words &&
	    stream->num_relocs >= num_relocs))
		return -EINVAL;

	buffer = stream->active_buffer;

	stream->num_words -= words;
	stream->num_relocs -= num_relocs;
	stream->num_syncpt_incrs += num_syncpt_incrs;

	/* Copy the contents */
	memcpy(buffer->data + buffer->cmd_ptr, addr, words * sizeof(uint32_t));

	/* Copy relocs */
	va_start(ap, num_syncpt_incrs);
	for (; num_relocs; num_relocs--) {

		uint32_t cmd_ptr;
		struct tegra_drm_reloc reloc_entry;

		reloc_arg = va_arg(ap, struct tegra_reloc);

		cmd_ptr = buffer->cmd_ptr +
			((uint32_t *) reloc_arg.addr) - ((uint32_t *) addr);

		reloc_entry.cmdbuf.handle = buffer->mem->gem_handle;
		reloc_entry.cmdbuf.offset = cmd_ptr * 4;
		reloc_entry.target.handle = reloc_arg.h->gem_handle;
		reloc_entry.target.offset = reloc_arg.offset;
		reloc_entry.shift = 0;

		buffer->data[cmd_ptr] = 0xDEADBEEF;
		buffer->reloc_table[buffer->reloc_ptr++] = reloc_entry;
	}
	va_end(ap);

	buffer->cmd_ptr += words;

	return 0;
}
