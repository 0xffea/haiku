/*
 * Copyright 2007, Hugo Santos. All Rights Reserved.
 * Copyright 2007, Axel Dörfler, axeld@pinc-software.de. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */


#include <sys/bus.h>

#include "if_xlreg.h"


HAIKU_FBSD_DRIVER_GLUE(3com, xl, pci);

extern driver_t *DRIVER_MODULE_NAME(bmtphy, miibus);
extern driver_t *DRIVER_MODULE_NAME(xlphy, miibus);


driver_t *
__haiku_select_miibus_driver(device_t dev)
{
	driver_t *drivers[] = {
		DRIVER_MODULE_NAME(bmtphy, miibus),
		DRIVER_MODULE_NAME(xlphy, miibus)
	};

	return __haiku_probe_miibus(dev, drivers, 2);
}


int
__haiku_disable_interrupts(device_t dev)
{
	struct xl_softc *sc = device_get_softc(dev);
	u_int16_t status = CSR_READ_2(sc, XL_STATUS);

	if (status == 0xffff || (status & XL_INTRS) == 0)
		return 0;

	atomic_or((int32 *)&sc->xl_intr_status, status);
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_INTR_ACK | (status & XL_INTRS));
	return 1;
}


void
__haiku_reenable_interrupts(device_t dev)
{
}


HAIKU_DRIVER_REQUIREMENTS(FBSD_TASKQUEUES | FBSD_SWI_TASKQUEUE);
