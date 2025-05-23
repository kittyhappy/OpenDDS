/*
 * Distributed under the OpenDDS License.
 * See: http://www.opendds.org/license.html
 */

#ifndef OPENDDS_DCPS_ADDRESSCACHE_H
#define OPENDDS_DCPS_ADDRESSCACHE_H

#include <ace/config-lite.h>
#ifndef ACE_LACKS_PRAGMA_ONCE
#  pragma once
#endif

#include "Definitions.h"
#include "GuidUtils.h"
#include "NetworkAddress.h"
#include "PoolAllocator.h"
#include "RcObject.h"
#include "TimeTypes.h"

OPENDDS_BEGIN_VERSIONED_NAMESPACE_DECL

namespace OpenDDS {
namespace DCPS {

typedef OPENDDS_SET_CMP(GUID_t, GUID_tKeyLessThan) GuidSet;

struct AddressCacheEntry : public RcObject {

  AddressCacheEntry()
    : expires_(MonotonicTimePoint::max_value)
#if defined ACE_HAS_CPP11
    , addrs_hash_(0)
#endif
  {}

  AddressCacheEntry(const NetworkAddressSet& addrs, const MonotonicTimePoint& expires)
    : addrs_(addrs)
    , expires_(expires)
#if defined ACE_HAS_CPP11
    , addrs_hash_(calculate_hash(addrs_))
#endif
  {}

  NetworkAddressSet addrs_;
  MonotonicTimePoint expires_;
#if defined ACE_HAS_CPP11
  size_t addrs_hash_;
#endif
};

struct AddressCacheEntryProxy {
  AddressCacheEntryProxy(RcHandle<AddressCacheEntry> rch) : entry_(rch) {}

  bool operator==(const AddressCacheEntryProxy& rhs) const
  {
#if defined ACE_HAS_CPP11
    return entry_ && rhs.entry_ && entry_->addrs_hash_ == rhs.entry_->addrs_hash_ && entry_->addrs_ == rhs.entry_->addrs_;
#else
    return entry_ && rhs.entry_ && entry_->addrs_ == rhs.entry_->addrs_;
#endif
  }

  bool operator<(const AddressCacheEntryProxy& rhs) const
  {
#if defined ACE_HAS_CPP11
    return (rhs.entry_ && (!entry_ || (entry_->addrs_hash_ < rhs.entry_->addrs_hash_ || (entry_->addrs_hash_ == rhs.entry_->addrs_hash_ && entry_->addrs_ < rhs.entry_->addrs_))));
#else
    return (rhs.entry_ && (!entry_ || (entry_->addrs_ < rhs.entry_->addrs_)));
#endif
  }

  const NetworkAddressSet& addrs() const { return entry_->addrs_; }

#if defined ACE_HAS_CPP11
  size_t hash() const noexcept { return entry_ ? entry_->addrs_hash_ : 0; }
#endif

private:
  RcHandle<AddressCacheEntry> entry_;
};

template <typename Key>
class AddressCache {
public:

  typedef OPENDDS_SET(Key) KeySet;
#if defined ACE_HAS_CPP11
  typedef OPENDDS_UNORDERED_MAP_T(Key, RcHandle<AddressCacheEntry>) MapType;
  typedef OPENDDS_UNORDERED_MAP_T(GUID_t, KeySet) IdMapType;
#else
  typedef OPENDDS_MAP_T(Key, RcHandle<AddressCacheEntry>) MapType;
  typedef OPENDDS_MAP_T(GUID_t, KeySet) IdMapType;
#endif

  AddressCache() {}
  virtual ~AddressCache() {}

  struct ScopedAccess {
    ScopedAccess(AddressCache& cache)
      : guard_(cache.mutex_)
      , rch_()
      , is_new_(false)
#if defined ACE_HAS_CPP11
      , non_const_touch_(false)
#endif
    {
    }

    ScopedAccess(AddressCache& cache, const Key& key, bool block = true, const MonotonicTimePoint& now = MonotonicTimePoint::now())
      : guard_(cache.mutex_, block)
      , rch_()
      , is_new_(false)
#if defined ACE_HAS_CPP11
      , non_const_touch_(false)
#endif
    {
      const typename MapType::iterator pos = cache.map_.find(key);
      if (pos == cache.map_.end()) {
        rch_ = make_rch<AddressCacheEntry>();
        cache.map_[key] = rch_;
        cache.insert_ids(key);
        is_new_ = true;
      } else {
        rch_ = pos->second;
      }

      if (rch_->expires_ < now) {
        rch_->addrs_.clear();
        rch_->expires_ = MonotonicTimePoint::max_value;
        is_new_ = true;
      }
    }

