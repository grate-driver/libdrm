/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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
 *
*/

#include "CUnit/Basic.h"

#include "amdgpu_test.h"
#include "amdgpu_drm.h"
#include "amdgpu_internal.h"

static amdgpu_device_handle device_handle;
static uint32_t major_version;
static uint32_t minor_version;

static void amdgpu_security_alloc_buf_test(void);
static void amdgpu_security_gfx_submission_test(void);

CU_BOOL suite_security_tests_enable(void)
{
	CU_BOOL enable = CU_TRUE;

	if (amdgpu_device_initialize(drm_amdgpu[0], &major_version,
				     &minor_version, &device_handle))
		return CU_FALSE;

	if (device_handle->info.family_id < AMDGPU_FAMILY_RV) {
		printf("\n\nDon't support TMZ (trust memory zone), security suite disabled\n");
		enable = CU_FALSE;
	}

	if (amdgpu_device_deinitialize(device_handle))
		return CU_FALSE;

	return enable;
}

int suite_security_tests_init(void)
{
	if (amdgpu_device_initialize(drm_amdgpu[0], &major_version,
				     &minor_version, &device_handle))
		return CUE_SINIT_FAILED;

	return CUE_SUCCESS;
}

int suite_security_tests_clean(void)
{
	int r;

	r = amdgpu_device_deinitialize(device_handle);
	if (r)
		return CUE_SCLEAN_FAILED;

	return CUE_SUCCESS;
}


CU_TestInfo security_tests[] = {
	{ "allocate secure buffer test", amdgpu_security_alloc_buf_test },
	{ "graphics secure command submission", amdgpu_security_gfx_submission_test },
	CU_TEST_INFO_NULL,
};

static void amdgpu_security_alloc_buf_test(void)
{
	amdgpu_bo_handle bo;
	amdgpu_va_handle va_handle;
	uint64_t bo_mc;
	int r;

	/* Test secure buffer allocation in VRAM */
	bo = gpu_mem_alloc(device_handle, 4096, 4096,
			   AMDGPU_GEM_DOMAIN_VRAM,
			   AMDGPU_GEM_CREATE_ENCRYPTED,
			   &bo_mc, &va_handle);

	r = gpu_mem_free(bo, va_handle, bo_mc, 4096);
	CU_ASSERT_EQUAL(r, 0);

	/* Test secure buffer allocation in system memory */
	bo = gpu_mem_alloc(device_handle, 4096, 4096,
			   AMDGPU_GEM_DOMAIN_GTT,
			   AMDGPU_GEM_CREATE_ENCRYPTED,
			   &bo_mc, &va_handle);

	r = gpu_mem_free(bo, va_handle, bo_mc, 4096);
	CU_ASSERT_EQUAL(r, 0);

	/* Test secure buffer allocation in invisible VRAM */
	bo = gpu_mem_alloc(device_handle, 4096, 4096,
			   AMDGPU_GEM_DOMAIN_GTT,
			   AMDGPU_GEM_CREATE_ENCRYPTED |
			   AMDGPU_GEM_CREATE_NO_CPU_ACCESS,
			   &bo_mc, &va_handle);

	r = gpu_mem_free(bo, va_handle, bo_mc, 4096);
	CU_ASSERT_EQUAL(r, 0);
}

static void amdgpu_security_gfx_submission_test(void)
{
	amdgpu_command_submission_write_linear_helper_with_secure(device_handle,
								  AMDGPU_HW_IP_GFX,
								  true);
}
