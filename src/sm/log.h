/*
 * (c) Copyright 2011-2013, Hewlett-Packard Development Company, LP
 */

/* -*- mode:C++; c-basic-offset:4 -*-
     Shore-MT -- Multi-threaded port of the SHORE storage manager

                       Copyright (c) 2007-2009
      Data Intensive Applications and Systems Labaratory (DIAS)
               Ecole Polytechnique Federale de Lausanne

                         All Rights Reserved.

   Permission to use, copy, modify and distribute this software and
   its documentation is hereby granted, provided that both the
   copyright notice and this permission notice appear in all copies of
   the software, derivative works or modified versions, and any
   portions thereof, and that both notices appear in supporting
   documentation.

   This code is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. THE AUTHORS
   DISCLAIM ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER
   RESULTING FROM THE USE OF THIS SOFTWARE.
*/

/*<std-header orig-src='shore' incl-file-exclusion='LOG_H'>

 $Id: log.h,v 1.86 2010/08/03 14:24:46 nhall Exp $

SHORE -- Scalable Heterogeneous Object REpository

Copyright (c) 1994-99 Computer Sciences Department, University of
                      Wisconsin -- Madison
All Rights Reserved.

Permission to use, copy, modify and distribute this software and its
documentation is hereby granted, provided that both the copyright
notice and this permission notice appear in all copies of the
software, derivative works or modified versions, and any portions
thereof, and that both notices appear in supporting documentation.

THE AUTHORS AND THE COMPUTER SCIENCES DEPARTMENT OF THE UNIVERSITY
OF WISCONSIN - MADISON ALLOW FREE USE OF THIS SOFTWARE IN ITS
"AS IS" CONDITION, AND THEY DISCLAIM ANY LIABILITY OF ANY KIND
FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.

This software was developed with support by the Advanced Research
Project Agency, ARPA order number 018 (formerly 8230), monitored by
the U.S. Army Research Laboratory under contract DAAB07-91-C-Q518.
Further funding for this work was provided by DARPA through
Rome Research Laboratory Contract No. F30602-97-2-0247.

*/

#ifndef LOG_H
#define LOG_H

#include "w_defines.h"

/*  -- do not edit anything above this line --   </std-header>*/

#undef ACQUIRE

class logrec_t;
class log_buf;
class lpid_t;
class generic_page;
class fixable_page_h;
class PoorMansOldestLsnTracker;
/**
 * \defgroup SSMLOG Logging And Recovery
 * \ingroup SSMXCT
 * \brief Classes for Transactional Logging and Recovery.
 * \details
 * Updates performed by transactions are logged so that
 * the can be rolled back (in the event of a transaction abort)
 * or restored (in the event of a crash).  Both the old and new values
 * of an updated location are logged.  This allows a steal, no-force
 * buffer management policy, which means the buffer manager is free
 * to write dirty pages to disk at any time and yet does not have
 * to write dirty pages for a a transaction to commit.
 *
 * The log is stored in a set of Unix files, all in the same directory,
 * whose path is determined by a run-time option.
 * The maximum size of the log is also determined by a run-time option.
 * The proper value of the log size depends on
 * the expected transaction mix.  More specifically, it depends on the
 * age of the oldest (longest running) transaction in the system and
 * the amount of log space used by all active transactions. Here are
 * some general rules to determine the  amount  of  free  log  space
 * available in the system.
 * \li Log records between the first log
 *   record generated by the oldest active transaction and the most
 *   recent log record generated by any transaction cannot be thrown
 *   away.
 * \li Log records from a transaction are no longer needed
 *   once the transaction has committed or completely aborted and all
 *   updates have made it to disk. Aborting a transaction causes log space
 *   to be used, so space is reserved for aborting each transaction.
 *   Enough log space must be available to commit or abort all active
 *   transactions at all times.
 *
 * \li Only space starting at the beginning of the log can be reused.
 *   This space can be reused if it contains log records only for
 *   transactions meeting the previous rule.
 *
 * \li All storage manager calls that update records require log space twice
 *    the size of the space updated in the record. All calls that create,
 *    append, or truncate records require log space equal to the size
 *    created, inserted, or deleted. Log records generated by these calls
 *    (generally one per call) have an overhead of approximately 50 bytes.
 *
 * \li The amount of log space reserved for aborting a transaction is equal to
 *   the amount of log space generated by the transaction plus a fudge
 *   factor.
 *   (Where btrees are concerned, a structure modification
 *   might be necessary on abort, using more space on abort, or might not be
 *   necessary on abort where it was done during forward processing,
 *   using less space on abort.)
 *
 * \li The transaction assumes responsiblity for reserving space in the
 *   log so that it can abort, should it need to (without leaving an
 *   unrecoverable volume).  The transaction and the log cooperate to
 *   reserve space for the transaction's aborting.
 *
 * \li When insufficient log space is available for a transaction, the
 *   transaction is (may be, depending on the server) aborted.
 *   The storage manager will return an error indication (out of log space)
 *   if it is unable to insert a log record into the log due to
 *   insufficient space.
 *
 * Checkpoints are taken periodically by the storage manager in order to
 * free log space and shorten recovery time.  Checkpoints are "fuzzy"
 * and can do not require the system to pause while they are completing.
 *
 * See the storage manager constructor ss_m::ss_m for more information
 * about handling out-of-logspace conditions.
 */

