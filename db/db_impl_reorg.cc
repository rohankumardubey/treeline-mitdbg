#include "bufmgr/page_memory_allocator.h"
#include "db/merge_iterators.h"

namespace llsm {

// Notes on concurrent operations during reorganization
//
// Concurrent writers:
//     --  Only FlushWorker() writes to pages, which uses FixOverflowChain() to
//         get an OverflowChain. That call of FixOverflowChain() will serialize
//         with our own call to FixOverflowChain(), since exactly one of the
//         calls will manage to lock the first chain link first.
//     --  If FlushWorker()'s call goes first, we'll block until the flush
//         completes and follow afterwards.
//     --  If our call goes first, FlushWorker()'s call will block until we are
//         done reorganizing and then see that the number of model pages changed
//         (see DBImpl::FixOverflowChain()). At that point it will return
//         nullptr and force FlushWorker() to fall back to ReinsertionWorker() -
//         which is necessary because we're no longer sure that `records` all go
//         to the same page.
//
// Concurrent readers:
//     --  Any readers that already had a non-exclusive lock on some
//         page in the chain will proceed normally, since we have to wait for
//         them to finish in order for us to return from FixOverflowChain().
//     --  Any readers that haven't fixed the first link of the chain yet will
//         block in FixPage() until we are done and then re-consult the model,
//         where they might discover that they need to try again (see
//         DBImpl::Get() step 4).
//
Status DBImpl::ReorganizeOverflowChain(PhysicalPageId page_id,
                                       uint32_t page_fill_pct) {
  OverflowChain chain = nullptr;
  while (chain == nullptr) {
    chain = FixOverflowChain(page_id, /* exclusive = */ true,
                             /* unlock_before_returning = */ false);
  }

  // Avoid accidental extra work if we scheduled the reorganization twice.
  if (chain->size() == 1) {
    buf_mgr_->UnfixPage(*(chain->at(0)), /* is_dirty = */ false);
    return Status::OK();
  }

  KeyDistHints dist;
  dist.record_size = options_.key_hints.record_size;
  dist.page_fill_pct = page_fill_pct;
  size_t records_per_page = dist.records_per_page();

  // 1. First pass to find boundaries and count number of records
  size_t record_count = 0;
  std::vector<std::string> boundary_keys;
  PageMergeIterator pmi(chain);
  boundary_keys.emplace_back(
      chain->at(0)->GetPage().GetLowerBoundary().ToString());

  while (pmi.Valid()) {
    if (record_count % records_per_page == 0 && record_count > 0)
      boundary_keys.emplace_back(pmi.key().ToString());
    ++record_count;
    pmi.Next();
  }

  dist.num_keys = record_count;
  boundary_keys.emplace_back(
      chain->at(0)->GetPage().GetUpperBoundary().ToString());
  // All pages in the chain have the same lower/upper boundary, since
  // they inherit it from the previous chain link upon construction.
  // The lower boundary is the smallest key that could go into this overflow
  // chain, the upper boundary is the smallest key that would go into the *next*
  // page through the model. Together, they define the common prefix of all the
  // keys in this overflow chain.

  // 2. Allocate and initialize in-memory pages
  const size_t old_num_pages = chain->size();
  const size_t new_num_pages = dist.num_pages();
  assert(boundary_keys.size() == new_num_pages + 1);
  PageBuffer page_data = PageMemoryAllocator::Allocate(new_num_pages);

  std::vector<Page> pages;
  for (size_t i = 0; i < new_num_pages; ++i) {
    pages.emplace_back(page_data.get() + i * Page::kSize, boundary_keys.at(i),
                       boundary_keys.at(i + 1));
  }

  // 3. Populate in-memory pages
  size_t temp_record_count = 0;
  PageMergeIterator pmi2(chain);

  while (pmi2.Valid()) {
    pages.at(temp_record_count / records_per_page)
        .Put(pmi2.key(), pmi2.value());
    ++temp_record_count;
    pmi2.Next();
  }

  // 4. Update data and model, adding new pages to chain as required.
  // Do this backwards to ensure correct behavior for stalled reads (i.e. ensure
  // that they will wait for entire reorg to complete).
  for (size_t i = new_num_pages - 1; i < new_num_pages; --i) {
    BufferFrame* frame;
    if (i < old_num_pages) {
      frame = chain->at(i);
    } else {
      PhysicalPageId new_page_id = buf_mgr_->GetFileManager()->AllocatePage();
      frame = &(buf_mgr_->FixPage(new_page_id, /* exclusive = */ true,
                                  /* is_newly_allocated = */ true));
    }

    memcpy(frame->GetData(), page_data.get() + i * Page::kSize, Page::kSize);
    model_->Insert(frame->GetPage().GetLowerBoundary(), frame->GetPageId());
    // No need to remove anything from the model; the lower boundary of the
    // first page will simply be overwritten.

    buf_mgr_->UnfixPage(*frame, /* is_dirty = */ true);
  }

  return Status::OK();
}

}  // namespace llsm