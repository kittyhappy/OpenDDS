#ifndef OPENDDS_RTPSRELAYLIB_PARTITION_INDEX_H
#define OPENDDS_RTPSRELAYLIB_PARTITION_INDEX_H

#include "Name.h"
#include "Utility.h"

#include <unordered_map>

namespace RtpsRelay {

struct GuidToParticipantGuid {
  OpenDDS::DCPS::GUID_t operator()(const OpenDDS::DCPS::GUID_t& x) const
  {
    return OpenDDS::DCPS::make_part_guid(x);
  }
};

struct Identity {
  std::string operator()(const std::string& x) const
  {
    return x;
  }
};


template<typename T, typename Transformer>
class TrieNode {
public:
  using NodePtr = std::shared_ptr<TrieNode>;
  using Value = typename T::value_type;

  static size_t insert(NodePtr node, const Name& name, const Value& guid)
  {
    size_t nodes_added = 0;
    for (const auto& atom : name) {
      const auto iter = node->children_.find(atom);
      if (iter == node->children_.end()) {
        NodePtr child = std::make_shared<TrieNode>();
        node->children_[atom] = child;
        node = std::move(child);
        ++nodes_added;
      } else {
        node = iter->second;
      }
    }

    node->guids_.insert(guid);
    return nodes_added;
  }

  static size_t remove(NodePtr node, const Name& name, const Value& guid)
  {
    return remove(std::move(node), name.begin(), name.end(), guid);
  }

  bool empty() const
  {
    return guids_.empty() && children_.empty();
  }

  static void lookup(NodePtr node, const Name& name, T& guids)
  {
    if (name.is_literal()) {
      lookup_literal(std::move(node), name.begin(), name.end(), false, guids);
    } else {
      lookup_pattern(std::move(node), name.begin(), name.end(), guids);
    }
  }

private:
  using ChildrenType = std::unordered_map<Atom, NodePtr, AtomHash>;
  ChildrenType children_;
  T guids_;

  static void insert_guids(NodePtr node, T& guids)
  {
    std::transform(node->guids_.begin(), node->guids_.end(), std::inserter(guids, guids.begin()), Transformer());
  }

  static void lookup_literal(NodePtr node,
                             Name::const_iterator begin,
                             Name::const_iterator end,
                             bool glob_only,
                             T& guids)
  {
    if (begin == end) {
      insert_guids(node, guids);
      lookup_globs(node, guids);
      return;
    }

    const auto& atom = *begin;

    for (const auto& pos : node->children_) {
      switch (pos.first.kind()) {
      case Atom::CHARACTER:
        if (!glob_only && pos.first == atom) {
          lookup_literal(pos.second, std::next(begin), end, false, guids);
        }
        break;
      case Atom::CHARACTER_CLASS:
        if (!glob_only && pos.first.characters().count(atom.character()) != 0) {
          lookup_literal(pos.second, std::next(begin), end, false, guids);
        }
        break;
      case Atom::NEGATED_CHARACTER_CLASS:
        if (!glob_only && pos.first.characters().count(atom.character()) == 0) {
          lookup_literal(pos.second, std::next(begin), end, false, guids);
        }
        break;
      case Atom::WILDCARD:
        if (!glob_only) {
          lookup_literal(pos.second, std::next(begin), end, false, guids);
        }
        break;
      case Atom::GLOB:
        // Glob consumes character and remains.
        lookup_literal(node, std::next(begin), end, true, guids);
        // Glob matches no characters.
        lookup_literal(pos.second, begin, end, false, guids);
        break;
      }
    }
  }

  static void lookup_globs(NodePtr node, T& guids)
  {
    for (const auto& pos : node->children_) {
      if (pos.first.kind() == Atom::GLOB) {
        insert_guids(pos.second, guids);
        lookup_globs(pos.second, guids);
      }
    }
  }

