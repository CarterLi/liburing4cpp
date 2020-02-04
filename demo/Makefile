CXX_COMPILER ?= clang++
MODE ?= DEBUG

ifeq ($(MODE),DEBUG)
	CXXFLAGS ?= -O0 -rdynamic -D_LIBCPP_DEBUG_LEVEL=1 -fno-omit-frame-pointer -fsanitize=address
else
	CXXFLAGS ?= -O3 -DNDEBUG
endif

ifeq ($(CXX_COMPILER),clang++)
	override CXXFLAGS += -fcoroutines-ts -stdlib=libc++ -lc++ -lc++abi
else
	override CXXFLAGS += -fcoroutines
endif

override CXXFLAGS += -g -Wall -std=c++17 -I.. -I../fmt/include ../fmt/src/format.cc -luring -pthread

all_targets = file_server http_client link_cp threading test bench echo_server

all: $(all_targets)

clean:
	rm -f $(all_targets)

file_server: file_server.cpp mime_dicts.cpp ../task.hpp ../io_service.hpp ../promise.hpp ../when.hpp ../cancelable.hpp
	$(CXX_COMPILER) ./file_server.cpp ./mime_dicts.cpp -o file_server $(CXXFLAGS)

http_client: http_client.cpp ../task.hpp ../io_service.hpp ../promise.hpp ../when.hpp ../cancelable.hpp
	$(CXX_COMPILER) ./http_client.cpp -o http_client $(CXXFLAGS)

link_cp: link_cp.cpp ../task.hpp ../io_service.hpp ../promise.hpp ../when.hpp ../cancelable.hpp
	$(CXX_COMPILER) ./link_cp.cpp -o link_cp $(CXXFLAGS)

threading: threading.cpp ../task.hpp ../io_service.hpp ../promise.hpp ../cancelable.hpp
	$(CXX_COMPILER) ./threading.cpp -o threading $(CXXFLAGS)

test: test.cpp ../task.hpp ../io_service.hpp ../promise.hpp ../when.hpp ../cancelable.hpp
	$(CXX_COMPILER) ./test.cpp -o test $(CXXFLAGS)

bench: bench.cpp ../task.hpp ../io_service.hpp ../promise.hpp ../cancelable.hpp
	$(CXX_COMPILER) ./bench.cpp -o bench $(CXXFLAGS)

echo_server: echo_server.cpp ../task.hpp ../io_service.hpp ../promise.hpp ../cancelable.hpp
	$(CXX_COMPILER) ./echo_server.cpp -o echo_server $(CXXFLAGS)
