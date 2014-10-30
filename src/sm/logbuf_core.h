/*
 * (c) Copyright 2011-2014, Hewlett-Packard Development Company, LP
 */

#ifndef LOGBUF_CORE_H
#define LOGBUF_CORE_H


// LOG_BUFFER switch
#include "logbuf_common.h"


#include "w_defines.h"
#include "w_base.h"
#include "w_list.h"
#include "tatas.h"
#include "mcs_lock.h"

#include "logbuf_seg.h"
#include "logbuf_hashtable.h"
#include "log_carray.h"

#include "log_core.h"



// M1: standalone log buffer
// M2: integrated log buffer with write
// M3: integrated log buffer with both read and write

class logrec_t;
class partition_t;

const char DEBUG_MSG[]="DEBUG";

// doubly linked list of segments
typedef w_list_t<logbuf_seg, tatas_lock> logbuf_seg_list_t;

// information stored in hints, not used for now
typedef struct hints {
    hints_op op;
    bool locality;
    bool prefetch;
    bool forward;
} hints_t;
    
// the lob buffer class
class logbuf_core : public log_core { // TODO inherit log_m once log_storage class is pulled out of log_core

    // a hacky way to add this "stand-alone" log buffer to the existing log manager
    // TODO: merge the log buffer code into log_core
    friend class log_core; 

    // INTERFACE METHODS BEGIN
public:
    // do whatever needs to be done before destructor is callable
    virtual void            shutdown(); 
    // returns lsn where data were written 
    virtual rc_t            insert(logrec_t &r, lsn_t* l); 
    virtual rc_t            flush(const lsn_t &lsn, bool block=true, bool signal=true, bool *ret_flushed=NULL);
    virtual rc_t            compensate(const lsn_t &orig_lsn, const lsn_t& undo_lsn);
#ifdef LOG_BUFFER
    virtual rc_t            fetch(lsn_t &lsn, logrec_t* &rec, lsn_t* nxt, const bool forward);
    virtual rc_t            fetch(lsn_t &lsn, logrec_t* &rec, lsn_t* nxt, hints_op op);
#else
    virtual rc_t            fetch(lsn_t &lsn, logrec_t* &rec, lsn_t* nxt, const bool forward);
#endif

    // INTERFACE METHODS END
public:
    logbuf_core(
            const char* path,
            long bsize,
            bool reformat,
            uint32_t count = LOGBUF_SEG_COUNT, 
            uint32_t flush_trigger = LOGBUF_FLUSH_TRIGGER, 
            uint32_t block_size = LOGBUF_BLOCK_SIZE, 
            uint32_t seg_size = LOGBUF_SEG_SIZE, 
            uint32_t part_size = LOGBUF_PART_SIZE,
            int avtive_slot_count = ConsolidationArray::DEFAULT_ACTIVE_SLOT_COUNT);

    ~logbuf_core();

    // for test and debug
    // print current buffer state for debugging
    void logbuf_print(const char *string = DEBUG_MSG, int level=3); 
    void logbuf_print_nolock(const char *string = DEBUG_MSG, int level=3);

      
    // fake operations for M1
    void logbuf_prime(lsn_t next);
    int logbuf_fetch(lsn_t lsn); 
    rc_t logbuf_insert(long recsize);
    rc_t logbuf_flush(const lsn_t &to_lsn, bool block = true, bool signal = true, bool
                       *ret_flushed = NULL);
    void logbuf_archive();

    logrec_t *logbuf_fake_logrec(uint32_t recsize); 
    

    // real operations for M2 and M3
    // called by xct
    // all of them are modified from their implementation in log_core
    //rc_t insert(logrec_t &rec, lsn_t* rlsn);
    //rc_t compensate(const lsn_t& orig_lsn, const lsn_t& undo_lsn);
    //rc_t flush(const lsn_t &to_lsn, bool block = true, bool signal = true, bool
                  //*ret_flushed = NULL);
    //rc_t fetch(lsn_t& ll, logrec_t*& rp, lsn_t* nxt, const bool forward);
    //rc_t fetch(lsn_t& ll, logrec_t* &rp, lsn_t* nxt, hints_op op);

    // spacial fetch function for test and debugging only
    int fetch_for_test(lsn_t& ll, logrec_t*& rp);


    // for prime
    // called during startup
    virtual void _prime(int fd, smlevel_0::fileoff_t start, lsn_t next);

