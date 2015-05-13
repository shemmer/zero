/*
 * (c) Copyright 2011-2013, Hewlett-Packard Development Company, LP
 */

#ifndef STNODE_PAGE_H
#define STNODE_PAGE_H

#include <vector>

#include "generic_page.h"
#include "srwlock.h"
#include "w_defines.h"
#include "sm_base.h"

class bf_fixed_m;
class store_operation_param;

/**
 * \brief Persistent structure representing metadata for a store.
 *
 * \details
 * Contains the root page ID of the given store, store flags (e.g.,
 * what kind of logging to use, is the store allocated?), and the
 * store's deleting status (e.g., is the store in the process of being
 * deleted?).
 *
 * These are contained in \ref stnode_page's.
 */
struct stnode_t {
    /// also okay to initialize via memset
    stnode_t() {
        root     = 0;
        flags    = 0;
        deleting = 0;
    }

    /// Root page ID of the store; holds 0 *if* the store is not allocated.
    shpid_t         root;      // +4 -> 4
    /// store flags            (holds a smlevel_0::store_flag_t)
    uint16_t        flags;     // +2 -> 6
    /// store deleting status  (holds a smlevel_0::store_deleting_t)
    uint16_t        deleting;  // +2 -> 8

    bool is_allocated() const  { return flags != smlevel_0::st_unallocated; }
};


/**
 * \brief Store-node page that contains one stnode_t for each
 * (possibly deleted or uncreated) store belonging to a given volume.
 *
 * \details
 * The handle class for this class is stnode_page_h.
 */
class stnode_page : public generic_page_header {
    friend class stnode_page_h;

    /// max # \ref stnode_t's on a single stnode_page; thus, the
    /// maximum number of stores per volume
    static const size_t max = (page_sz - sizeof(generic_page_header)) / sizeof(stnode_t);

    /// stnode[i] is the stnode_t for store # i of this volume
    stnode_t stnode[max];
};
BOOST_STATIC_ASSERT(sizeof(stnode_page) == generic_page_header::page_sz);

/**
 * \brief Handle for a stnode_page.
 */
class stnode_page_h : public generic_page_h {
    stnode_page *page() const { return reinterpret_cast<stnode_page*>(_pp); }

public:
    /// format given page with page-ID pid as an stnode_page page then
    /// return a handle to it.
    stnode_page_h(generic_page* s, const lpid_t& pid);

    /// construct handle from an existing stnode_page page
    stnode_page_h(generic_page* s) : generic_page_h(s) {
    }
    ~stnode_page_h() {}


    /// max # \ref stnode_t's on a single stnode_page; thus, the
    /// maximum number of stores per volume
    static const size_t max = stnode_page::max;

    stnode_t& get(size_t index) {
        // FIXME: it appears we do not ever use the stnode_t for the
        // store with # 0 as we use that number as a special case to
        // indicate stnode_page/alloc_page's.  See comment in
        // stnode_cache_t::get_min_unused_store_ID().  This is
        // demonstrated by the following assert never triggering:
        w_assert1(0 < index);

        w_assert1(index < max);
        return page()->stnode[index];
    }
    const stnode_t& get(size_t index) const {
        // see comment in non-const version of this method
        w_assert1(0 < index);

        w_assert1(index < max);
        return page()->stnode[index];
    }
};



/**
 * \brief Store creation/destroy/query interface.
 *
 * \details
 * This object handles store create/destroy/query requests for one
 * volume.  99.99% of the requests are, of course, querying the root
 * page ID of indexes.  This object does a lightweight synchronization
 * (latch) to protect them from MT accesses.  However, this object
 * doesn't use locks because we don't need them.  If the store is
 * being destroyed, ss_m will check intent locks before calling this
 * object, so we are safe.
 *
 * This object and vol_t replace the "directory" thingies in original
 * Shore-MT with more efficiency and simplicity.
 */
class stnode_cache_t {
public:
    /// special_pages here holds the special pages for volume vid, the
    /// last of which should be the stnode_page for that volume
    stnode_cache_t(vid_t vid, bf_fixed_m* special_pages);

    /**
     * Returns the root page ID of the given store.
     * If that store isn't allocated, returns 0.
     * @param[in] store Store ID.
     */
    shpid_t get_root_pid(snum_t store) const;

    bool is_allocated(snum_t store) const;

    /// Make a copy of the entire stnode_t of the given store.
    void get_stnode(snum_t store, stnode_t &stnode) const;

    /// Returns the first snum_t that can be used for a new store in
    /// this volume or stnode_page_h::max if all available stores of
    /// this volume are already allocated.
    snum_t get_min_unused_store_ID() const;

    /// Returns the snum_t of all allocated stores in the volume.
    std::vector<snum_t> get_all_used_store_ID() const;

    /** Init method is only invoked after volume data is safe on disk, i.e.,
     * after format or restore in case of a media failure,
     */
    void init();


