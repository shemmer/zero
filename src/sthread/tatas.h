#ifndef TATAS_H
#define TATAS_H

#include "Lintel/AtomicCounter.hpp"
#include "os_interface.h"

/**\brief A test-and-test-and-set spinlock. 
 *
 * This lock is good for short, uncontended critical sections. 
 * If contention is high, use an mcs_lock. 
 * Long critical sections should use pthread_mutex_t.
 *
 * Tradeoffs are:
 *  - test-and-test-and-set locks: low-overhead but not scalable
 *  - queue-based locks: higher overhead but scalable
 *  - pthread mutexes : very high overhead and blocks, but frees up 
 *  cpu for other threads when number of cpus is fewer than number of threads
 *
 *  \sa REFSYNC
 */
struct tatas_lock {
    /**\cond skip */
    enum { NOBODY=0 };
    typedef union  {
        pthread_t         handle;
#if SIZEOF_PTHREAD_T==8
        uint64_t           bits;
#elif SIZEOF_PTHREAD_T==0
#error  Configuration could not determine size of pthread_t. Fix configure.ac.
#else 
#error  Configuration determined size of pthread_t is unexpected. Fix sthread.h.
#endif
    } holder_type_t;
    volatile holder_type_t _holder;
    /**\endcond skip */

    tatas_lock() { _holder.bits=NOBODY; }

private:
    // CC mangles this as __1cKtatas_lockEspin6M_v_
    /// spin until lock is free
    void spin() { while(*&(_holder.handle)) ; }

public:
    /// Try to acquire the lock immediately.
    bool try_lock() 
    {
        holder_type_t tid = { pthread_self() };
        bool success = false;
	uint64_t old_holder = NOBODY;
	if(lintel::unsafe::atomic_compare_exchange_strong(const_cast<uint64_t*>(&_holder.bits), &old_holder, tid.bits)) {
            lintel::atomic_thread_fence(lintel::memory_order_acquire);
            success = true;
	}
        return success;
    }

    /// Acquire the lock, spinning as long as necessary. 
    void acquire() {
        w_assert1(!is_mine());
        holder_type_t tid = { pthread_self() };
	uint64_t old_holder = NOBODY;
        do {
            spin();
        }
	while(!lintel::unsafe::atomic_compare_exchange_strong(const_cast<uint64_t*>(&_holder.bits), &old_holder, tid.bits));
        lintel::atomic_thread_fence(lintel::memory_order_acquire);
        w_assert1(is_mine());
    }

    /// Release the lock
    void release() {
        lintel::atomic_thread_fence(lintel::memory_order_release);
        w_assert1(is_mine()); // moved after the fence
        _holder.bits= NOBODY;
#if W_DEBUG_LEVEL > 0
        {
            lintel::atomic_thread_fence(lintel::memory_order_acquire); // needed for the assert?
            w_assert1(!is_mine());
        }
#endif
    }

    /// True if this thread is the lock holder
    bool is_mine() const { return 
        pthread_equal(_holder.handle, pthread_self()) ? true : false; }
#undef CASFUNC 
};
/** Scoped objects to automatically acquire tatas_lock. */
class tataslock_critical_section {
public:
    tataslock_critical_section(tatas_lock *lock) : _lock(lock) {
        _lock->acquire();
    }
    ~tataslock_critical_section() {
        _lock->release();
    }
private:
    tatas_lock *_lock;
};

#endif // TATAS_H