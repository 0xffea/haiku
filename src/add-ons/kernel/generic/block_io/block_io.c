/*
 * Copyright 2002/03, Thomas Kurschel. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*
	Part of Open block device manager

	Actual I/O.

	Main file.
*/


#include "block_io_private.h"
#include <stdio.h>


//#define TRACE_BLOCK_IO
#ifdef TRACE_BLOCK_IO
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


uint block_io_buffer_size;
sem_id block_io_buffer_lock;
struct iovec block_io_buffer_vec[1];
void *block_io_buffer_phys;
char *block_io_buffer;
phys_vecs block_io_buffer_phys_vec;
area_id block_io_buffer_area;

locked_pool_interface *locked_pool;
device_manager_info *pnp;


static status_t
block_io_open(block_io_device_info *device, uint32 flags, 
	block_io_handle_info **res_handle)
{
	block_io_handle_info *handle;
	status_t res;

	TRACE(("block_io_open()\n"));

	handle = (block_io_handle_info *)malloc(sizeof(*handle));

	if (handle == NULL)
		return B_NO_MEMORY;

	handle->device = device;

	res = device->interface->open(device->cookie, &handle->cookie);
	if (res < B_OK)
		goto err;

	*res_handle = handle;

	TRACE((" opened.\n"));

	return B_OK;

err:
	free(handle);
	return res;
}


static status_t
block_io_close(block_io_handle_info *handle)
{
	block_io_device_info *device = handle->device;

	TRACE(("block_io_close()\n"));

	device->interface->close(handle->cookie);
	return B_OK;
}


static status_t
block_io_freecookie(block_io_handle_info *handle)
{
	block_io_device_info *device = handle->device;

	TRACE(("block_io_freecookie()\n"));

	device->interface->free(handle->cookie);
	free(handle);

	TRACE(("done.\n"));

	return B_OK;
}

// TODO: this assumes private R5 kernel functions to be present
#if 0
/** Verify a checksum that is part of BIOS drive identification.
 *	returns B_OK on success
 */

static status_t
verify_checksum(block_io_handle_info *handle, uint64 offset, uint32 len, uint32 chksum)
{
	void *buffer;
	uint32 readlen;
	status_t res;

//	SHOW_FLOW( 0, "offset=%lld, len=%ld", offset, len );

	buffer = malloc(len);
	if (buffer == NULL)
		return B_NO_MEMORY;

	readlen = len;

	res = block_io_read(handle, offset, buffer, &readlen);
	if (res < B_OK || readlen < len)
		goto err;

//	SHOW_FLOW0( 0, "check hash sum" );

	if (boot_calculate_hash(buffer, len) != chksum)
		goto err;

//	SHOW_FLOW0( 0, "success" );

	free(buffer);
	return B_OK;

err:
	free(buffer);
	return B_ERROR;
}


/** store BIOS drive id in node's attribute */

static status_t
store_bios_drive_in_node(block_io_device_info *device)
{
	device_attr attribute = {
		B_BLOCK_DEVICE_BIOS_ID, B_UINT8_TYPE, { ui8: 
			device->bios_drive != NULL ? device->bios_drive->bios_id : 0 }
	};

	return pnp->write_attr(device->node, &attribute);
}


/** find BIOS info of drive, if not happened yet.
 *	this must be called whenever someone wants to access BIOS infos about the drive;
 *	there are two reasons to not call this during probe():
 *	 - perhaps nobody is interested in BIOS info
 *	 - we need a working block_io device to handle lower level driver 
 *	   restrictions, so this method can only be safely called once the 
 *	   node has been loaded
 */

