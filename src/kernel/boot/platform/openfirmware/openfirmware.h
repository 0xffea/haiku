/*
** Copyright 2003, Axel D�rfler, axeld@pinc-software.de. All rights reserved.
** Distributed under the terms of the OpenBeOS License.
*/
#ifndef OPEN_FIRMWARE_H
#define OPEN_FIRMWARE_H


#include <SupportDefs.h>


#define OF_FAILED	(-1)

/* global device tree/properties access */
extern int gChosen;


#ifdef __cplusplus
extern "C" {
#endif

extern void of_init(int (*openFirmwareEntry)(void *));

/* device tree functions */
extern int of_finddevice(const char *device);
extern int of_child(int node);
extern int of_peer(int node);
extern int of_parent(int node);
extern int of_instance_to_path(int instance, char *pathBuffer, int bufferSize);
extern int of_instance_to_package(int instance);
extern int of_getprop(int package, const char *property, void *buffer, int bufferSize);
extern int of_setprop(int package, const char *property, const void *buffer, int bufferSize);
extern int of_nextprop(int package, const char *previousProperty, char *nextProperty);
extern int of_getproplen(int package, const char *property);
extern int of_package_to_path(int package, char *pathBuffer, int bufferSize);

/* I/O functions */
extern int of_open(const char *nodeName);
extern void of_close(int handle);
extern int of_read(int handle, void *buffer, int bufferSize);
extern int of_write(int handle, const void *buffer, int bufferSize);
extern int of_seek(int handle, off_t pos);

/* memory functions */
extern int of_release(void *virtualAddress, int size);
extern void *of_claim(void *virtualAddress, int size);

/* misc functions */
extern int of_test(const char *service);
extern int of_milliseconds(void);
extern void of_exit(void);

#ifdef __cplusplus
}
#endif

#endif	/* OPEN_FIRMWARE_H */
