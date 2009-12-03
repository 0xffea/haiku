/*
 * Copyright 2009 Colin Günther, coling@gmx.de
 * All Rights Reserved. Distributed under the terms of the MIT License.
 */


extern "C" {
#include <compat/sys/condvar.h>
#include <compat/sys/kernel.h>
}

#include <new>

#include <condition_variable.h>

#include "Condvar.h"
#include "device.h"


#define ticks_to_usecs(t) (1000000*((bigtime_t)t) / hz)


void
conditionInit(struct cv* variable, const char* description)
{
	variable->condition = new(std::nothrow) ConditionVariable();
	variable->condition->Init(variable, description);
}


void
conditionPublish(struct cv* variable, const void* waitChannel, 
	const char* description)
{
	variable->condition = new(std::nothrow) ConditionVariable();
	variable->condition->Publish(waitChannel, description);
}


void
conditionUnpublish(const struct cv* variable)
{
	variable->condition->Unpublish();
	delete variable->condition;
}


int
conditionTimedWait(const struct cv* variable, const int timeout)
{
	status_t status = variable->condition->Wait(B_RELATIVE_TIMEOUT,
		ticks_to_usecs(timeout));

	if (status != B_OK)
		status = EWOULDBLOCK;
	return status;
}


void
conditionWait(const struct cv* variable)
{
	variable->condition->Wait();
}


void
conditionNotifyOne(const struct cv* variable)
{
	variable->condition->NotifyOne();
}


int
publishedConditionTimedWait(const void* waitChannel, const int timeout)
{
	ConditionVariableEntry variableEntry;

	status_t status = variableEntry.Wait(waitChannel, B_RELATIVE_TIMEOUT,
		ticks_to_usecs(timeout));

	if (status != B_OK)
		status = EWOULDBLOCK;
	return status;
}


void
publishedConditionNotifyAll(const void* waitChannel)
{
	ConditionVariable::NotifyAll(waitChannel);
}
