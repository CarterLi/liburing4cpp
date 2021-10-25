ifeq ($(MODE),DEBUG)
	CXXFLAGS ?= -O0 -rdynamic -D_LIBCPP_DEBUG_LEVEL=1 -fno-omit-frame-pointer -fsanitize=address
else
	CXXFLAGS ?= -O3 -DNDEBUG -march=native
endif

override CXXFLAGS += -Wall -IC:\vcpkg\installed\x64-windows\include\ -I..\win_ring\libwinring\include\ -LC:\vcpkg\installed\x64-windows\lib\ -lfmt -lboost_context-vc140-mt -I.. -I..\cxx-yield -DUSE_FCONTEXT=1 -DSYSCALL_COUNT=1 -std=c++17 -march=native -g

override CXX = clang++

all_targets = bench.exe

all: $(all_targets)

fmt.dll:
	copy C:\vcpkg\installed\x64-windows\bin\fmt.dll .

boost_context-vc143-mt-x64-1_77.dll:
	copy C:\vcpkg\installed\x64-windows\bin\boost_context-vc143-mt-x64-1_77.dll .

bench.exe: fmt.dll boost_context-vc143-mt-x64-1_77.dll
	$(CXX) $(CXXFLAGS) ..\win_ring\bench.cpp -o bench.exe
