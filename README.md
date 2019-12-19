# liburing4cpp

Modern C++ binding for [liburing](https://github.com/axboe/liburing) that uses C++2a Coroutines ( but still compiles for `clang` at C++17 mode with `-fcoroutines-ts` )

Originally named liburing-http-demo ( this project was originally started for demo )

## Requirements

Requires the latest stable linux kernel ( currently 5.4 ). Since [io_uring](https://git.kernel.dk/cgit/liburing/) is in active development, we will drop old kernel support when every new linux kernel version is released ( before the next LTS version is released, maybe ).

Tested: `Linux archlinux-pc 5.4.0-arch1-1 #1 SMP PREEMPT Mon, 25 Nov 2019 22:31:57 +0000 x86_64 GNU/Linux` with `clang-9.0.0`

## First glance

```cpp
#include "io_service.hpp"

int main() {
    io_service service;

    auto work = [&] () -> task<> {
        co_await service.writev(STDOUT_FILENO, to_iov("Hello world\n"), 0);
    }();

    while (!work.done()) {
        auto [promise, res] = service.wait_event();
        promise->resolve(res);
    }
}
```

## Project Structure

### task.hpp

An awaitable class for C++2a coroutine functions. Originally modified from [gor_task.h](https://github.com/Quuxplusone/coro#taskh-gor_taskh)

### promise.hpp

An awaitable class. It's different from task that it can't used for return type, but can be created directly without calling an async function.

Its design is highly inspired by [Promise of JavaScript](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise)

### when.hpp

Provide helper functions that working with an array of tasks

### io_service.hpp

Main [liburing](https://github.com/axboe/liburing) binding. Also provides some helper functions for working with posix interfaces easier.

### demo

Some demo

#### file_server.cpp

A simple http file server that returns file's content requested by clients

#### link_cp.cpp

A cp command inspired by original [liburing link-cp demo](https://github.com/axboe/liburing/blob/master/examples/link-cp.c)

## Build

This library is header only. It provides some demos for testing

`make`, requires clang++-9, `-std=c++17 -stdlib=libc++ -fcoroutines-ts`; depends on liburing & [fmt](http://fmtlib.net/)

## License

Public domain
