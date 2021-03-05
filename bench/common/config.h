#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "gflags/gflags.h"
#include "llsm/options.h"
#include "rocksdb/options.h"

// This header declares all the common configuration flags used across the LLSM
// benchmarks as well as a few utility functions that use these flags.

// Which database(s) to use in the benchmark {all, rocksdb, llsm}.
DECLARE_string(db);

// The path where the database(s) should be stored.
DECLARE_string(db_path);

// The number of times to repeat the experiment.
DECLARE_uint32(trials);

// The seed any pseudorandom number generator should use (to ensure
// reproducibility).
DECLARE_uint32(seed);

// The size of the records in the benchmark dataset, in bytes.
DECLARE_uint32(record_size_bytes);

// The size of the database's in-memory cache, in MiB.
// For LLSM, this is the size of its buffer pool.
// For RocksDB, this is the size of its block cache.
DECLARE_uint64(cache_size_mib);

// The number background threads that the database can use.
DECLARE_uint32(bg_threads);

// Whether or not to use direct I/O.
DECLARE_bool(use_direct_io);

// The size of the memtable before it should be flushed, in MiB.
DECLARE_uint64(memtable_size_mib);

// How full each LLSM page should be, as a value between 1 and 100
// inclusive.
DECLARE_uint32(llsm_page_fill_pct);

// The minimum number of operations to a given page that need to be encoutered
// while flushing a memtable in order to trigger a flush.
DECLARE_uint64(io_threshold);

// The maximum number of times that a given operation can be deferred to a
// future flush.
DECLARE_uint64(max_deferrals);

// If true, all writes will bypass the write-ahead log.
DECLARE_bool(bypass_wal);

namespace llsm {
namespace bench {

// An enum that represents the `db` flag above.
enum class DBType : uint32_t { kAll = 0, kLLSM = 1, kRocksDB = 2 };

// Returns the `DBType` enum value associated with a given string.
// - "all" maps to `kAll`
// - "llsm" maps to `kLLSM`
// - "rocksdb" maps to `kRocksDB`
// All other strings map to an empty `std::optional`.
std::optional<DBType> ParseDBType(const std::string& candidate);

// Returns options that can be used to start RocksDB with the configuration
// specified by the flags set above.
rocksdb::Options BuildRocksDBOptions();

// Returns options that can be used to start LLSM with the configuration
// specified by the flags set above.
llsm::Options BuildLLSMOptions();

}  // namespace bench
}  // namespace llsm