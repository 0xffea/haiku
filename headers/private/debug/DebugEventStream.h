/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef _DEBUG_EVENT_STREAM_H
#define _DEBUG_EVENT_STREAM_H

#include <SupportDefs.h>

#include <system_profiler_defs.h>


class BDataIO;


struct debug_event_stream_header {
	char	signature[32];
	uint32	version;
	uint32	flags;
	uint32	event_mask;
};


// signature and version
#define B_DEBUG_EVENT_STREAM_SIGNATURE	"Haiku debug events"
#define B_DEBUG_EVENT_STREAM_VERSION	1


// flags
enum {
	B_DEBUG_EVENT_STREAM_FLAG_HOST_ENDIAN		= 0x00000001,
	B_DEBUG_EVENT_STREAM_FLAG_SWAPPED_ENDIAN	= 0x01000000,

	B_DEBUG_EVENT_STREAM_FLAG_ZIPPED			= 0x00000002
};


class BDebugEventInputStream {
public:
								BDebugEventInputStream();
								~BDebugEventInputStream();

			status_t			SetTo(BDataIO* stream);
			void				Unset();

			ssize_t				ReadNextEvent(
									const system_profiler_event_header**
										_header);

private:
			ssize_t				_Read(void* buffer, size_t size);
			status_t			_GetData(size_t size);

private:
			BDataIO*			fStream;
			uint32				fFlags;
			uint32				fEventMask;
			uint8*				fBuffer;
			size_t				fBufferCapacity;
			size_t				fBufferSize;
			size_t				fBufferPosition;
};


class BDebugEventOutputStream {
public:
								BDebugEventOutputStream();
								~BDebugEventOutputStream();

			status_t			SetTo(BDataIO* stream, uint32 flags,
									uint32 eventMask);
			void				Unset();

			status_t 			Write(const void* buffer, size_t size);
			status_t			Flush();

private:
			BDataIO*			fStream;
			uint32				fFlags;
};


#endif	// _DEBUG_EVENT_STREAM_H
