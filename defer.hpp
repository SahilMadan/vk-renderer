#pragma once

#include <functional>

// Lightning Talk: FINALLY - The Presentation You've All Been Waiting For
// Presentor: Louis Thomas
// Conference: CppCon 2021

#define DEFER DEFER_internal_1(__LINE__)
#define DEFER_internal_1(LINE) DEFER_internal_2(LINE)
#define DEFER_internal_2(LINE) auto defer_##LINE = util::defer

namespace util {

template <typename Fn>

auto defer(Fn action) {
  class DeferImpl {
   public:
    Fn action_;

    inline explicit DeferImpl(Fn action) : action_(std::move(action)){};

    inline ~DeferImpl() { action_(); }
  };
  return DeferImpl(std::move(fn));
}

};  // namespace util
