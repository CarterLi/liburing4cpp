async: async_main.cpp global.cpp task.hpp async_coro.hpp promise.hpp when.hpp
	clang++ -std=c++17 -g -fcoroutines-ts -stdlib=libc++ -lc++ -lc++abi -luring -lfmt ./async_main.cpp ./global.cpp -o async -O0 -D_LIBCPP_DEBUG_LEVEL=1 -fno-omit-frame-pointer -fsanitize=address