/**
 * \brief Log manager interface class.
 * \ingroup SSMLOG
 * \details
 * This is is exposed to the rest of the server.
 * A small amount of the implementation is in here, because
 * such part is needed for things like handling the out-of-log-space
 * callbacks.
 * The details are in the log_core (derived) class.
 *
 * A log is created by the server by
 * calling static new_log_m, not with new/constructor.
 * This is in part because there are so many ways for failure and we
 * need to be able to return a w_rc_t.
 */
class log_m : public smlevel_0
{
public:
    /*
     * Constructor required for proper linking. Direct construction of log_m
     * is not allowed because it is an abstract class.
     */
    log_m() {};
    virtual ~log_m() {};

    virtual lsn_t               min_chkpt_rec_lsn() const = 0;

    virtual rc_t                file_was_archived(const char *file) = 0;

    typedef    smlevel_0::partition_number_t partition_number_t;

    /**\brief Do whatever needs to be done before destructor is called, then destruct.
     *\details
     * Shutdown calls the desctructor; the server, after calling shutdown,
     * nulls out its pointer.
     */
    virtual void           shutdown() = 0;

    /**\brief Return name of directory holding log files
     * \details
     * Used by xct_t for error reporting, callback-handling.
     */
    virtual const char * dir_name() const = 0;

    /**\brief  Return the amount of space left in the log.
     * \details
     * Used by xct_impl for error-reporting.
     */
    virtual fileoff_t           space_left() const = 0;
    virtual fileoff_t           space_for_chkpt() const = 0;

    /**\brief Return name of log file for given partition number.
     * \details
     * Used by xct for error-reporting and callback-handling.
     */
    virtual const char * make_log_name(uint32_t n,
                        char*              buf,
                        int                bufsz) = 0;

    /**\brief Infect the log.
     * \details
     * Used by ss_m for testing.
     * When log_corruption is turned on,
     * insertion of a log record will cause the record to be zeroed
     * in such a way to make it look like the end of the log was
     * hit; this should cause a crash and recovery.
     * Corruption is turned off right after the log record is corrupted.
     */
    virtual void                start_log_corruption() = 0;

    /**\brief Return first lsn of a given partition.
     * \details
     * Used by xct_impl.cpp in handling of emergency log flush.
     */
    static lsn_t        first_lsn(uint32_t pnum) { return lsn_t(pnum, 0); }

    /**\brief Return current lsn of the log (for insert purposes)
     * \details
     * Used by xct_impl.cpp in handling of emergency log flush.
     * Used by force_until_lsn all pages after recovery in
     *   ss_m constructor and destructor.
     * Used by restart.
     * Used by crash to flush log to the end.
     */
    virtual lsn_t               curr_lsn()  const = 0;

