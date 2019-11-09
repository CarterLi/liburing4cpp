async: async_main.cpp global.cpp task.hpp async_coro.hpp promise.hpp
	clang++ -std=c++17 -g -fcoroutines-ts -stdlib=libc++ -lc++ -lc++abi -luring -lfmt ./async_main.cpp ./global.cpp -o async -O0 -fno-omit-frame-pointer -fsanitize=address
