#include "pch.h"
#include "Lock.h"

CLock::CLock(SRWLOCK* srw, char lockType)
{
	lock = srw;
	type = lockType;

	if (type == 0) {
		AcquireSRWLockShared(lock);
	}
	else {
		AcquireSRWLockExclusive(lock);
	}
}

CLock::~CLock()
{
	if (type == 0) {
		ReleaseSRWLockShared(lock);
	}
	else {
		ReleaseSRWLockExclusive(lock);
	}
}
