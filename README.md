# liburing4cpp

Simple http server demo using [liburing](http://kernel.dk/io_uring.pdf) and [Cxx-yield](https://github.com/CarterLi/Cxx-yield/)

Tested on `Linux Ubuntu 5.14.6-051406-generic #202109181232 SMP Sat Sep 18 12:35:35 UTC 2021 x86_64 x86_64 x86_64 GNU/Linux`

## Dependencies

* [liburing](http://git.kernel.dk/liburing)
* [libaio](http://git.infradead.org/users/hch/libaio.git)
* [fmt](https://github.com/fmtlib/fmt)
* [boost::context](https://boost.org) ( optional )
* Compiler that supports C++17

## Introduction ( Chinese )

* https://segmentfault.com/a/1190000019300089
* https://segmentfault.com/a/1190000019361819

## Benchmark

### 1 Conn, 100000 msgs

#### POLL

TYPE       | METHOD        |        1st |        2nd |        3rd |        mid | syscalls
:-:        | :-:           |         -: |         -: |         -: |         -: |       -:
EPOLL      | RECV-SEND     | 1213720024 | 1203002323 | 1202268956 | 1203002323 |   800007
EPOLL      | SPLICE-SPLICE | 1222139555 | 1276005966 | 1226541992 | 1226541992 |   800007
AIO        | RECV-SEND     | 1295369109 | 1313941823 | 1285587449 | 1285587449 |   800003
AIO        | SPLICE-SPLICE | 1304920320 | 1323454602 | 1344003697 | 1323454602 |   800003
IO_URING   | RECV-SEND     | 1097310812 | 1076904395 | 1052063003 | 1076904395 |   200003
IO_URING   | SPLICE-SPLICE | 1062135048 | 1134858429 | 1080572551 | 1080572551 |   200003

#### IO_URING without POLL

METHOD        | USE_LINK |        1st |        2nd |        3rd |        mid | syscalls
:-:           | :-:      |         -: |         -: |         -: |         -: |       -:
RECV-SEND     | 0        |  888807084 |  866377056 |  932095066 |  888807084 |   200003
SPLICE-SPLICE | 0        | 3571375121 | 3576119028 | 3632239887 | 3576119028 |   294766
SPLICE-SPLICE | 1        | 2711935931 | 2696392063 | 2722265501 | 2722265501 |   218582

### 500 Conns, 500 msgs

#### POLL

TYPE       | METHOD        |        1st |        2nd |        3rd |        mid | syscalls
:-:        | :-:           |         -: |         -: |         -: |         -: |      -:
EPOLL      | RECV-SEND     | 3417458521 | 3349537493 | 3359452396 | 3359452396 |  1004502
EPOLL      | SPLICE-SPLICE | 3408531236 | 3325660818 | 3303161806 | 3325660818 |  1004502
AIO        | RECV-SEND     | 2770272374 | 2763336323 | 2802280021 | 2770272374 |     5001
AIO        | SPLICE-SPLICE | 2772713512 | 2824479822 | 2776287044 | 2772713512 |     5001
IO_URING   | RECV-SEND     | 2944616957 | 2934462576 | 2915093399 | 2934462576 |     1502
IO_URING   | SPLICE-SPLICE | 3002170858 | 3023692843 | 2942249975 | 3023692843 |     1502

#### IO_URING without POLL

METHOD        | USE_LINK |        1st |        2nd |        3rd |        mid | syscalls
:-:           | :-:      |         -: |         -: |         -: |         -: |       -:
RECV-SEND     | 0        | 2241744584 | 2224194012 | 2242771549 | 2241744584 |     1502
SPLICE-SPLICE | 0        | 4158028540 | 4042506178 | 4058224613 | 4058224613 |     1502
SPLICE-SPLICE | 1        | 3555245458 | 3493551902 | 3482760968 | 3482760968 |     1004

## License

Public domain
