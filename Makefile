LDFLAGS?=-lboost_context

ifeq ($(MODE),DEBUG)
	CXXFLAGS ?= -O0 -rdynamic -D_LIBCPP_DEBUG_LEVEL=1 -fno-omit-frame-pointer -fsanitize=address
else
	CXXFLAGS ?= -O3 -DNDEBUG -march=native -mtune=intel
endif

all: file-server echo-server

libaco.a:
	$(CC) -g -O3 -Wall -Werror ./libaco/acosw.S ./libaco/aco.c -c -fPIE
	ar rcs libaco.a acosw.o aco.o

libfmt.a:
	$(CXX) -g -O3 -Wall -Werror ./fmt/src/format.cc -I./fmt/include -c -fPIE
	ar rcs libfmt.a format.o

file-server: libaco.a libfmt.a file-server.cpp io_coroutine.hpp yield.hpp io_host.hpp utils.hpp
	$(CXX) $(CXXFLAGS) file-server.cpp libfmt.a -I./fmt/include -D_GNU_SOURCE=1 -std=c++17 -flto -march=native -g -o file-server -DUSE_FCONTEXT=1 -luring $(LDFLAGS)

echo-server: libaco.a libfmt.a echo-server.cpp io_coroutine.hpp yield.hpp io_host.hpp utils.hpp
	$(CXX) $(CXXFLAGS) echo-server.cpp libfmt.a -I./fmt/include -D_GNU_SOURCE=1 -std=c++17 -flto -march=native -g -o echo-server -DUSE_FCONTEXT=1 -luring $(LDFLAGS)

clean:
	rm -f file-server echo-server
