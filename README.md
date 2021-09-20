# liburing-http-demo

Simple http server demo using [liburing](http://kernel.dk/io_uring.pdf) and [Cxx-yield](https://github.com/CarterLi/Cxx-yield/)

Tested on `Linux Ubuntu 5.14.2-051402-generic #202109080331 SMP Wed Sep 8 07:35:12 UTC 2021 x86_64 x86_64 x86_64 GNU/Linux`

## Dependencies

* [liburing](http://git.kernel.dk/liburing) ( requires Linux 5.1 or later )
* [libaio](http://git.infradead.org/users/hch/libaio.git) ( fallback, requires Linux 4.18 or later )
* [fmt](https://github.com/fmtlib/fmt)
* [boost::context](https://boost.org) ( optional )
* CMake ( build )
* Compiler that supports C++17

## Introduction ( Chinese )

* https://segmentfault.com/a/1190000019300089
* https://segmentfault.com/a/1190000019361819

## Benchmark

### 1 Conn, 100000 msgs

TYPE     | METHOD        | USE_LINK |        1st |        2nd |        3rd |        mid |    rate
:-:      | :-:           | :-:      |         -: |         -: |         -: |         -: |      -:
EPOLL    | RECV-SEND     | -        | 1152471444 | 1237428357 | 1170941584 | 1170941584 | 100.38%
EPOLL    | SPLICE-SPLICE | -        | 1229160321 | 1225887357 | 1239826713 | 1225887357 | 100.00%
IO_URING | RECV-SEND     | 0        |  889995555 |  916739744 |  887430281 |  889995555 | 105.05%
IO_URING | SPLICE-SPLICE | 0        | 3676125047 | 3628336967 | 3645826790 | 3645826790 |  81.63%
IO_URING | SPLICE-SPLICE | 1        | 2711719003 | 2610140507 | 2736547714 | 2736547714 |  97.16%

### 500 Conns, 500 msgs

TYPE     | METHOD        | USE_LINK |        1st |        2nd |        3rd |        mid |    rate
:-:      | :-:           | :-:      |         -: |         -: |         -: |         -: |      -:
EPOLL    | RECV-SEND     | -        | 3657537054 | 3596052860 | 3734281147 | 3657537054 | 100.38%
EPOLL    | SPLICE-SPLICE | -        | 3615665399 | 3724534568 | 3666675832 | 3666675832 | 100.00%
IO_URING | RECV-SEND     | 0        | 2287768306 | 2302809944 | 2323533284 | 2302809944 | 105.05%
IO_URING | SPLICE-SPLICE | 0        | 2263911899 | 2314418887 | 2232968254 | 2263911899 |  81.63%
IO_URING | SPLICE-SPLICE | 1        | 2099712027 | 2206519410 | 2222934230 | 2206519410 |  97.16%

## License

Public domain
