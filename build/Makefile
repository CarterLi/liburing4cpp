ifeq ($(MODE),DEBUG)
	CXXFLAGS ?= -O0 -rdynamic -D_LIBCPP_DEBUG_LEVEL=1 -fno-omit-frame-pointer -fsanitize=address
else
	CXXFLAGS ?= -O3 -DNDEBUG -march=native
endif

ifeq ($(CXX),clang++)
	override CXXFLAGS += -stdlib=libc++ -lc++ -lc++abi
endif

override CXXFLAGS += -Wall -lboost_context -lfmt -I.. -I../cxx-yield -D_GNU_SOURCE=1 -DUSE_FCONTEXT=1 -DSYSCALL_COUNT=1 -std=c++17 -flto -march=native -g

all_targets = bench io_uring poll-epoll poll-io_uring poll-aio

all: $(all_targets)

help:
	@echo 'make [MODE={DEBUG,RELEASE}] [CXX={g++|clang++}]'

bench: ../io_uring/bench.cpp ../io_uring/io_coroutine.hpp ../io_uring/io_host.hpp
	$(CXX) $(CXXFLAGS) -luring ../io_uring/bench.cpp -o bench

io_uring: ../io_uring/ping-pong.cpp ../utils.hpp ../io_uring/io_coroutine.hpp ../io_uring/io_host.hpp ../io_uring/fixed_file.hpp
	$(CXX) $(CXXFLAGS) -luring ../io_uring/ping-pong.cpp -o io_uring

poll-epoll: ../poll/ping-pong.cpp ../utils.hpp ../poll/io_coroutine.hpp ../poll/io_host.hpp
	$(CXX) $(CXXFLAGS) ../poll/ping-pong.cpp -o poll-epoll

poll-io_uring: ../poll/ping-pong.cpp ../utils.hpp ../poll/io_coroutine.hpp ../poll/io_host.hpp
	$(CXX) $(CXXFLAGS) -DUSE_LIBURING=1 -luring ../poll/ping-pong.cpp -o poll-io_uring

poll-aio: ../poll/ping-pong.cpp ../utils.hpp ../poll/io_coroutine.hpp ../poll/io_host.hpp
	$(CXX) $(CXXFLAGS) -DUSE_LIBAIO=1 -laio ../poll/ping-pong.cpp -o poll-aio

clean:
	rm -f $(all_targets)
