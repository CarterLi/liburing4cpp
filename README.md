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

## Benchmarks

* VMWare Ubuntu Focal Fossa 20.04 (development branch)
* Linux carter-virtual-machine 5.5.0-999-generic #202002070204 SMP Fri Feb 7 02:09:27 UTC 2020 x86_64 x86_64 x86_64 GNU/Linux
* 4 virtual cores
* Macbook pro i7 2.5GHz/16GB
* Compiler: clang version 9.0.1-8build1

### demo/bench

```
service.yield:        5802611604
plain IORING_OP_NOP:  5430090005
this_thread::yield:   4794909829
pause:                  26623780
```

### demo/echo_server

* demo/echo_server 12345 ( C++, uses coroutines )
* [./io_uring_echo_server 12345](https://github.com/CarterLi/io_uring-echo-server) ( C, raw )

with `rust_echo_bench`: https://github.com/haraldh/rust_echo_bench

#### command: `cargo run --release -- -c 50`

LANG | USE_POLL | USE_FIXED |           operations |     1st |     2nd |     3rd |     mid |   rate
:-:  | :-:      | :-:       |                   -: |      -: |      -: |      -: |      -: |     -:
C    | 1        | -         | POLL-RECVMSG-SENDMSG |  137656 |  133897 |  140219 |  133897 | 100.0%
C++  | 1        | 0         | POLL-RECVMSG-SENDMSG |  122892 |   97884 |  137922 |  122892 |  91.8%
C++  | 1        | 1         |  POLL-READ_F-WRITE_F |  125645 |  120349 |  117123 |  120349 |  89.9%

LANG | USE_POLL | USE_FIXED |           operations |     1st |     2nd |     3rd |     mid |   rate
:-:  | :-:      | :-:       |                   -: |      -: |      -: |      -: |      -: |     -:
C    | 0        | -         |      RECVMSG-SENDMSG |   97500 |   84620 |   89185 |   89185 | 100.0%
C++  | 0        | 0         |      RECVMSG-SENDMSG |   95357 |   97559 |  100073 |   97559 | 109.4%
C++  | 0        | 1         |       READ_F-WRITE_F |  101917 |  102845 |   95126 |  101917 | 114.3%

#### command: `cargo run --release -- -c 1`

LANG | USE_POLL | USE_FIXED |           operations |     1st |     2nd |     3rd |     mid |   rate
:-:  | :-:      | :-:       |                   -: |      -: |      -: |      -: |      -: |     -:
C    | 1        | -         | POLL-RECVMSG-SENDMSG |   16637 |   14697 |   13854 |   14697 | 100.0%
C++  | 1        | 0         | POLL-RECVMSG-SENDMSG |   14943 |   12507 |   12984 |   12984 |  88.3%
C++  | 1        | 1         |  POLL-READ_F-WRITE_F |   15122 |   13482 |   17776 |   15122 | 102.9%

LANG | USE_POLL | USE_FIXED |           operations |     1st |     2nd |     3rd |     mid |   rate
:-:  | :-:      | :-:       |                   -: |      -: |      -: |      -: |      -: |     -:
C    | 0        | -         |      RECVMSG-SENDMSG |   21234 |   24287 |   20465 |   21234 | 100.0%
C++  | 0        | 0         |      RECVMSG-SENDMSG |   55424 |   49969 |   48218 |   49969 | 235.3%
C++  | 0        | 1         |       READ_F-WRITE_F |   57104 |   42985 |   58549 |   57104 | 268.9%

## Performance suggestions:

Until Linux 5.5 at least

### For non-disk I/O, always `POLL` before `READ`/`RECV`.  

For operations that may block, kernel will punt them into a kernel worker called `io-wq`, which turns out to have high overhead cost. Always make sure that the fd to read is ready to read.


### Carefully use `IOSQE_IO_LINK`.

`IOSQE_IO_LINK` isn't something that make the operation zero-copy, but a way to reduce the number of `io_uring_enter` syscalls. Less syscalls means less context switches, which is good, but operations marked `IOSQE_IO_LINK` will still awake `io_uring_enter` ( ie `io_uring_wait_cqe` ). Users usually have nothing to do but to wait the whole link chain being completed by issuing another `io_uring_enter` syscall. So if you can't control the number of cqe to wait ( ie use `io_uring_wait_cqes` ), don't use `IOSQE_IO_LINK`.

For `READ-WRITE` chain, be sure to check `-ECANCELED` result of `WRITE` operation ( a short read is considered an error in a link chain which will cancel operations after the operation ). Never use `IOSQE_IO_LINK` for `RECV-SEND` chain because you can't control the number of bytes to send (a short read for `RECV` is NOT considered an error. I don't know why).

### Don't use FIXED_FILE & FIXED_BUFFER. 
They have little performace boost but increase much code complexity. Because the number of files and buffers can be registered has limitation, you almost always have to write fallbacks. In addition, you have to reuse the old file *slots* and buffers. See example: https://github.com/CarterLi/liburing4cpp/blob/daf6261419f39aae9a6624f0a271242b1e228744/demo/echo_server.cpp#L37

Note `RECV`/`SEND` have no `_fixed` variant.

### Don't use `io_uring_submit_and_wait(1)`.

`io_uring_submit_and_wait(&ring, 1); io_uring_wait_cqe(&ring, &cqe);` turns out to be slower than `io_uring_submit(&ring); io_uring_wait_cqe(&ring, &cqe)`. Details unknown.

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