    ~ScopedAccess()
    {
#if defined ACE_HAS_CPP11
      recalculate_hash();
#endif
    }

    AddressCacheEntry& value()
    {
      OPENDDS_ASSERT(rch_);
#if defined ACE_HAS_CPP11
      non_const_touch_ = true;
#endif
      return *rch_;
    }

    const AddressCacheEntry& value() const
    {
      OPENDDS_ASSERT(rch_);
      return *rch_;
    }

#if defined ACE_HAS_CPP11
    void recalculate_hash()
    {
      if (non_const_touch_) {
        rch_->addrs_hash_ = calculate_hash(rch_->addrs_);
        non_const_touch_ = false;
      }
    }
#endif

    ACE_Guard<ACE_Thread_Mutex> guard_;
    RcHandle<AddressCacheEntry> rch_;
    bool is_new_;
#if defined ACE_HAS_CPP11
    bool non_const_touch_;
#endif

  private:
    ScopedAccess();
    ScopedAccess(const ScopedAccess&);
    ScopedAccess& operator=(const ScopedAccess&);
  };

  bool load(const Key& key, NetworkAddressSet& addrs) const
  {
    ACE_Guard<ACE_Thread_Mutex> guard(mutex_);
    const typename MapType::const_iterator pos = map_.find(key);
    if (pos != map_.end()) {
      if (MonotonicTimePoint::now() < pos->second->expires_) {
        const NetworkAddressSet& as = pos->second->addrs_;
        for (NetworkAddressSet::const_iterator it = as.begin(), limit = as.end(); it != limit; ++it) {
          addrs.insert(*it);
        }
        return true;
      }
    }
    return false;
  }

  void store(const Key& key, const NetworkAddressSet& addrs, const MonotonicTimePoint& expires = MonotonicTimePoint::max_value)
  {
    ACE_Guard<ACE_Thread_Mutex> guard(mutex_);
    RcHandle<AddressCacheEntry>& rch = map_[key];
    if (rch) {
      rch->addrs_ = addrs;
      rch->expires_ = expires;
    } else {
      rch = make_rch<AddressCacheEntry>(addrs, expires);
      insert_ids(key);
    }
  }

  bool remove(const Key& key)
  {
    ACE_Guard<ACE_Thread_Mutex> guard(mutex_);
    return remove_i(key);
  }

  void remove_id(const GUID_t& val)
  {
    ACE_Guard<ACE_Thread_Mutex> guard(mutex_);
    const typename IdMapType::iterator pos = id_map_.find(val);
    KeySet keys;
    if (pos != id_map_.end()) {
      keys.swap(pos->second);
      id_map_.erase(pos);
    }

    for (typename KeySet::iterator key = keys.begin(), limit = keys.end(); key != limit; ++key) {
      remove_i(*key);
    }
  }

  bool empty() const
  {
    return map_.empty() && id_map_.empty();
  }

private:

  void insert_ids(const Key& key)
  {
    GuidSet guids;
    key.get_contained_guids(guids);
    for (GuidSet::const_iterator it = guids.begin(), limit = guids.end(); it != limit; ++it) {
      id_map_[*it].insert(key);
    }
  }

  bool remove_i(const Key& key)
  {
    const bool found = map_.erase(key) != 0;

    // Undo insert_ids(key)
    GuidSet guids;
    key.get_contained_guids(guids);
    for (GuidSet::const_iterator guid = guids.begin(), limit = guids.end(); guid != limit; ++guid) {
      const typename IdMapType::iterator other = id_map_.find(*guid);
      if (other != id_map_.end()) {
        other->second.erase(key);
        if (other->second.empty()) {
          id_map_.erase(other);
        }
      }
    }

    return found;
  }

  mutable ACE_Thread_Mutex mutex_;
  MapType map_;
  IdMapType id_map_;
};

} // namespace DCPS
} // namespace OpenDDS

OPENDDS_END_VERSIONED_NAMESPACE_DECL

#endif /* OPENDDS_DCPS_RADDRESSCACHE_H */