static void
find_bios_drive_info(block_io_handle_info *handle)
{
	block_io_device_info *device = handle->device;
	bios_drive *drive = NULL; //, *colliding_drive;
	char name[32];
	uint8 bios_id;

//	SHOW_FLOW( 0, "%p", device );

	// return immediately if BIOS info has already been found	
	if (device->bios_drive != NULL)
		return;

	// check whether BIOS info was found during one of the previous	
	// loads
	if (pnp->get_attr_uint8(device->node, B_BLOCK_DEVICE_BIOS_ID, &bios_id, false) == B_OK) {
		TRACE(("use previous BIOS ID 0x%x\n", bios_id));

		// yes, so find the associated data structure
		if (bios_id != 0) {
			for (drive = bios_drive_info; drive->bios_id != 0; ++drive) {
				if (drive->bios_id == bios_id)
					break;
			}
		} else
			drive = NULL;

		device->bios_drive = drive;
		return;
	}

	sprintf(name, "PnP %p", device->node);

	// do it the hard way: find a BIOS drive with the same checksums
	for (drive = bios_drive_info; drive->bios_id != 0; ++drive) {
		uint32 i;

		// ignore identified BIOS drives
		if (drive->name[0] != 0)
			continue;

		TRACE(("verifying drive 0x%x", drive->bios_id));

		for (i = 0; i < drive->num_chksums; ++i) {
			if (verify_checksum( handle, drive->chksums[i].offset,
					drive->chksums[i].len, drive->chksums[i].chksum) != B_OK)
				break;
		}

		if (i == drive->num_chksums)
			break;
	}

	if (drive->bios_id == 0) {
		TRACE(("this is no BIOS drive\n"));
		// no BIOS drive found
		goto no_bios_drive;
	}

	TRACE(("this is BIOS drive 0x%x\n", drive->bios_id));

	// the R5 boot loader assumes that two drives can be distinguished by
	// - their checksums
	// - their physical layout
	// unfortunately, the "physical layout" is something virtual defined by the
	// BIOS itself, so nobody can verify that;
	// as a result, we may have two drives with same checksums and different
	// geometry - having no opportunity to check the geometry, we cannot
	// distinguish between them.
	// The simple solution is to modify the boot loader to not take geometry
	// into account, but without sources, the boot loader cannot be fixed.
	for (colliding_drive = bios_drive_info; colliding_drive->bios_id != 0; ++colliding_drive) {
		uint32 i;

		if (drive == colliding_drive)
			continue;

		if (drive->num_chksums != colliding_drive->num_chksums)
			continue;

		for (i = 0; i < colliding_drive->num_chksums; ++i) {
			if (colliding_drive->chksums[i].offset != drive->chksums[i].offset
				|| colliding_drive->chksums[i].len != drive->chksums[i].len
				|| colliding_drive->chksums[i].chksum != drive->chksums[i].chksum)
				break;
		}

		if (i < colliding_drive->num_chksums)
			continue;

		dprintf("Cannot distinguish between BIOS drives %x and %x without geometry\n",
			drive->bios_id, colliding_drive->bios_id);

		// this is nasty - we cannot reliable assign BIOS drive number. 
		// if the user has luck, he "only" cannot install a boot manager;
		// but if the boot drive is affected, he cannot even boot.
		goto no_bios_drive;
	}

	TRACE(("store driver \"%s\" in system data\n", name));

	// store name so noone else will test this BIOS drive
	strcpy(drive->name, name);
	device->bios_drive = drive;

	// remember that to avoid testing next time	
	store_bios_drive_in_node(device);
	return;

no_bios_drive:	
	device->bios_drive = NULL;

	// remember that to avoid testing next time
	store_bios_drive_in_node(device);
	return;
}
#endif


static status_t
block_io_ioctl(block_io_handle_info *handle, uint32 op, void *buffer, size_t length)
{
	block_io_device_info *device = handle->device;

#if 0
	if (device->is_bios_drive) {
		switch (op) {
			case B_GET_BIOS_DRIVE_ID:
				find_bios_drive_info(handle);

				if (device->bios_drive == NULL)
					return B_ERROR;

				*(char *)buffer = device->bios_drive->bios_id;
				return B_OK;

			case B_GET_BIOS_GEOMETRY:
			{
				device_geometry *geometry = (device_geometry *)buffer;
				status_t status;

				find_bios_drive_info(handle);
				if (device->bios_drive == NULL)
					return B_ERROR;

				TRACE(("GET_BIOS_GEOMETRY\n"));

				// get real geometry from low level driver		
				status = device->interface->ioctl(handle->cookie, B_GET_GEOMETRY, 
					geometry, sizeof(*geometry));
				if (status != B_OK)
					return status;

				// replace entries with bios info retrieved by boot loader
				geometry->cylinder_count = device->bios_drive->cylinder_count;
				geometry->head_count = device->bios_drive->head_count;
				geometry->sectors_per_track = device->bios_drive->sectors_per_track;
				return B_OK;
			}
		}
	}
#endif

	return device->interface->ioctl(handle->cookie, op, buffer, length);
}


static status_t
block_io_register_device(device_node_handle parent)
{
	TRACE(("block_io_register_device()\n"));

	// ready to register at devfs
	{
		device_attr attrs[] = {
			{ B_DRIVER_MODULE, B_STRING_TYPE, { string: B_BLOCK_IO_MODULE_NAME }},
			{ PNP_DRIVER_CONNECTION, B_STRING_TYPE, { string: "block_io" }},

			{ B_DRIVER_FIXED_CHILD, B_STRING_TYPE, { string: PNP_DEVFS_MODULE_NAME }},
			{ NULL }
		};

		device_node_handle node;

		return pnp->register_device(parent, attrs, NULL, &node);
	}
}


