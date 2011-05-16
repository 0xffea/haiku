/*
 * Copyright (c) 2002, Thomas Kurschel
 * Copyright 2004-2011 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Thomas Kurschel
 *		Clemens Zeidler, <haiku@clemens-zeidler.de>
 *		Alexander von Gluck IV, kallisti5@unixzen.com
 */


#include "driver.h"
#include "device.h"
#include "lock.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <AGP.h>
#include <KernelExport.h>
#include <OS.h>
#include <PCI.h>
#include <SupportDefs.h>


#define TRACE_DRIVER
#ifdef TRACE_DRIVER
#	define TRACE(x...) dprintf("radeon_hd: " x)
#else
#	define TRACE(x...) ;
#endif

#define MAX_CARDS 1


// list of supported devices
const struct supported_device {
	uint32		device_id;
	int32		chipset;
	const char*	name;
} kSupportedDevices[] = {
	// R600 series	(HD24xx - HD42xx)
	// Codename: Pele
	//	Radeon 2400		- RV610
	{0x9611, RADEON_R600 | 0x20, "Radeon HD 3100"},	/*RV620, IGP*/
	{0x9613, RADEON_R600 | 0x20, "Radeon HD 3100"},	/*RV620, IGP*/
	{0x9610, RADEON_R600 | 0x20, "Radeon HD 3200"},	/*RV620, IGP*/
	{0x9612, RADEON_R600 | 0x20, "Radeon HD 3200"},	/*RV620, IGP*/
	{0x9615, RADEON_R600 | 0x20, "Radeon HD 3200"},	/*RV620, IGP*/
	//  Radeon 3410		- RV620
	//  Radeon 3430		- RV620
	{0x95c5, RADEON_R600 | 0x20, "Radeon HD 3450"},	/*RV620*/
	//  Radeon 3470		- RV620
	{0x9710, RADEON_R600 | 0x20, "Radeon HD 4200"},	/*RV620, IGP*/
	//	Radeon 4225		- RV620
	//	Radeon 4250		- RV620
	{0x9712, RADEON_R600 | 0x20, "Radeon HD 4270"},	/*RV620, IGP*/
	{0x9589, RADEON_R600 | 0x30, "Radeon HD 2600"},	/*RV630*/
	//	Radeon 2700		- RV630
	//  Radeon 3650		- RV635
	//  Radeon 3670		- RV635
	//  Radeon 3850		- RV670
	//  Radeon 3870		- RV670
	{0x9501, RADEON_R600 | 0x70, "Radeon HD 3870"},	/*RV670, IGP*/
	//  Radeon 3850 x2	- RV680
	//  Radeon 3870 x2	- RV680

	// R700 series	(HD4330 - HD4890, HD51xx, HD5xxV)
	// Codename: Wekiva
	//	Radeon 4330		- RV710
	{0x954f, RADEON_R700 | 0x10, "Radeon HD 4350"},	/*RV710*/
	{0x9555, RADEON_R700 | 0x10, "Radeon HD 4350"},	/*RV710*/
	//	Radeon 4530		- RV710
	{0x9540, RADEON_R700 | 0x10, "Radeon HD 4550"},	/*RV710*/
	//	Radeon 4570		- RV710
	//	Radeon 4650		- RV730
	//	Radeon 4670		- RV730
	{0x94b3, RADEON_R700 | 0x40, "Radeon HD 4770"},	/*RV740*/
	//	Radeon 4830		- RV740
	//	Radeon 4850		- RV770
	//	Radeon 4860		- RV740
	//	Radeon 4870		- RV770
	//	Radeon 4870	x2	- RV770

	// R800 series (HD54xx - HD59xx)
	// Codename: Evergreen
	{0x68f9, RADEON_R800 | 0x0, "Radeon HD 5450"},	/*RV8XX*/
	{0x68e0, RADEON_R800 | 0x0, "Radeon HD 5470"},	/*RV8XX*/
	{0x68da, RADEON_R800 | 0x0, "Radeon HD 5500"},	/*RV8XX*/
	{0x68d9, RADEON_R800 | 0x0, "Radeon HD 5570"},	/*RV8XX*/
	{0x68b9, RADEON_R800 | 0x0, "Radeon HD 5600"},	/*RV8XX*/
	{0x68c1, RADEON_R800 | 0x0, "Radeon HD 5650"},	/*RV8XX*/
	{0x68d8, RADEON_R800 | 0x0, "Radeon HD 5670"},	/*RV8XX*/
	{0x68be, RADEON_R800 | 0x0, "Radeon HD 5700"},	/*RV8XX*/
	{0x68b8, RADEON_R800 | 0x0, "Radeon HD 5770"},	/*RV8XX*/
	{0x6898, RADEON_R800 | 0x0, "Radeon HD 5800"},	/*RV8XX*/
	{0x6899, RADEON_R800 | 0x0, "Radeon HD 5800"},	/*RV8XX*/
	{0x689e, RADEON_R800 | 0x0, "Radeon HD 5800"},	/*RV8XX*/
	{0x689c, RADEON_R800 | 0x0, "Radeon HD 5900"}	/*RV8XX*/
};


