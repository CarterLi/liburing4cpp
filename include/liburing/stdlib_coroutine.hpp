#pragma once
#if __has_include(<coroutine>)

// This needs to be defined to enable the <coroutine> header in libstdc++ when
// using clang
#    if defined(__clang__) && !defined(__cpp_impl_coroutine)
#        define __cpp_impl_coroutine 201902L
#    endif

#    include <coroutine>

// std::coroutine_handle and std::coroutine_traits need to be included in
// std::experimental when using libstdc++ and clang together, however the others
// don't.
#    ifdef __clang__
namespace std::experimental {
using std::coroutine_handle;
using std::coroutine_traits;
} // namespace std::experimental
#    endif

// If we have the include <experimental/coroutine>, then we need to ensure that
// std::experimental::* are inside namespace std
#elif __has_include(<experimental/coroutine>)
#    include <experimental/coroutine>
namespace std {
using std::experimental::coroutine_handle;
using std::experimental::coroutine_traits;
using std::experimental::noop_coroutine;
using std::experimental::noop_coroutine_handle;
using std::experimental::noop_coroutine_promise;
using std::experimental::suspend_always;
using std::experimental::suspend_never;
} // namespace std
#endif