    virtual lsn_t               getLastMountLSN()  const = 0;
    virtual void               setLastMountLSN(lsn_t)  = 0;

    //virtual bool                squeezed_by(const lsn_t &)  const  = 0;


    virtual lsn_t               durable_lsn() const = 0;
    virtual lsn_t               master_lsn() const = 0;

    // not called from the implementation:
    virtual rc_t                scavenge(const lsn_t &min_rec_lsn,
                               const lsn_t &min_xct_lsn) = 0;
    virtual rc_t                insert(logrec_t &r, lsn_t* ret) = 0;
    virtual rc_t                compensate(const lsn_t& orig_lsn,
                               const lsn_t& undo_lsn) = 0;

    // used by log_i and xct_impl
    virtual rc_t                fetch(lsn_t &lsn, logrec_t* &rec, lsn_t* nxt=NULL, const bool forward = true) = 0;
            // used in implementation also:
    virtual void        release() = 0; // used by log_i
    virtual rc_t        flush(const lsn_t& lsn, bool block=true, bool signal=true, bool *ret_flushed=NULL) = 0;

    virtual fileoff_t           reserve_space(fileoff_t howmuch) = 0;
    virtual void                release_space(fileoff_t howmuch) = 0;
    virtual rc_t                wait_for_space(fileoff_t &amt, int32_t timeout) = 0;
    virtual fileoff_t           consume_chkpt_reservation(fileoff_t howmuch) = 0;
    virtual void                activate_reservations()  = 0;

    virtual void                set_master(const lsn_t& master_lsn,
                            const lsn_t& min_lsn,
                            const lsn_t& min_xct_lsn) = 0;


    // used by bf_m
    lsn_t               global_min_lsn() const {
                          return std::min(master_lsn(), min_chkpt_rec_lsn()); }
    lsn_t               global_min_lsn(lsn_t const &a) const {
                          return std::min(global_min_lsn(), a); }
    // used by implementation
    lsn_t               global_min_lsn(lsn_t const &a, lsn_t const &b) const {
                          return std::min(global_min_lsn(a), b); }

    // flush won't return until target lsn before durable_lsn(), so
    // back off by one byte so we don't depend on other inserts to
    // arrive after us
    // used by bf_m
    rc_t    flush_all(bool block=true) {
                          return flush(curr_lsn().advance(-1), block); }

    virtual PoorMansOldestLsnTracker* get_oldest_lsn_tracker() = 0;

    virtual partition_number_t  partition_num() const = 0;

    /**\brief used by partition */
    virtual fileoff_t limit() const = 0;

private:
    // no copying allowed
    log_m &operator=(log_m const &);
    log_m(log_m const &);
}; // log_m

/**
 * \brief Log-scan iterator
 * \ingroup SSMLOG
 * \details
 * Used in restart to scan the log.
 */
class log_i {
public:
    /// start a scan of the given log a the given log sequence number.
    NORET                        log_i(log_m& l, const lsn_t& lsn, const bool forward = true) ;
    NORET                        ~log_i();

    /// Get the next log record for transaction, put its sequence number in argument \a lsn
    bool                         xct_next(lsn_t& lsn, logrec_t*& r);

    /// Get the return code from the last next() call.
    w_rc_t&                      get_last_rc();
private:
    log_m&                       log;
    lsn_t                        cursor;
    w_rc_t                       last_rc;
    bool                         forward_scan;
}; // log_i

inline NORET
log_i::log_i(log_m& l, const lsn_t& lsn, const bool forward)  // Default: true for forward scan
    : log(l), cursor(lsn), forward_scan(forward)
{ }

inline
log_i::~log_i()
{ last_rc.verify(); }

inline w_rc_t&
log_i::get_last_rc()
{ return last_rc; }

/*<std-footer incl-file-exclusion='LOG_H'>  -- do not edit anything below this line -- */

#endif          /*</std-footer>*/
