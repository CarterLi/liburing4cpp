CXX = clang++

CXXFLAGS ?= -O0 -g -rdynamic -D_LIBCPP_DEBUG_LEVEL=1 -fno-omit-frame-pointer -fsanitize=address

override CXXFLAGS += -Wall -fcoroutines-ts -stdlib=libc++ -std=c++17 -lc++ -lc++abi -I.. -I../fmt/include ../fmt/src/format.cc -luring

all_targets = whens

all: $(all_targets)

clean:
	rm -f $(all_targets)

whens: whens.cpp ../task.hpp ../io_service.hpp ../promise.hpp ../when.hpp ../cancelable.hpp
	$(CXX) ./whens.cpp -o whens $(CXXFLAGS)
