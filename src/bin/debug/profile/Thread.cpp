/*
 * Copyright 2008, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */

#include "Thread.h"

#include <algorithm>
#include <new>

#include <debug_support.h>

#include "debug_utils.h"

#include "Options.h"
#include "Team.h"


// #pragma mark - ThreadImage


ThreadImage::ThreadImage(Image* image)
	:
	fImage(image),
	fTotalHits(0)
{
	fImage->AddReference();
}


ThreadImage::~ThreadImage()
{
	fImage->RemoveReference();
}


status_t
ThreadImage::Init()
{
	return B_OK;
}


// #pragma mark - Thread


Thread::Thread(const thread_info& info, Team* team)
	:
	fInfo(info),
	fTeam(team),
	fSampleArea(-1),
	fSamples(NULL),
	fProfileResult(NULL)
{
}


Thread::~Thread()
{
	if (fSampleArea >= 0)
		delete_area(fSampleArea);

	delete fProfileResult;
}


void
Thread::SetProfileResult(ThreadProfileResult* result)
{
	delete fProfileResult;
	fProfileResult = result;
}


void
Thread::UpdateInfo()
{
	thread_info info;
	if (get_thread_info(ID(), &info) == B_OK)
		fInfo = info;
}


void
Thread::SetSampleArea(area_id area, addr_t* samples)
{
	fSampleArea = area;
	fSamples = samples;
}


void
Thread::SetInterval(bigtime_t interval)
{
	fProfileResult->SetInterval(interval);
}


status_t
Thread::AddImage(Image* image)
{
	return fProfileResult->AddImage(image);
}


void
Thread::AddSamples(int32 count, int32 dropped, int32 stackDepth,
	bool variableStackDepth, int32 event)
{
	fProfileResult->SynchronizeImages(event);

	if (variableStackDepth) {
		addr_t* samples = fSamples;

		while (count > 0) {
			addr_t sampleCount = *(samples++);

			if (sampleCount >= B_DEBUG_PROFILE_EVENT_BASE) {
				int32 eventParameterCount
					= sampleCount & B_DEBUG_PROFILE_EVENT_PARAMETER_MASK;
				if (sampleCount == B_DEBUG_PROFILE_IMAGE_EVENT) {
					fProfileResult->SynchronizeImages((int32)samples[0]);
				} else {
					fprintf(stderr, "unknown profile event: %#lx\n",
						sampleCount);
				}

				samples += eventParameterCount;
				count -= eventParameterCount + 1;
				continue;
			}

			fProfileResult->AddSamples(samples, sampleCount);

			samples += sampleCount;
			count -= sampleCount + 1;
		}
	} else {
		count = count / stackDepth * stackDepth;

		for (int32 i = 0; i < count; i += stackDepth)
			fProfileResult->AddSamples(fSamples + i, stackDepth);
	}

	fProfileResult->AddDroppedTicks(dropped);
}


void
Thread::PrintResults() const
{
	fProfileResult->PrintResults();
}


// #pragma mark - ThreadProfileResult


ThreadProfileResult::ThreadProfileResult()
	:
	fThread(NULL),
	fInterval(1)
{
}


ThreadProfileResult::~ThreadProfileResult()
{
}


status_t
ThreadProfileResult::Init(Thread* thread)
{
	fThread = thread;
	return B_OK;
}


void
ThreadProfileResult::SetInterval(bigtime_t interval)
{
	fInterval = interval;
}
