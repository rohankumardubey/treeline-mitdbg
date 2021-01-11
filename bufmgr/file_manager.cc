#include "file_manager.h"

#include <cassert>

namespace llsm {

// Creates a file manager according to the options specified in `options`.
FileManager::FileManager(const BufMgrOptions options, std::string db_path)
    : db_path_(std::move(db_path)),
      page_size_(options.page_size),
      pages_per_segment_(options.pages_per_segment) {
  assert(options.num_segments >= 1);
  assert(options.pages_per_segment >= 1);

  for (size_t i = 0; i < options.num_segments; ++i) {
    db_files_.push_back(std::make_unique<File>(
        options, db_path_ + "/segment-" + std::to_string(i)));
  }
}

// Reads the part of the on-disk database file corresponding to `page_id` into
// the in-memory page-sized block pointed to by `data`.
void FileManager::ReadPage(const uint64_t page_id, void* data) {
  const FileAddress address = PageIdToAddress(page_id);
  const auto& file = db_files_[address.file_id];
  file->ZeroOut(address.offset);
  file->ReadPage(address.offset, data);
}

// Writes from the in-memory page-sized block pointed to by `data` to the part
// of the on-disk database file corresponding to `page_id`.
void FileManager::WritePage(const uint64_t page_id, void* data) {
  const FileAddress address = PageIdToAddress(page_id);
  const auto& file = db_files_[address.file_id];
  file->ZeroOut(address.offset);
  file->WritePage(address.offset, data);
}

// Uses the model to derive a FileAddress given a `page_id`.
FileAddress FileManager::PageIdToAddress(const size_t page_id) const {
  FileAddress address;
  address.file_id = page_id / pages_per_segment_;
  address.offset = (page_id % pages_per_segment_) * page_size_;
  return address;
}

}  // namespace llsm