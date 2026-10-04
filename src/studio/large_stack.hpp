/**
 * @file large_stack.hpp
 * @brief (private) Run a callable on a dedicated thread with a large stack and wait for it.
 * @ingroup studio
 *
 * The formal tools (UTAP's parser, the aligner's zone exploration, Z3) recurse
 * deeply. HTTP worker threads have small default stacks (512 KiB on macOS), on
 * which these tools overflow. Every formal check therefore runs through
 * run_with_large_stack(), which uses a POSIX thread with an explicit stack size.
 */
#pragma once

#include <pthread.h>

#include <exception>
#include <optional>
#include <type_traits>
#include <utility>

#include "twin/core/result.hpp"

namespace twin::studio {

/// @brief Stack size for formal-check threads (256 MiB of reserved address space; committed lazily).
inline constexpr std::size_t kFormalCheckStack = std::size_t{256} * 1024 * 1024;

/**
 * @brief Execute @p fn on a fresh thread with @p stack_bytes of stack; blocks until it returns.
 * Exceptions thrown by @p fn are rethrown in the caller.
 */
template <class F>
auto run_with_large_stack(F&& fn, std::size_t stack_bytes = kFormalCheckStack) -> decltype(fn()) {
    using R = decltype(fn());
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
    if (rc != 0) {
        // Could not create the thread: run inline rather than failing the request.
        return fn();
    }
    pthread_join(thread, nullptr);
    if (task.error) std::rethrow_exception(task.error);
    return std::move(*task.result);
}

}  // namespace twin::studio