static void
block_io_remove(device_node_handle node, void *cookie)
{
	uint8 bios_id;
	//bios_drive *drive;

	// if this drive has a BIOS ID, remove it from BIOS drive list
	if (pnp->get_attr_uint8(node, B_BLOCK_DEVICE_BIOS_ID, &bios_id, false) != B_OK
		|| bios_id == 0 )
		return;

// ToDo: this assumes private R5 kernel functions to be present
#if 0
	for (drive = bios_drive_info; drive->bios_id != 0; ++drive) {
		if (drive->bios_id == bios_id)
			break;
	}

	if (drive->bios_id != 0) {
		TRACE(("Marking BIOS device 0x%x as being unknown\n", bios_id));
		drive->name[0] = 0;
	}
#endif
}


static status_t
block_io_init_device(device_node_handle node, void *user_cookie, void **cookie)
{
	block_io_device_info *device;
	block_device_params params;
	char *name, *tmp_name;
	uint8 is_bios_drive;
	status_t res;

	TRACE(("block_io_init_device()\n"));

	// extract controller/protocoll restrictions from node
	if (pnp->get_attr_uint32(node, B_BLOCK_DEVICE_DMA_ALIGNMENT, &params.alignment, true) != B_OK)
		params.alignment = 0;
	if (pnp->get_attr_uint32(node, B_BLOCK_DEVICE_MAX_BLOCKS_ITEM, &params.max_blocks, true) != B_OK)
		params.max_blocks = 0xffffffff;
	if (pnp->get_attr_uint32(node, B_BLOCK_DEVICE_DMA_BOUNDARY, &params.dma_boundary, true) != B_OK)
		params.dma_boundary = ~0;
	if (pnp->get_attr_uint32(node, B_BLOCK_DEVICE_MAX_SG_BLOCK_SIZE, &params.max_sg_block_size, true) != B_OK)
		params.max_sg_block_size = 0xffffffff;
	if (pnp->get_attr_uint32(node, B_BLOCK_DEVICE_MAX_SG_BLOCKS, &params.max_sg_blocks, true) != B_OK)
		params.max_sg_blocks = ~0;

	// do some sanity check:
	// (see scsi/bus_mgr.c)
	params.max_sg_block_size &= ~params.alignment;

	if (params.alignment > B_PAGE_SIZE) {
		dprintf("Alignment (0x%lx) must be less then B_PAGE_SIZE\n", params.alignment);
		return B_ERROR;
	}

	if (params.max_sg_block_size < 512) {
		dprintf("Max s/g block size (0x%lx) is too small\n", params.max_sg_block_size);
		return B_ERROR;
	}

	if (params.dma_boundary < B_PAGE_SIZE - 1) {
		dprintf("DMA boundary (0x%lx) must be at least B_PAGE_SIZE\n", params.dma_boundary);
		return B_ERROR;
	}

	if (params.max_blocks < 1 || params.max_sg_blocks < 1) {
		dprintf("Max blocks (%ld) and max s/g blocks (%ld) must be at least 1",
			params.max_blocks, params.max_sg_blocks);
		return B_ERROR;
	}

	// allow "only" up to 512 sg entries 
	// (they consume 4KB and can describe up to 2MB virtual cont. memory!)
	params.max_sg_blocks = min(params.max_sg_blocks, 512);

	if (pnp->get_attr_uint8(node, B_BLOCK_DEVICE_IS_BIOS_DRIVE, &is_bios_drive, true) != B_OK)
		is_bios_drive = false;

	// we don't really care about /dev name, but if it is 
	// missing, we will get a problem
	if (pnp->get_attr_string(node, PNP_DEVFS_FILENAME, &name, true) != B_OK) {
		dprintf("devfs filename is missing.\n");
		return B_ERROR;
	}

	device = (block_io_device_info *)malloc(sizeof(*device));
	if (device == NULL) {
		res = B_NO_MEMORY;
		goto err1;
	}

	memset(device, 0, sizeof(*device));

	device->node = node;

	res = benaphore_init(&device->lock, "block_device_mutex");
	if (res < 0)
		goto err2;

	// construct a identifiable name for S/G pool		
	tmp_name = malloc(strlen(name) + strlen(" sg_lists") + 1);
	if (tmp_name == NULL) {
		res = B_NO_MEMORY;
		goto err3;
	}

	strcpy(tmp_name, name);
	strcat(tmp_name, " sg_lists");

	// create S/G pool with initial size 1
	// (else, we may be on the paging path and have no S/G entries at hand)
	device->phys_vecs_pool = locked_pool->create( 
		params.max_sg_blocks * sizeof(physical_entry),
		sizeof( physical_entry ) - 1,
		0, 16*1024, 32, 1, tmp_name, B_FULL_LOCK | B_CONTIGUOUS,
		NULL, NULL, NULL);

	free(tmp_name);

	if (device->phys_vecs_pool == NULL) {
		res = B_NO_MEMORY;
		goto err3;	
	}

	device->params = params;
	device->is_bios_drive = is_bios_drive != 0;

	res = pnp->init_driver(pnp->get_parent(node), device,
			(driver_module_info **)&device->interface, (void **)&device->cookie);
	if (res != B_OK)
		goto err4;

	free(name);

	TRACE(("done\n"));

	*cookie = device;
	return B_OK;

err4:
	locked_pool->destroy(device->phys_vecs_pool);
err3:
	benaphore_destroy(&device->lock);
err2:
	free(device);
err1:
	free(name);
	pnp->uninit_driver(pnp->get_parent(node));
	return res;
}


