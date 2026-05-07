#pragma once

class LightweightMutex {
public:
    LightweightMutex() = default;

    LightweightMutex(const LightweightMutex&) = delete;
    LightweightMutex& operator=(const LightweightMutex&) = delete;

private:
    friend class LightweightMutexLock;

    // The native static initializer is zero storage; the implementation verifies this buffer matches it.
    alignas(void*) unsigned char storage_[sizeof(void*)]{};
};

class LightweightMutexLock {
public:
    explicit LightweightMutexLock(LightweightMutex& mutex);
    ~LightweightMutexLock();

    LightweightMutexLock(const LightweightMutexLock&) = delete;
    LightweightMutexLock& operator=(const LightweightMutexLock&) = delete;

private:
    void* mutex_ = nullptr;
};
