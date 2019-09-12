# liburing-http-demo

Simple http server demo using [liburing](http://kernel.dk/io_uring.pdf) and [Cxx-yield](https://github.com/CarterLi/Cxx-yield/)

Tested on `Linux Ubuntu 5.3.0-10-generic #11-Ubuntu SMP Mon Sep 9 15:12:17 UTC 2019 x86_64 x86_64 x86_64 GNU/Linux`

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

## More info

https://github.com/libuv/libuv/issues/1947

## License

Public domain
