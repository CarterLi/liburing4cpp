# liburing4cpp

Modern C++ binding for [liburing](https://github.com/axboe/liburing) that uses C++2a Coroutines ( but still compiles for `clang` at C++17 mode with `-fcoroutines-ts` )

Originally named liburing-http-demo ( this project was originally started for demo )

## Requirements

Requires the latest linux kernel ( currently 5.5 ). Since [io_uring](https://git.kernel.dk/cgit/liburing/) is in active development, we will drop old kernel support when every new linux kernel version is released ( before the next LTS version is released, maybe ).

Tested: `Linux carter-virtual-machine 5.5.0-999-generic #202002012101 SMP Sun Feb 2 02:07:23 UTC 2020 x86_64 x86_64 x86_64 GNU/Linux` with `clang version 9.0.1-8build1`

## First glance

```cpp
#include "io_service.hpp"

int main() {
    // You first need an io_service instance
    io_service service;

    // In order to `co_await`, you must be in a coroutine.
    // We use IIFE here for simplification
    auto work = [&] () -> task<> {
        // Use Linux syscalls just as what you did before (except a little changes)
        const auto str = "Hello world\n"sv;
        co_await service.write(STDOUT_FILENO, str.data(), str.size(), 0);
    }();

    // At last, you need a loop to dispatch finished IO events
    // It's usually called Event Loop (https://en.wikipedia.org/wiki/Event_loop)
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

Some examples

#### file_server.cpp

A simple http file server that returns file's content requested by clients

#### link_cp.cpp

A cp command inspired by original [liburing link-cp demo](https://github.com/axboe/liburing/blob/master/examples/link-cp.c)

#### http_client.cpp

A simple http client that sends `GET` http request

#### threading.cpp

A simple `async_invoke` implementation

#### test.cpp

Various simple tests

#### bench.cpp

Benchmarks

#### echo_server.cpp

Echo server, features IOSQE_IO_LINK and IOSQE_FIXED_FILE

See also https://github.com/frevib/io_uring-echo-server#benchmarks for benchmarking

## Build

This library is header only. It provides some demos for testing

You must have `liburing` built and installed first, and run `make` in directory `demo`, requires `clang++-9`. When benchmarking, you may want to build it with optimization: `make MODE=RELEASE`. Note it seems that `-flto` generates wrong code, don't use it ( for now )

## License

Public domain
