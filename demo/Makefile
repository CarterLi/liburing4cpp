CXX_COMPILER ?= clang++
MODE ?= DEBUG

ifeq ($(MODE),DEBUG)
	CXXFLAGS ?= -O0 -rdynamic -D_LIBCPP_DEBUG_LEVEL=1 -fno-omit-frame-pointer -fsanitize=address
else
	CXXFLAGS ?= -O3 -DNDEBUG -march=native
endif

ifeq ($(CXX_COMPILER),clang++)
	override CXXFLAGS += -fcoroutines-ts -stdlib=libc++ -lc++ -lc++abi
else
	override CXXFLAGS += -fcoroutines
endif

override CXXFLAGS += -g -Wall -std=c++17 -I.. -lfmt -luring -pthread

all_targets = file_server http_client link_cp threading test bench echo_server

all: $(all_targets)

clean:
	rm -f $(all_targets)

help:
	@echo 'make [MODE={DEBUG,RELEASE}] [CXX_COMPILER={g++|clang++}]'

file_server: file_server.cpp ../include/task.hpp ../include/io_service.hpp
	$(CXX_COMPILER) ./file_server.cpp -I../include -o file_server $(CXXFLAGS)

http_client: http_client.cpp ../include/task.hpp ../include/io_service.hpp
	$(CXX_COMPILER) ./http_client.cpp -I../include -o http_client $(CXXFLAGS)

link_cp: link_cp.cpp ../include/task.hpp ../include/io_service.hpp
	$(CXX_COMPILER) ./link_cp.cpp -I../include -o link_cp $(CXXFLAGS)

threading: threading.cpp ../include/task.hpp ../include/io_service.hpp
	$(CXX_COMPILER) ./threading.cpp -I../include -o threading $(CXXFLAGS)

test: test.cpp ../include/task.hpp ../include/io_service.hpp
	$(CXX_COMPILER) ./test.cpp -I../include -o test $(CXXFLAGS)

bench: bench.cpp ../include/task.hpp ../include/io_service.hpp
	$(CXX_COMPILER) ./bench.cpp -I../include -o bench $(CXXFLAGS)

echo_server: echo_server.cpp ../include/task.hpp ../include/io_service.hpp
	$(CXX_COMPILER) ./echo_server.cpp -I../include -o echo_server $(CXXFLAGS)
