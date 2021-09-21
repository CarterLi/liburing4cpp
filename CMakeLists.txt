cmake_minimum_required(VERSION 3.14)  # CMake version check

project("liburing 4 cpp")
set(libname "liburing4cpp")
set(CMAKE_CXX_STANDARD 20)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Use pthreads, if availible
set(THREADS_PREFER_PTHREAD_FLAG ON)

add_library("${libname}" INTERFACE)

target_include_directories(${libname} INTERFACE include/)

include(functions.cmake)


find_package(Threads)
# Target all the source files in demo
add_source_dir("demo/" ${libname} fmt uring Threads::Threads)
