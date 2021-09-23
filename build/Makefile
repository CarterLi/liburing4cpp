LDFLAGS?=-lboost_context

ifeq ($(MODE),DEBUG)
	CXXFLAGS ?= -O0 -rdynamic -D_LIBCPP_DEBUG_LEVEL=1 -fno-omit-frame-pointer -fsanitize=address
else
	CXXFLAGS ?= -O3 -DNDEBUG -march=native -mtune=intel
endif

all: io_uring poll-epoll poll-io_uring poll-aio

libaco.a:
	$(CC) -g -O3 -Wall -Werror -flto ../cxx-yield/libaco/acosw.S ../cxx-yield/libaco/aco.c -c -fPIE
	ar rcs -flto libaco.a acosw.o aco.o
	rm acosw.o aco.o

libfmt.a:
	$(CXX) -g -O3 -Wall -Werror -flto ../fmt/src/format.cc ../fmt/src/os.cc -I../fmt/include -c -fPIE
	ar rcs -flto libfmt.a format.o os.o
	rm format.o os.o

io_uring: libfmt.a ../io_uring/ping-pong.cpp ../yield.hpp ../utils.hpp ../io_uring/io_coroutine.hpp ../io_uring/io_host.hpp ../io_uring/fixed_file.hpp
	$(CXX) $(CXXFLAGS) ../io_uring/ping-pong.cpp libfmt.a -I../fmt/include -I.. -D_GNU_SOURCE=1 -std=c++17 -flto -march=native -g -luring -o io_uring -DUSE_FCONTEXT=1 -DSYSCALL_COUNT=1 $(LDFLAGS)

poll-epoll: libfmt.a ../poll/ping-pong.cpp ../yield.hpp ../utils.hpp ../poll/io_coroutine.hpp ../poll/io_host.hpp
	$(CXX) $(CXXFLAGS) ../poll/ping-pong.cpp libfmt.a -I../fmt/include -I.. -D_GNU_SOURCE=1 -std=c++17 -flto -march=native -g -o poll-epoll -DUSE_FCONTEXT=1 -DSYSCALL_COUNT=1 $(LDFLAGS)

poll-io_uring: libfmt.a ../poll/ping-pong.cpp ../yield.hpp ../utils.hpp ../poll/io_coroutine.hpp ../poll/io_host.hpp
	$(CXX) $(CXXFLAGS) ../poll/ping-pong.cpp libfmt.a -I../fmt/include -I.. -D_GNU_SOURCE=1 -std=c++17 -flto -march=native -g -DUSE_LIBURING=1 -luring -o poll-io_uring -DUSE_FCONTEXT=1 -DSYSCALL_COUNT=1 $(LDFLAGS)

poll-aio: libfmt.a ../poll/ping-pong.cpp ../yield.hpp ../utils.hpp ../poll/io_coroutine.hpp ../poll/io_host.hpp
	$(CXX) $(CXXFLAGS) ../poll/ping-pong.cpp libfmt.a -I../fmt/include -I.. -D_GNU_SOURCE=1 -std=c++17 -flto -march=native -g -DUSE_LIBAIO=1 -laio -o poll-aio -DUSE_FCONTEXT=1 -DSYSCALL_COUNT=1 $(LDFLAGS)

clean:
	rm -f io_uring poll-epoll poll-io_uring poll-aio
