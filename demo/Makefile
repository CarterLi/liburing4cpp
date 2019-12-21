CXX = clang++

CXXFLAGS ?= -O0 -g -rdynamic -D_LIBCPP_DEBUG_LEVEL=1 -fno-omit-frame-pointer -fsanitize=address

override CXXFLAGS += -Wall -fcoroutines-ts -stdlib=libc++ -std=c++17 -lc++ -lc++abi -I.. -I../fmt/include ../fmt/src/format.cc -luring

all_targets = file_server http_client link_cp

all: $(all_targets)

clean:
	rm -f $(all_targets)

file_server: file_server.cpp mime_dicts.cpp ../task.hpp ../io_service.hpp ../promise.hpp ../when.hpp ../cancelable.hpp
	$(CXX) ./file_server.cpp ./mime_dicts.cpp -o file_server $(CXXFLAGS)

http_client: http_client.cpp ../task.hpp ../io_service.hpp ../promise.hpp ../when.hpp ../cancelable.hpp
	$(CXX) ./http_client.cpp -o http_client $(CXXFLAGS)

link_cp: link_cp.cpp ../task.hpp ../io_service.hpp ../promise.hpp ../when.hpp ../cancelable.hpp
	$(CXX) ./link_cp.cpp -o link_cp $(CXXFLAGS)
