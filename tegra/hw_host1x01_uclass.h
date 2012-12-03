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

 /*
  * Function naming determines intended use:
  *
  *	 <x>_r(void) : Returns the offset for register <x>.
  *
  *	 <x>_w(void) : Returns the word offset for word (4 byte) element <x>.
  *
  *	 <x>_<y>_s(void) : Returns size of field <y> of register <x> in bits.
  *
  *	 <x>_<y>_f(uint32_t v) : Returns a value based on 'v' which has been shifted
  *		 and masked to place it at field <y> of register <x>.  This value
  *		 can be |'d with others to produce a full register value for
  *		 register <x>.
  *
  *	 <x>_<y>_m(void) : Returns a mask for field <y> of register <x>.  This
  *		 value can be ~'d and then &'d to clear the value of field <y> for
  *		 register <x>.
  *
  *	 <x>_<y>_<z>_f(void) : Returns the constant value <z> after being shifted
  *		 to place it at field <y> of register <x>.  This value can be |'d
  *		 with others to produce a full register value for <x>.
  *
  *	 <x>_<y>_v(uint32_t r) : Returns the value of field <y> from a full register
  *		 <x> value 'r' after being shifted to place its LSB at bit 0.
  *		 This value is suitable for direct comparison with other unshifted
  *		 values appropriate for use in field <y> of register <x>.
  *
  *	 <x>_<y>_<z>_v(void) : Returns the constant value for <z> defined for
  *		 field <y> of register <x>.  This value is suitable for direct
  *		 comparison with unshifted values appropriate for use in field <y>
  *		 of register <x>.
  */

#ifndef HW_HOST1X_UCLASS_HOST1X_H_
#define HW_HOST1X_UCLASS_HOST1X_H_

static inline uint32_t host1x_uclass_incr_syncpt_r(void)
{
	return 0x0;
}
static inline uint32_t host1x_uclass_incr_syncpt_cond_f(uint32_t v)
{
	return (v & 0xff) << 8;
}
static inline uint32_t host1x_uclass_incr_syncpt_cond_op_done_v(void)
{
	return 1;
}
static inline uint32_t host1x_uclass_incr_syncpt_indx_f(uint32_t v)
{
	return (v & 0xff) << 0;
}
static inline uint32_t host1x_uclass_wait_syncpt_r(void)
{
	return 0x8;
}
static inline uint32_t host1x_uclass_wait_syncpt_indx_f(uint32_t v)
{
	return (v & 0xff) << 24;
}
static inline uint32_t host1x_uclass_wait_syncpt_thresh_f(uint32_t v)
{
	return (v & 0xffffff) << 0;
}
static inline uint32_t host1x_uclass_wait_syncpt_base_r(void)
{
	return 0x09;
}
static inline uint32_t host1x_uclass_wait_syncpt_base_indx_f(uint32_t v)
{
	return (v & 0xff) << 24;
}
static inline uint32_t host1x_uclass_wait_syncpt_base_base_indx_f(uint32_t v)
{
	return (v & 0xff) << 16;
}
static inline uint32_t host1x_uclass_wait_syncpt_base_offset_f(uint32_t v)
{
	return (v & 0xffff) << 0;
}
static inline uint32_t host1x_uclass_wait_syncpt_incr_indx_f(uint32_t v)
{
	return (v & 0xff) << 24;
}
static inline uint32_t host1x_uclass_wait_syncpt_incr_r(void)
{
	return 0x0a;
}
static inline uint32_t host1x_uclass_load_syncpt_base_base_indx_f(uint32_t v)
{
	return (v & 0xff) << 24;
}
static inline uint32_t host1x_uclass_load_syncpt_base_value_f(uint32_t v)
{
	return (v & 0xffffff) << 0;
}
static inline uint32_t host1x_uclass_incr_syncpt_base_base_indx_f(uint32_t v)
{
	return (v & 0xff) << 24;
}
static inline uint32_t host1x_uclass_incr_syncpt_base_offset_f(uint32_t v)
{
	return (v & 0xffffff) << 0;
}
static inline uint32_t host1x_uclass_delay_usec_r(void)
{
	return 0x10;
}
static inline uint32_t host1x_uclass_indoff_r(void)
{
	return 0x2d;
}
static inline uint32_t host1x_uclass_indoff_indbe_f(uint32_t v)
{
	return (v & 0xf) << 28;
}
static inline uint32_t host1x_uclass_indoff_autoinc_f(uint32_t v)
{
	return (v & 0x1) << 27;
}
static inline uint32_t host1x_uclass_indoff_indmodid_f(uint32_t v)
{
	return (v & 0xff) << 18;
}
static inline uint32_t host1x_uclass_indoff_indroffset_f(uint32_t v)
{
	return (v & 0xffff) << 2;
}
static inline uint32_t host1x_uclass_indoff_rwn_read_v(void)
{
	return 1;
}
#endif
