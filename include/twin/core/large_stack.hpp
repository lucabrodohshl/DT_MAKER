/**
 * @file large_stack.hpp
 * @brief Run a callable on a dedicated thread with a large stack and wait for it.
 * @ingroup core
 *
 * The formal tools (UTAP's parser, the aligner's zone exploration, Z3) recurse
 * deeply. Worker threads have small default stacks (512 KiB on macOS), on which
 * these tools overflow; the main thread has 8 MiB. Code that may run formal
 * tools off the main thread (HTTP handlers, job workers) runs them through
 * run_with_large_stack(), which uses a POSIX thread with an explicit stack size.
 * (Studio's services use the same technique in src/studio/large_stack.hpp.)
 */
#pragma once

#include <pthread.h>

#include <cstddef>
#include <exception>
#include <optional>
#include <type_traits>
#include <utility>

namespace twin {

/// @brief Stack size for formal-tool threads (256 MiB of reserved address space; committed lazily).
inline constexpr std::size_t kFormalToolStack = std::size_t{256} * 1024 * 1024;

/**
 * @brief Execute @p fn on a fresh thread with @p stack_bytes of stack; blocks until it returns.
 * Exceptions thrown by @p fn are rethrown in the caller. If no thread can be created,
 * @p fn runs inline.
 */
template <class F>
auto run_with_large_stack(F&& fn, std::size_t stack_bytes = kFormalToolStack) -> decltype(fn()) {
    using R = decltype(fn());
    static_assert(!std::is_void_v<R>, "run_with_large_stack needs a callable that returns a value");
    struct Task {
        std::remove_reference_t<F>* fn;
        std::optional<R> result;
        std::exception_ptr error;
    } task{&fn, std::nullopt, nullptr};
    auto entry = [](void* p) -> void* {
        auto* t = static_cast<Task*>(p);
        try {
            t->result.emplace((*t->fn)());
        } catch (...) {
            t->error = std::current_exception();
        }
        return nullptr;
    };
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, stack_bytes);
    pthread_t thread;
    const int rc = pthread_create(&thread, &attr, entry, &task);
    pthread_attr_destroy(&attr);
    if (rc != 0) return fn();
    pthread_join(thread, nullptr);
    if (task.error) std::rethrow_exception(task.error);
    return std::move(*task.result);
}

}  // namespace twin
