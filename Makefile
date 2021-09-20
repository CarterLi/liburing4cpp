LDFLAGS?=-lboost_context

ifeq ($(MODE),DEBUG)
	CXXFLAGS ?= -O0 -rdynamic -D_LIBCPP_DEBUG_LEVEL=1 -fno-omit-frame-pointer -fsanitize=address
else
	CXXFLAGS ?= -O3 -DNDEBUG -march=native -mtune=intel
endif

all: io_uring.o epoll.o aio.o

libaco.a:
	$(CC) -g -O3 -Wall -Werror ./libaco/acosw.S ./libaco/aco.c -c -fPIE
	ar rcs libaco.a acosw.o aco.o

libfmt.a:
	$(CXX) -g -O3 -Wall -Werror ./fmt/src/format.cc -I./fmt/include -c -fPIE
	ar rcs libfmt.a format.o

io_uring.o: libaco.a libfmt.a io_uring/ping-pong.cpp yield.hpp utils.hpp io_uring/io_coroutine.hpp io_uring/io_host.hpp
	$(CXX) $(CXXFLAGS) io_uring/ping-pong.cpp libfmt.a -I./fmt/include -I. -D_GNU_SOURCE=1 -std=c++17 -flto -march=native -g -o io_uring.o -DUSE_FCONTEXT=1 -luring $(LDFLAGS)

epoll.o: libaco.a libfmt.a epoll/ping-pong.cpp yield.hpp utils.hpp epoll/io_coroutine.hpp epoll/io_host.hpp
	$(CXX) $(CXXFLAGS) epoll/ping-pong.cpp libfmt.a -I./fmt/include -I. -D_GNU_SOURCE=1 -std=c++17 -flto -march=native -g -o epoll.o -DUSE_FCONTEXT=1 $(LDFLAGS)

aio.o: libaco.a libfmt.a epoll/ping-pong.cpp yield.hpp utils.hpp epoll/io_coroutine.hpp epoll/io_host.hpp
	$(CXX) $(CXXFLAGS) epoll/ping-pong.cpp libfmt.a -I./fmt/include -I. -D_GNU_SOURCE=1 -std=c++17 -flto -DUSE_LIBAIO=1 -laio -march=native -g -o aio.o -DUSE_FCONTEXT=1 $(LDFLAGS)

clean:
	rm -f io_uring.o epoll.o aio.o