static status_t
block_io_uninit_device(block_io_device_info *device)
{
	pnp->uninit_driver(pnp->get_parent(device->node));

	locked_pool->destroy(device->phys_vecs_pool);
	benaphore_destroy(&device->lock);
	free(device);

	return B_OK;
}


static status_t
block_io_init_buffer(void)
{
	physical_entry physicalTable[2];
	status_t res;

	TRACE(("block_io_init_buffer()\n"));

	block_io_buffer_size = 32*1024;

	block_io_buffer_lock = create_sem(1, "block_io_buffer_mutex");
	if (block_io_buffer_lock < 0) {
		res = block_io_buffer_lock;
		goto err1;
	}

	res = block_io_buffer_area = create_area("block_io_buffer", 
		(void **)&block_io_buffer, B_ANY_KERNEL_ADDRESS, 
		block_io_buffer_size, B_FULL_LOCK | B_CONTIGUOUS, B_READ_AREA | B_WRITE_AREA);
	if (res < 0)
		goto err2;

	res = get_memory_map(block_io_buffer, block_io_buffer_size, physicalTable, 2);
	if (res < 0)
		goto err3;

	block_io_buffer_vec[0].iov_base = block_io_buffer;
	block_io_buffer_vec[0].iov_len = block_io_buffer_size;

	block_io_buffer_phys_vec.num = 1;
	block_io_buffer_phys_vec.total_len = block_io_buffer_size;
	block_io_buffer_phys_vec.vec[0] = physicalTable[0];

	return B_OK;

err3:
	delete_area(block_io_buffer_area);
err2:
	delete_sem(block_io_buffer_lock);
err1:
	return res;
}


static status_t
block_io_uninit_buffer(void)
{
	delete_area(block_io_buffer_area);	
	delete_sem(block_io_buffer_lock);

	return B_OK;
}


static status_t
std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			return block_io_init_buffer();
		case B_MODULE_UNINIT:
			block_io_uninit_buffer();
			return B_OK;

		default:
			return B_ERROR;
	}
}


module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info **)&pnp },
	{ LOCKED_POOL_MODULE_NAME, (module_info **)&locked_pool },
	{}
};


pnp_devfs_driver_info block_io_module = {
	{
		{
			B_BLOCK_IO_MODULE_NAME,
			0,

			std_ops
		},

		NULL,	// supports device		
		block_io_register_device,
		block_io_init_device,
		(status_t (*)( void * )) block_io_uninit_device,
		block_io_remove,
		NULL,	// cleanup
		NULL,	// get paths
	},

	(status_t (*)(void *, uint32, void **))block_io_open,
	(status_t (*)(void *))block_io_close,
	(status_t (*)(void *))block_io_freecookie,

	(status_t (*)(void *, uint32, void *, size_t))block_io_ioctl,

	(status_t (*)(void *, off_t, void *, size_t *))block_io_read,
	(status_t (*)(void *, off_t, const void *, size_t *))block_io_write,

	NULL,
	NULL,

	(status_t (*)(void *, off_t, const iovec *, size_t, size_t *))block_io_readv,
	(status_t (*)(void *, off_t, const iovec *, size_t, size_t *))block_io_writev
};


static status_t
std_ops_for_driver(int32 op, ...)
{
	// there is nothing to setup as this is a interface for 
	// drivers which are loaded by _us_
	switch (op) {
		case B_MODULE_INIT:
		case B_MODULE_UNINIT:
			return B_OK;

		default:
			return B_ERROR;
	}
}


block_io_for_driver_interface block_io_for_driver_module = {
	{
		B_BLOCK_IO_FOR_DRIVER_MODULE_NAME,
		0,

		std_ops_for_driver
	},

	block_io_set_media_params,
};

module_info *modules[] = {
	&block_io_module.info.info,
	&block_io_for_driver_module.info,
	NULL
};
