//------------------------------------------------------------------------------
//	Copyright (c) 2001-2002, OpenBeOS
//
//	Permission is hereby granted, free of charge, to any person obtaining a
//	copy of this software and associated documentation files (the "Software"),
//	to deal in the Software without restriction, including without limitation
//	the rights to use, copy, modify, merge, publish, distribute, sublicense,
//	and/or sell copies of the Software, and to permit persons to whom the
//	Software is furnished to do so, subject to the following conditions:
//
//	The above copyright notice and this permission notice shall be included in
//	all copies or substantial portions of the Software.
//
//	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//	FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//	DEALINGS IN THE SOFTWARE.
//
//	File Name:		session.h
//	Author:			Ingo Weinhold (bonefish@users.sf.net)
//	Description:	hook function declarations for disk_scanner session modules
//------------------------------------------------------------------------------

/*!
	\file session.h
	hook function declarations for disk_scanner session modules
*/

#ifndef _PARTSCAN_SESSION_H
#define _PARTSCAN_SESSION_H

#include <module.h>

struct session_info;

typedef bool (*session_identify_hook)(int deviceFD, off_t deviceSize,
	int32 blockSize);
typedef status_t (*session_get_nth_info_hook)(int deviceFD, int32 index,
	off_t deviceSize, int32 blockSize, struct session_info *sessionInfo);

typedef struct session_module_info {
	module_info					module;

	session_identify_hook		identify;
	session_get_nth_info_hook	get_nth_info;
} session_module_info;

/*
	identify():
	----------

	Checks whether the module knows about sessions on the given device.
	Returns true, if it does, false otherwise.

	params:
	deviceFD: a device FD
	deviceSize: size of the device in bytes
	blockSize: the logical block size


	get_nth_info():
	--------------

	Fills in all fields of sessionInfo with information about
	the indexth session on the specified device.

	params:
	deviceFD: a device FD
	index: the session index
	deviceSize: size of the device in bytes
	blockSize: the logical block size
	sessionInfo: the session info

	The functions is only called, when a call to identify() returned
	true.

	Returns B_OK, if successful, B_ENTRY_NOT_FOUND, if the index is out of
	range.
*/

#endif	// _PARTSCAN_SESSION_H
