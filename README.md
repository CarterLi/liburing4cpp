# liburing4cpp

Modern C++ binding for [liburing](https://github.com/axboe/liburing) that uses C++20 Coroutines ( but still compiles for `clang` at C++17 mode with `-fcoroutines-ts` )

Originally named liburing-http-demo ( this project was originally started for demo )

## Requirements

Requires the latest kernel ( currently 5.6 ). Since [io_uring](https://git.kernel.dk/cgit/liburing/) is in active development, we will drop old kernel support when every new linux kernel version is released ( before the next LTS version is released, maybe ).

Tested: `Linux carter-virtual-machine 5.7.0-999-generic #202005082207 SMP Sat May 9 02:09:38 UTC 2020 x86_64 x86_64 x86_64 GNU/Linux` with `clang version 10.0.0-4ubuntu1`

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
    service.run(work);
}
```

## Benchmarks

* VMWare Ubuntu Focal Fossa 20.04 (development branch)
* Linux carter-virtual-machine 5.5.0-999-generic #202002070204 SMP Fri Feb 7 02:09:27 UTC 2020 x86_64 x86_64 x86_64 GNU/Linux
* 4 virtual cores
* Macbook pro i7 2.5GHz/16GB
* Compiler: clang version 9.0.1-8build1

### demo/bench

```
service.yield:        5857666577
plain IORING_OP_NOP:  5354647639
this_thread::yield:   4709248308
pause:                  26562813
```

### demo/echo_server

* demo/echo_server 12345 ( C++, uses coroutines )
* [./io_uring_echo_server 12345](https://github.com/CarterLi/io_uring-echo-server) ( C, raw )

with `rust_echo_bench`: https://github.com/haraldh/rust_echo_bench
unit: request/sec

Also see [benchmarks for different opcodes](https://github.com/CarterLi/io_uring-echo-server#benchmarks)

#### command: `cargo run --release -- -c 50`

LANG | USE_LINK | USE_FIXED |           operations |     1st |     2nd |     3rd |     mid |    rate
:-:  | :-:      | :-:       |                   -: |      -: |      -: |      -: |      -: |      -:
C    | -        | -         |       POLL-RECV-SEND |  158517 |  163899 |  156310 |  158517 | 100.00%
C++  | 0        | 0         |       POLL-RECV-SEND |  146928 |  140234 |  159169 |  146928 |  92.69%
C++  | 0        | 1         |  POLL-READ_F-WRITE_F |  133356 |  124511 |  148615 |  133356 |  84.12%
C++  | 1        | 0         |       POLL-RECV-SEND |  -      |  -      |  -      |  -      |
C++  | 1        | 1         |  POLL-READ_F-WRITE_F |  -      |  -      |  -      |  -      |

#### command: `cargo run --release -- -c 200`

LANG | USE_LINK | USE_FIXED |           operations |     1st |     2nd |     3rd |     mid |    rate
:-:  | :-:      | :-:       |                   -: |      -: |      -: |      -: |      -: |      -:
C    | -        | -         |       POLL-RECV-SEND |  150691 |  151855 |  147474 |  150691 | 100.00%
C++  | 0        | 0         |       POLL-RECV-SEND |  141288 |  138427 |  155898 |  141288 |  93.76%
C++  | 0        | 1         |  POLL-READ_F-WRITE_F |  141509 |  146540 |  143541 |  143541 |  95.25%
C++  | 1        | 0         |       POLL-RECV-SEND |  -      |  -      |  -      |  -      |
C++  | 1        | 1         |  POLL-READ_F-WRITE_F |  -      |  -      |  -      |  -      |

## Project Structure

### task.hpp

An awaitable class for C++2a coroutine functions. Originally modified from [gor_task.h](https://github.com/Quuxplusone/coro#taskh-gor_taskh)

NOTE: `task` is not lazily executed, which is easy to use of course, but also can be easily misused. The simplest code to crash your memory is:

```c++
{
    char c;
    service.read(STDIN_FILENO, &c, sizeof (c), 0);
}
```

The task instance returned by `service.read` is destructed, but the kernel task itself is **NOT** canceled. The memory of variable `c` will be written sometime. In this case, out-of-scope stack memory access will happen.

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

MIT
