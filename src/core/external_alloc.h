// Copyright 2022, Roman Gershman.  All rights reserved.
// See LICENSE for licensing terms.
//
#pragma once

#include <absl/container/btree_map.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace dfly {

/**
 *
 * An external allocator inspired by mimalloc. Its goal is to maintain a state machine for
 * bookkeeping the allocations of different sizes that are backed up by a separate
 * storage. It could be a disk, SSD or another memory allocator. This class serves
 * as a state machine that either returns an offset to the backign storage or the indication
 * of the resource that is missing. The advantage of such design is that we can use it in
 * asynchronous callbacks without blocking on any IO requests.
 * The allocator uses dynanic memory internally. Should be used in a single thread.
 *
 */

namespace detail {
class Page;

constexpr unsigned kLargeSizeBin = 34;
constexpr unsigned kNumSizeBins = kLargeSizeBin + 1;

/**
 * pages classes can be SMALL, MEDIUM or LARGE. SMALL (1MB) for block sizes upto 128KB.
 * MEDIUM (8MB) for block sizes upto 1MB. LARGE - blocks larger than 1MB.
 *
 */
enum PageClass : uint8_t {
  SMALL_P = 0,
  MEDIUM_P = 1,
  LARGE_P = 2,
};

}  // namespace detail

class ExternalAllocator {
  ExternalAllocator(const ExternalAllocator&) = delete;
  void operator=(const ExternalAllocator&) = delete;

 public:
  static constexpr size_t kExtAlignment = 1ULL << 28;  // 256 MB

  ExternalAllocator();

  // If a negative result - backing storage is required of size=-result. See AddStorage
  // on how to add more storage.
  // For results >= 0 Returns offset to the backing storage where we may write the data of
  // size sz.
  int64_t Malloc(size_t sz);
  void Free(size_t offset, size_t sz);

  /// Adds backing storage to the allocator.
  /// offset must be aligned to kExtAlignment boundaries.
  /// It is expected that storage is added in a linear fashion, without skipping ranges.
  /// So if [0, 256MB) is added, then next time [256MB, 512MB) is added etc.
  void AddStorage(size_t offset, size_t size);

  // Similar to mi_good_size, returns the size of the underlying block as if
  // were returned by Malloc. Guaranteed that the result not less than sz.
  // No allocation is done.
  static size_t GoodSize(size_t sz);

  size_t capacity() const {
    return capacity_;
  }

  size_t allocated_bytes() const {
    return allocated_bytes_;
  }

 private:
  class SegmentDescr;
  using Page = detail::Page;

  Page* FindPage(detail::PageClass sc, size_t* seg_size);
  SegmentDescr* GetNewSegment(detail::PageClass sc);
  void FreePage(Page* page, SegmentDescr* owner, size_t block_size);

  static SegmentDescr* ToSegDescr(Page*);

  SegmentDescr* sq_[3];  // map: PageClass -> free Segment.
  Page* free_pages_[detail::kNumSizeBins];

  // A segment for each 256MB range. To get a segment id from the offset, shift right by 28.
  std::vector<SegmentDescr*> segments_;

  // weird queue to support AddStorage interface. We can not instantiate segment
  // until we know its class and that we know only when a page is demanded.
  absl::btree_map<size_t, size_t> added_segs_;

  size_t capacity_ = 0;  // in bytes.
  size_t allocated_bytes_ = 0;
};

}  // namespace dfly