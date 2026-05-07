#include "util/lightweight_mutex.h"

#include <windows.h>

static_assert(sizeof(SRWLOCK) == sizeof(LightweightMutex));
static_assert(alignof(SRWLOCK) <= alignof(LightweightMutex));

LightweightMutexLock::LightweightMutexLock(LightweightMutex& mutex) : mutex_(mutex.storage_) {
    AcquireSRWLockExclusive(static_cast<SRWLOCK*>(mutex_));
}

LightweightMutexLock::~LightweightMutexLock() {
    ReleaseSRWLockExclusive(static_cast<SRWLOCK*>(mutex_));
}
