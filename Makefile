async: async_main.cpp global.cpp task.hpp async_coro.hpp
	clang++ -std=c++2a -g -fcoroutines-ts -stdlib=libc++ -lc++ -lc++abi -luring -lfmt async_main.cpp global.cpp -o async -O0