    /**
     *  Fix the given stnode_page and perform the given store
     *  operation *including* logging it.
     *
     *  param type is in sm_io.h.
     *
     *  It contains:
     *   typedef smlevel_0::store_operation_t        store_operation_t;
     *   in sm_base.h
     *   Operations:
     *       t_delete_store, <---- when really deleted after space freed
     *       t_create_store, <---- store is allocated (snum_t is in use)
     *       t_set_deleting, <---- when transaction deletes store (t_deleting_store)
     *       t_set_store_flags,
     *
     *   typedef smlevel_0::store_flag_t             store_flag_t;
     *       in sm_base.h:
     *       logging attribute: regular, tmp, load, insert
     *
     *   typedef smlevel_0::store_deleting_t         store_deleting_t;
     *           t_not_deleting_store = 0,  // must be 0: code assumes it
     *           t_deleting_store,
     *           t_unknown_deleting         // for error handling
     *
     *  If invoked with redo == true, the method does not generate any log
     *  records. This is used for redo operations in restart and restore.
     */
    rc_t  store_operation(store_operation_param op, bool redo = false);


private:
    /// all operations in this object except get_root_pid are protected by this latch
    mutable queue_based_lock_t _spin_lock;

    const vid_t   _vid;                /// The volume number of the volume we are caching
    bf_fixed_m*   _special_pages;      /// The buffer manager holding the volume's special pages
    stnode_page_h _stnode_page;        /// The stnode_page of the volume we are caching
};

class store_operation_param  {
    friend ostream & operator<<(ostream&, const store_operation_param &);

public:
        typedef smlevel_0::store_operation_t        store_operation_t;
        typedef smlevel_0::store_flag_t             store_flag_t;
        typedef smlevel_0::store_deleting_t         store_deleting_t;

private:
        snum_t                _snum;
        uint16_t               _op;
        fill2                 _filler; // for purify
        union {
            struct {
                uint16_t      _value1;
                uint16_t      _value2;
            } values;
            shpid_t page;
        } _u;


    public:
        store_operation_param(snum_t snum, store_operation_t theOp) :
            _snum(snum), _op(theOp)
        {
            w_assert2(_op == smlevel_0::t_delete_store);
            _u.page=0;
        };
        store_operation_param(snum_t snum, store_operation_t theOp,
                              store_flag_t theFlags) :
            _snum(snum), _op(theOp)
        {
            w_assert2(_op == smlevel_0::t_create_store);
            _u.values._value1 = theFlags;
            _u.values._value2 = 0; // unused
        };
        store_operation_param(snum_t snum, store_operation_t theOp,
                              store_deleting_t newValue,
                              store_deleting_t oldValue = smlevel_0::t_unknown_deleting) :
            _snum(snum), _op(theOp)
        {
            w_assert2(_op == smlevel_0::t_set_deleting);
            _u.values._value1 = newValue;
            _u.values._value2 = oldValue;
        };
        store_operation_param(snum_t snum, store_operation_t theOp,
                              store_flag_t newFlags,
                              store_flag_t oldFlags) :
            _snum(snum), _op(theOp)
        {
            w_assert2(_op == smlevel_0::t_set_store_flags);
            _u.values._value1 = newFlags;
            _u.values._value2 = oldFlags;
        };
        store_operation_param(snum_t snum, store_operation_t theOp,
                              shpid_t root) :
            _snum(snum), _op(theOp)
        {
            w_assert2(_op == smlevel_0::t_set_root);
            _u.page=root;
        };


        snum_t snum()  const { return _snum; };
        store_operation_t op()  const { return (store_operation_t)_op; };
        store_flag_t new_store_flags()  const {
            w_assert2(_op == smlevel_0::t_create_store
                      || _op == smlevel_0::t_set_store_flags);
            return (store_flag_t)_u.values._value1;
        };
        store_flag_t old_store_flags()  const {
            w_assert2(_op == smlevel_0::t_set_store_flags);
            return (store_flag_t)_u.values._value2;
        };
        void set_old_store_flags(store_flag_t flag) {
            w_assert2(_op == smlevel_0::t_set_store_flags);
            _u.values._value2 = flag;
        }
        shpid_t root()  const {
            w_assert2(_op == smlevel_0::t_set_root);
            return _u.page;
        };
        store_deleting_t new_deleting_value()  const {
            w_assert2(_op == smlevel_0::t_set_deleting);
            return (store_deleting_t)_u.values._value1;
        };
        store_deleting_t old_deleting_value()  const {
            w_assert2(_op == smlevel_0::t_set_deleting);
            return (store_deleting_t)_u.values._value2;
        };
        void set_old_deleting_value(store_deleting_t old_value) {
            w_assert2(_op == smlevel_0::t_set_deleting);
            _u.values._value2 = old_value;
        }
        int size()  const { return sizeof (*this); };

    private:
        store_operation_param();
};

#endif // STNODE_PAGE_H
