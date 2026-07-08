#pragma once

namespace services::map {

/** Non-recursive busy flag for guarding a re-entrant call site. Arduino-free
 *  so native tests can cover it (same idiom as ui/color_blend.h).
 *
 *  Only the lock instance that actually acquired the flag clears it on
 *  destruction -- a nested lock that finds the flag already held must NOT
 *  clear it when it goes out of scope, or it would release the outer lock's
 *  hold early and defeat the guard. */
class RebuildLock {
 public:
  explicit RebuildLock(bool& busy) : busy_(busy), acquired_(!busy) {
    if (acquired_) busy_ = true;
  }
  ~RebuildLock() {
    if (acquired_) busy_ = false;
  }
  bool acquired() const { return acquired_; }

  RebuildLock(const RebuildLock&) = delete;
  RebuildLock& operator=(const RebuildLock&) = delete;

 private:
  bool& busy_;
  bool acquired_;
};

}  // namespace services::map
