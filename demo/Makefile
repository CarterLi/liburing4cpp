all: file_server link_cp
	echo OK

file_server: file_server.cpp mime_dicts.cpp ../task.hpp ../io_service.hpp ../promise.hpp ../when.hpp
	clang++ -std=c++17 -g -fcoroutines-ts -stdlib=libc++ -lc++ -lc++abi -luring -lfmt ./file_server.cpp ./mime_dicts.cpp -I.. -o file_server -O0 -D_LIBCPP_DEBUG_LEVEL=1 -Wall -fno-omit-frame-pointer -fsanitize=address

link_cp: link_cp.cpp ../task.hpp ../io_service.hpp ../promise.hpp ../when.hpp
	clang++ -std=c++17 -g -fcoroutines-ts -stdlib=libc++ -lc++ -lc++abi -luring -lfmt ./link_cp.cpp -I.. -o link_cp -O0 -D_LIBCPP_DEBUG_LEVEL=1 -Wall -fno-omit-frame-pointer -fsanitize=address
