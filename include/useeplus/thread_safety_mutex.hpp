/**
 * @file thread_safety_mutex.hpp
 * @brief Clang Thread Safety Analysis wrappers for standard C++ synchronization primitives.
 * @details This file provides annotated wrappers around `std::mutex` and RAII lock guards.
 * By using these wrappers instead of the raw `std` types, the Clang compiler can statically
 * analyze the codebase at compile time to detect race conditions, deadlocks, and unguarded
 * data access (via the `GUARDED_BY` macros).
 */

#pragma once
#include <mutex>

#include "thread_safety.hpp"

/**
 * @brief A static-analysis-enabled wrapper around `std::mutex`.
 * @details Annotated with Clang's `CAPABILITY` macro. When a class member is marked with
 * `GUARDED_BY(this_mutex)`, the compiler will enforce that this specific Mutex is locked
 * before the member can be read or written.
 */
class CAPABILITY("mutex") Mutex {
   public:
    /**
     * @brief Acquires the mutex, blocking the calling thread until it is available.
     * @details Annotated with `ACQUIRE()` to signal to the compiler that the thread
     * now holds this capability.
     */
    void lock() ACQUIRE() {
        m_.lock();
    }

    /**
     * @brief Releases the mutex, allowing other blocked threads to acquire it.
     * @details Annotated with `RELEASE()` to signal to the compiler that the thread
     * no longer holds this capability.
     */
    void unlock() RELEASE() {
        m_.unlock();
    }

    /**
     * @brief Attempts to acquire the mutex without blocking the calling thread.
     * @return true if the lock was successfully acquired, false if it is currently held by another
     * thread.
     */
    bool try_lock() {
        return m_.try_lock();
    }

   private:
    std::mutex m_;
};

/**
 * @brief An RAII scoped lock guard enabled for static thread safety analysis.
 * @details Acts as a static-analysis drop-in replacement for `std::lock_guard`.
 * Annotated with `SCOPED_CAPABILITY` to tie the lock's lifecycle to the local C++ scope.
 * The compiler mathematically guarantees that the associated Mutex is released when
 * this object is destroyed.
 */
class SCOPED_CAPABILITY MutexLock {
   public:
    /**
     * @brief Acquires the provided mutex and ties its lifecycle to this object.
     * @param mu The Mutex to lock. Annotated with `ACQUIRE(mu)` to bind the capability.
     */
    explicit MutexLock(Mutex& mu) ACQUIRE(mu) : mu_(mu) {
        mu_.lock();
    }

    /**
     * @brief Automatically releases the associated mutex upon scope exit.
     */
    ~MutexLock() RELEASE() {
        mu_.unlock();
    }

    MutexLock(const MutexLock&) = delete;
    MutexLock& operator=(const MutexLock&) = delete;
    MutexLock(MutexLock&&) = delete;
    MutexLock& operator=(MutexLock&&) = delete;

   private:
    Mutex& mu_;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
};