int32 api_version = B_CUR_DRIVER_API_VERSION;


char* gDeviceNames[MAX_CARDS + 1];
radeon_info* gDeviceInfo[MAX_CARDS];
pci_module_info* gPCI;
mutex gLock;


static status_t
get_next_radeon_hd(int32 *_cookie, pci_info &info, uint32 &type)
{
	int32 index = *_cookie;

	// find devices

	for (; gPCI->get_nth_pci_info(index, &info) == B_OK; index++) {
		// check vendor
		if (info.vendor_id != VENDOR_ID_ATI
			|| info.class_base != PCI_display
			|| info.class_sub != PCI_vga)
			continue;

		// check device
		for (uint32 i = 0; i < sizeof(kSupportedDevices)
				/ sizeof(kSupportedDevices[0]); i++) {
			if (info.device_id == kSupportedDevices[i].device_id) {
				type = i;
				*_cookie = index + 1;
				return B_OK;
			}
		}
	}

	return B_ENTRY_NOT_FOUND;
}


extern "C" const char **
publish_devices(void)
{
	TRACE("%s\n", __func__);
	return (const char **)gDeviceNames;
}


extern "C" status_t
init_hardware(void)
{
	TRACE("%s\n", __func__);

	status_t status = get_module(B_PCI_MODULE_NAME, (module_info **)&gPCI);
	if (status != B_OK) {
		dprintf(DEVICE_NAME ": ERROR: pci module unavailable\n");
		return status;
	}

	int32 cookie = 0;
	uint32 type;
	pci_info info;
	status = get_next_radeon_hd(&cookie, info, type);

	put_module(B_PCI_MODULE_NAME);
	return status;
}


extern "C" status_t
init_driver(void)
{
	TRACE("%s\n", __func__);

	status_t status = get_module(B_PCI_MODULE_NAME, (module_info**)&gPCI);
	if (status != B_OK) {
		dprintf(DEVICE_NAME ": ERROR: pci module unavailable\n");
		return status;
	}

	mutex_init(&gLock, "radeon hd ksync");

	// find devices

	int32 found = 0;

	for (int32 cookie = 0; found < MAX_CARDS;) {
		pci_info* info = (pci_info*)malloc(sizeof(pci_info));
		if (info == NULL)
			break;

		uint32 type;
		status = get_next_radeon_hd(&cookie, *info, type);
		if (status < B_OK) {
			free(info);
			break;
		}

		// create device names & allocate device info structure

		char name[64];
		sprintf(name, "graphics/radeon_hd_%02x%02x%02x",
			info->bus, info->device,
			info->function);

		gDeviceNames[found] = strdup(name);
		if (gDeviceNames[found] == NULL)
			break;

		gDeviceInfo[found] = (radeon_info*)malloc(sizeof(radeon_info));
		if (gDeviceInfo[found] == NULL) {
			free(gDeviceNames[found]);
			break;
		}

		// initialize the structure for later use

		memset(gDeviceInfo[found], 0, sizeof(radeon_info));
		gDeviceInfo[found]->init_status = B_NO_INIT;
		gDeviceInfo[found]->id = found;
		gDeviceInfo[found]->pci = info;
		gDeviceInfo[found]->registers = (uint8 *)info->u.h0.base_registers[0];
		gDeviceInfo[found]->device_identifier = kSupportedDevices[type].name;
		gDeviceInfo[found]->device_chipset = kSupportedDevices[type].chipset;

		dprintf(DEVICE_NAME ": GPU(%ld) %s, revision = 0x%x\n", found,
			kSupportedDevices[type].name, info->revision);

		found++;
	}

	gDeviceNames[found] = NULL;

	if (found == 0) {
		mutex_destroy(&gLock);
		put_module(B_AGP_GART_MODULE_NAME);
		put_module(B_PCI_MODULE_NAME);
		return ENODEV;
	}

	return B_OK;
}


extern "C" void
uninit_driver(void)
{
	TRACE("%s\n", __func__);

	mutex_destroy(&gLock);

	// free device related structures
	char* name;
	for (int32 index = 0; (name = gDeviceNames[index]) != NULL; index++) {
		free(gDeviceInfo[index]);
		free(name);
	}

	put_module(B_PCI_MODULE_NAME);
}


extern "C" device_hooks*
find_device(const char* name)
{
	int index;

	TRACE("%s\n", __func__);

	for (index = 0; gDeviceNames[index] != NULL; index++) {
		if (!strcmp(name, gDeviceNames[index]))
			return &gDeviceHooks;
	}

	return NULL;
}

