file_server: file_server.cpp mime_dicts.cpp ../task.hpp ../io_service.hpp ../promise.hpp ../when.hpp
	clang++ -std=c++17 -g -fcoroutines-ts -stdlib=libc++ -lc++ -lc++abi -luring -lfmt ./file_server.cpp ./mime_dicts.cpp -I.. -o file_server -O0 -D_LIBCPP_DEBUG_LEVEL=1 -fno-omit-frame-pointer -fsanitize=address
