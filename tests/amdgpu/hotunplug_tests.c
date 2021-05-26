/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#if HAVE_ALLOCA_H
# include <alloca.h>
#endif

#include "CUnit/Basic.h"

#include "amdgpu_test.h"
#include "amdgpu_drm.h"
#include "amdgpu_internal.h"
#include "xf86drm.h"
#include <pthread.h>


static  amdgpu_device_handle device_handle;
static  uint32_t  major_version;
static  uint32_t  minor_version;
static char *sysfs_remove = NULL;

CU_BOOL suite_hotunplug_tests_enable(void)
{
	CU_BOOL enable = CU_TRUE;
	drmDevicePtr device;

	if (drmGetDevice2(drm_amdgpu[0], DRM_DEVICE_GET_PCI_REVISION, &device)) {
		printf("\n\nGPU Failed to get DRM device PCI info!\n");
		return CU_FALSE;
	}

	if (device->bustype != DRM_BUS_PCI) {
		printf("\n\nGPU device is not on PCI bus!\n");
		amdgpu_device_deinitialize(device_handle);
		return CU_FALSE;
	}

	/* Disable until the hot-unplug support in kernel gets into drm-next */
	if (major_version < 0xff)
		enable = false;

	if (amdgpu_device_initialize(drm_amdgpu[0], &major_version,
					     &minor_version, &device_handle))
		return CU_FALSE;

	/* TODO Once DRM version for unplug feature ready compare here agains it*/

	if (amdgpu_device_deinitialize(device_handle))
		return CU_FALSE;

	return enable;
}

int suite_hotunplug_tests_init(void)
{
	/* We need to open/close device at each test manually */
	amdgpu_close_devices();

	return CUE_SUCCESS;
}

int suite_hotunplug_tests_clean(void)
{


	return CUE_SUCCESS;
}

static int amdgpu_hotunplug_trigger(const char *pathname)
{
	int fd, len;

	fd = open(pathname, O_WRONLY);
	if (fd < 0)
		return -errno;

	len = write(fd, "1", 1);
	close(fd);

	return len;
}

static int amdgpu_hotunplug_setup_test()
{
	int r;
	char *tmp_str;

	if (amdgpu_open_device_on_test_index(open_render_node) <= 0) {
		printf("\n\n Failed to reopen device file!\n");
		return CUE_SINIT_FAILED;



	}

	r = amdgpu_device_initialize(drm_amdgpu[0], &major_version,
				   &minor_version, &device_handle);

	if (r) {
		if ((r == -EACCES) && (errno == EACCES))
			printf("\n\nError:%s. "
				"Hint:Try to run this test program as root.",
				strerror(errno));
		return CUE_SINIT_FAILED;
	}

	tmp_str = amdgpu_get_device_from_fd(drm_amdgpu[0]);
	if (!tmp_str){
		printf("\n\n Device path not found!\n");
		return  CUE_SINIT_FAILED;
	}

	sysfs_remove = realloc(tmp_str, strlen(tmp_str) * 2);
	strcat(sysfs_remove, "/remove");

	return 0;

}

static int amdgpu_hotunplug_teardown_test()
{
	if (amdgpu_device_deinitialize(device_handle))
		return CUE_SCLEAN_FAILED;

	amdgpu_close_devices();

	if (sysfs_remove)
		free(sysfs_remove);

	return 0;
}

static inline int amdgpu_hotunplug_remove()
{
	return amdgpu_hotunplug_trigger(sysfs_remove);
}

static inline int amdgpu_hotunplug_rescan()
{
	return amdgpu_hotunplug_trigger("/sys/bus/pci/rescan");
}


static void amdgpu_hotunplug_simple(void)
{
	int r;

	r = amdgpu_hotunplug_setup_test();
	CU_ASSERT_EQUAL(r , 0);

	r = amdgpu_hotunplug_remove();
	CU_ASSERT_EQUAL(r > 0, 1);

	r = amdgpu_hotunplug_teardown_test();
	CU_ASSERT_EQUAL(r , 0);

	r = amdgpu_hotunplug_rescan();
	CU_ASSERT_EQUAL(r > 0, 1);
}

CU_TestInfo hotunplug_tests[] = {
	{ "Unplug card and rescan the bus to plug it back", amdgpu_hotunplug_simple },
	CU_TEST_INFO_NULL,
};


