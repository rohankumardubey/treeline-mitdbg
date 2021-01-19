#pragma once

#include "llsm/slice.h"
#include "llsm/status.h"
#include "util/arena.h"
#include "util/inlineskiplist.h"

namespace llsm {

// An ordered in-memory table storing "recent" writes and deletes.
//
// This `MemTable` explicitly stores deletes, distinguishing them from regular
// writes using `EntryType::kDelete`. This is important because the key may
// still exist on disk, and we need to know that it should be deleted when this
// `MemTable` is flushed to disk.
//
// Data is stored in a custom memory-managed arena. Once this `MemTable` is done
// being used, it should be deleted to free its allocated memory.
//
// External mutual exclusion is required if calls to `Add()`, `Put()`,
// `Delete()`, `Get()`, and `GetIterator()` occur concurrently. If only the
// `const` methods are called concurrently, no mutual exclusion is needed.
class MemTable {
 public:
  enum class EntryType : uint8_t { kWrite = 0, kDelete = 1 };

  MemTable();

  // Add an entry to this table. The `EntryType` is used to disambiguate between
  // regular writes and deletes. When deleting, `value` is ignored.
  Status Add(const Slice& key, const Slice& value, EntryType entry_type);

  // Record a write of `key` with value `value`.
  // This is a convenience method that calls `Add()` with `EntryType::kWrite`.
  Status Put(const Slice& key, const Slice& value);

  // Record a delete of `key`.
  // This is a convenience method that calls `Add()` with `EntryType::kDelete`.
  Status Delete(const Slice& key);

  // Retrieve the entry associated with `key`.
  //
  // If an entry is found, this method will return an OK status;
  // `entry_type_out` will contain the entry type (write or delete) and
  // `value_out` will contain the value if the entry is a write.
  Status Get(const Slice& key, EntryType* entry_type_out,
             std::string* value_out) const;

  class Iterator;
  Iterator GetIterator() const;

  // Returns an estimate of this table's memory usage, in bytes. Note that this
  // estimate includes memory used by the underlying data structure.
  size_t ApproximateMemoryUsage() const;

  // Returns true iff there is at least one entry (added through `Add()`) in
  // this table.
  bool HasEntries() const { return has_entries_; }

 private:
  // Represents a key-value entry in this `MemTable`. Records stored in this
  // `MemTable` are allocated in memory so that their key and value are stored
  // immediately after the end of this struct.
  struct Record {
    static const Record* FromRawBytes(const char* raw_record) {
      return reinterpret_cast<const Record*>(raw_record);
    }
    static Record* FromRawBytes(char* raw_record) {
      return reinterpret_cast<Record*>(raw_record);
    }

    // The key and value are stored contiguously in the bytes immediately
    // following this Record.
    inline const char* key() const {
      return reinterpret_cast<const char*>(this) + sizeof(Record);
    }
    inline const char* value() const { return key() + key_length; }
    inline char* key() {
      return reinterpret_cast<char*>(this) + sizeof(Record);
    }
    inline char* value() { return key() + key_length; }

    // The lengths of the key and value, in bytes.
    uint32_t key_length, value_length;

    // The sequence number is used to de-duplicate `Record`s with the same
    // `key`. If multiple `Record`s share the same key, the one with the largest
    // sequence number is the most recent entry.
    //
    // The most significant 7 bytes store the sequence number (max. 2^56 - 1)
    // and the least significant byte stores the `EntryType`.
    uint64_t sequence_number;
  };

  // A comparison functor used by `InlineSkipList` to establish a total ordering
  // over `Record`s.
  class Comparator {
   public:
    // Expected by the InlineSkipList.
    using DecodedType = const Record*;
    DecodedType decode_key(const char* key) const {
      return Record::FromRawBytes(key);
    }
    // Classical comparison semantics. We return a:
    // - Negative integer if `r1 < r2`
    // - Zero if `r1 == r2`
    // - Positive integer if `r1 > r2`
    // Note that although the `InlineSkipList` expects a `const char*`, `r1` is
    // actually a pointer to a `const Record`.
    int operator()(const char* r1, const Record* r2) const;
    int operator()(const char* r1, const char* r2) const {
      return operator()(r1, decode_key(r2));
    }
  };

  using Table = InlineSkipList<Comparator>;

  // A custom memory-managed arena that stores the `Record`s, keys, and values.
  Arena arena_;
  Table table_;
  uint64_t next_sequence_num_;
  bool has_entries_;
};

// An iterator for the MemTable.
//
// To get an instance, call `GetIterator()` on a `MemTable`. One of the Seek
// methods must be called first before `Next()` can be called.
//
// When `Valid()` returns `true`, the `key()`, `value()`, and `type()` methods
// return the key, value, and entry type associated with the record that the
// iterator currently "points" to.
class MemTable::Iterator {
 public:
  // Returns true iff the iterator is positioned at a valid node.
  bool Valid() const;
  // Returns the key at the current position.
  // REQUIRES: `Valid()`
  Slice key() const;
  // Returns the value at the current position.
  // REQUIRES: `Valid()`
  Slice value() const;
  // Returns the entry type at the current position.
  // REQUIRES: `Valid()`
  MemTable::EntryType type() const;

  // Advances to the next position.
  // REQUIRES: Valid()
  void Next();
  // Advance to the first entry with a key >= target
  void Seek(const Slice& target);
  // Position at the first entry in list.
  // Final state of iterator is `Valid()` iff list is not empty.
  void SeekToFirst();

 private:
  friend class MemTable;
  explicit Iterator(Table::Iterator it) : it_(it) {}

  // A helper method that returns the key stored in an internal record.
  Slice KeyFromRecord(const char* raw_record) const;

  Table::Iterator it_;
};

}  // namespace llsm
