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
#include <assert.h>

#include "private.h"

static inline unsigned long drm_tegra_bo_get_offset(struct drm_tegra_bo *bo,
						    void *ptr)
{
	return (unsigned long)ptr - (unsigned long)bo->map;
}

int host1x_pushbuf_push(struct host1x_pushbuf *pb, uint32_t word)
{
	if (!pb)
		return -EINVAL;

	assert(pb->ptr <= pb->end);
	if (pb->ptr == pb->end)
		return -EINVAL;

	*pb->ptr++ = word;
	pb->length++;

	TRACE_PUSH("PUSH: %08x\n", word);

	return 0;
}

int host1x_pushbuf_relocate(struct host1x_pushbuf *pb,
			    struct drm_tegra_bo *target, unsigned long offset,
			    unsigned long shift)
{
	struct host1x_pushbuf_reloc *reloc;
	size_t size;

	size = (pb->num_relocs + 1) * sizeof(*reloc);

	reloc = realloc(pb->relocs, size);
	if (!reloc)
		return -ENOMEM;

	pb->relocs = reloc;

	reloc = &pb->relocs[pb->num_relocs++];

	reloc->source_offset = drm_tegra_bo_get_offset(pb->bo, pb->ptr);
	reloc->target_handle = target->handle;
	reloc->target_offset = offset;
	reloc->shift = shift;

	return 0;
}

int host1x_pushbuf_sync(struct host1x_pushbuf *pb, enum host1x_syncpt_cond cond)
{
	int err;

	if (cond >= HOST1X_SYNCPT_COND_MAX)
		return -EINVAL;

	err = host1x_pushbuf_push(pb, cond << 8 | pb->syncpt);
	if (!err)
		pb->increments++;
	return err;
}