    // for insert
    // the four functions below are modified from their implementation in log_core
    virtual void _reserve_buffer_space(CArraySlot *info, long recsize);
    virtual void _acquire_buffer_space(CArraySlot* info, long recsize);
    virtual lsn_t _copy_to_buffer(logrec_t &rec, long pos, long recsize, CArraySlot*
    info);
    virtual bool _update_epochs(CArraySlot* info);

    logbuf_seg *_lookup_for_compensate(lsn_t lsn);

    // for fetch
    w_rc_t _fetch(logrec_t* &rec, lsn_t &lsn, partition_t *p);
    w_rc_t _fetch(logrec_t* &rec, lsn_t &lsn, partition_t *p, hints_op op);

    // helper for backward scan
    w_rc_t _get_lsn_for_backward_scan(lsn_t &lsn, partition_t *p);

    //void shutdown();
    void flush_daemon();
    lsn_t flush_daemon_work(lsn_t old_mark);
    virtual void _flushX(lsn_t start_lsn, uint64_t start, uint64_t end);

    // for log_core to access our private members
    // these variables are originally in log_core but are now moved here
    long start_byte() const { return _start; }
    long end_byte() const { return _end; }
    //long segsize() const { return _segsize; }
    
private:

    // functions that manipulate log buffer structures

    // for fetch, insert a seg to both the list and the hashtable
    void _insert_seg_for_fetch(logbuf_seg *seg);

    // for insertion
    void _insert_seg_to_list_for_insertion(logbuf_seg *seg);
    void _insert_seg_to_hashtable_for_insertion(logbuf_seg *seg);

    // remove a seg from both the list and the hashtable
    void _remove_seg(logbuf_seg *seg);

    // request a new segment for fetch (on misses)
    logbuf_seg *_get_new_seg_for_fetch();

    // request more space from the log buffer for insertion
    void _get_more_space_for_insertion(CArraySlot *info);

    // replacement algorithm
    logbuf_seg *_replacement(); 
    
    // force the flush daemon to flush 
    void force_a_flush();


    static long         _floor(long offset, long block_size) 
    { return (offset/block_size)*block_size; }
    static long         _ceil(long offset, long block_size) 
    { return _floor(offset + block_size - 1, block_size); }

    //private:
    // make the following public for testing...
public:

    uint32_t _max_seg_count;  // max number of segments in the log buffer (N)


    uint32_t _flush_trigger; // max number of segments in the write buffer (M)
                             // (unflushed) before a forced flush is triggered

    uint32_t _block_size;  // log block size

    uint32_t _tail_size; // total size of tail blocks

    uint32_t _actual_segsize;  // in-memory segment size (with tails)

    tatas_lock _logbuf_lock;
    /** @cond */ char    _padding00[CACHELINE_TATAS_PADDING]; /** @endcond */


    uint32_t _seg_count;  // current number of segments in the log buffer           
                          // it keeps increasing unless the seg is freed
                          // protected by _logbuf_lock

    logbuf_seg_list_t *_seg_list;  // doubly-linked list, protected by _logbuf_lock

    tatas_lock _seg_list_lock; // not used as of now
    /** @cond */ char    _padding0[CACHELINE_TATAS_PADDING]; /** @endcond */



    logbuf_hashtable *_hashtable;  // hash table mapping from lsn to logbuf_seg
                               // the hashtable is thread-safe by itself


    /**
     * _to_OPERATION_lsn is the lsn of the log record that the OPERATION is going to start from. 
     * For example, _to_insert_lsn is the next available lsn for any new insertion, and _to_flush_lsn 
     * points to the very first unflushed log record. 
     * _to_OPERATION_seg usually points to segment that contains _to_OPERATION_lsn, but not always. 
     * When _to_OPERATION_lsn is at offset 0 of a new segment (i.e., lsn.lo() % _segsize == 0), 
     * _to_OPERATION_seg still points to the preceding segment, but not the new segment. 
     * This is because the new segment may not have been allocated yet. 
     */

    logbuf_seg *_to_archive_seg;  
    logbuf_seg *_to_insert_seg;  
    logbuf_seg *_to_flush_seg;  

    lsn_t _to_archive_lsn;  
    lsn_t _to_insert_lsn;  // same as _curr_lsn
    lsn_t _to_flush_lsn;  // same as _durable_lsn

    int64_t _free;  // number of bytes usable (allocated and free) for insertion
        
private:

    // performance stats
    uint64_t reads;
    uint64_t hits;
};


#endif // LOGBUF_CORE_H