  static void lookup_pattern(NodePtr node,
                             Name::const_iterator begin,
                             Name::const_iterator end,
                             T& guids)
  {
    if (begin == end) {
      insert_guids(node, guids);
      return;
    }

    const auto& atom = *begin;

    switch (atom.kind()) {
    case Atom::CHARACTER:
      {
        const auto pos = node->children_.find(atom);
        if (pos != node->children_.end()) {
          lookup_pattern(pos->second, std::next(begin), end, guids);
        }
      }
      break;
    case Atom::CHARACTER_CLASS:
      for (const auto& p : node->children_) {
        if (p.first.kind() == Atom::CHARACTER && atom.characters().count(p.first.character()) != 0) {
          lookup_pattern(p.second, std::next(begin), end, guids);
        }
      }
      break;
    case Atom::NEGATED_CHARACTER_CLASS:
      for (const auto& p : node->children_) {
        if (p.first.kind() == Atom::CHARACTER && atom.characters().count(p.first.character()) == 0) {
          lookup_pattern(p.second, std::next(begin), end, guids);
        }
      }
      break;
    case Atom::WILDCARD:
      for (const auto& p : node->children_) {
        if (p.first.kind() == Atom::CHARACTER) {
          lookup_pattern(p.second, std::next(begin), end, guids);
        }
      }
      break;
    case Atom::GLOB:
      // Glob consumes character and remains.
      for (const auto& p : node->children_) {
        if (p.first.kind() == Atom::CHARACTER) {
          lookup_pattern(p.second, begin, end, guids);
        }
      }
      // Glob matches no characters.
      lookup_pattern(std::move(node), std::next(begin), end, guids);
      break;
    }
  }

  static size_t remove(NodePtr node,
                       Name::const_iterator begin,
                       Name::const_iterator end,
                       const Value& guid)
  {
    if (begin == end) {
      node->guids_.erase(guid);
      return 0;
    }

    size_t nodes_removed = 0;
    const auto& atom = *begin;
    const auto pos = node->children_.find(atom);
    if (pos != node->children_.end()) {
      nodes_removed += remove(pos->second, std::next(begin), end, guid);
      if (pos->second->empty()) {
        node->children_.erase(pos);
        ++nodes_removed;
      }
    }
    return nodes_removed;
  }
};

template <typename T, typename Transformer>
class PartitionIndex {
public:
  using TrieNodeT = TrieNode<T, Transformer>;
  using Value = typename T::value_type;

  PartitionIndex()
    : root_(std::make_shared<TrieNodeT>())
    , size_(1u)
  {}

  size_t cache_size() const { return cache_.size(); }
  size_t size() const { return size_; }

  void insert(const std::string& name, const Value& guid)
  {
    size_ += TrieNodeT::insert(root_, Name(name), guid);
    cache_.clear();
  }

  void remove(const std::string& name, const Value& guid)
  {
    size_ -= TrieNodeT::remove(root_, Name(name), guid);
    cache_.clear();
  }

  /// If 'allowed' is not nullptr, entries inserted into 'guids' must be in 'allowed'
  void lookup(const std::string& name, T& guids, const T* allowed = nullptr) const
  {
    LimitedInserter inserter(guids, allowed);
    const auto pos = cache_.find(name);
    if (pos != cache_.end()) {
      inserter.insert(pos->second.begin(), pos->second.end());
      return;
    }
    T& cache_temp = cache_[name];
    TrieNodeT::lookup(root_, Name(name), cache_temp);
    inserter.insert(cache_temp.begin(), cache_temp.end());
  }

  class LimitedInserter {
  public:
    LimitedInserter(T& output, const T* limits)
      : output_(output)
      , limits_(limits)
    {}

    void insert(const typename T::const_iterator& begin, const typename T::const_iterator& end)
    {
      if (limits_) {
        for (auto iter = begin; iter != end; ++iter) {
          if (limits_->count(*iter)) {
            output_.insert(*iter);
          }
        }
      } else {
        output_.insert(begin, end);
      }
    }

  private:
    T& output_;
    const T* limits_;
  };

private:
  typename TrieNodeT::NodePtr root_;
  size_t size_;
  using Cache = std::unordered_map<std::string, T>;
  mutable Cache cache_;
};

}

#endif // RTPSRELAY_PARTITION_INDEX_H_
