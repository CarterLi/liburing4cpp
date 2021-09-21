cmake_minimum_required(VERSION 3.14)  # CMake version check

project("liburing4cpp")
set(libname "liburing4cpp")
set(CMAKE_CXX_STANDARD 20)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Use pthreads, if availible
set(THREADS_PREFER_PTHREAD_FLAG ON)

add_library("${libname}" INTERFACE)

target_include_directories(${libname} INTERFACE include/)

# Defines helper functions, like find_or_fetch
include(FetchContent)
include(functions.cmake)

find_or_fetch(
    fmt                                 # Package Name
    https://github.com/fmtlib/fmt.git   # Repository
    master)                             # Branch (or commit or tag)

# find_or_fetch adds stuff to a list called remote_dependencies
note("Remote dependencies: ${remote_dependencies}")
FetchContent_MakeAvailable(${remote_dependencies})

find_package(Threads)
# Target all the source files in demo
add_source_dir("demo" ${libname} fmt uring Threads::Threads)

target_sources(file_server PRIVATE "demo/demo-sources/mime_dicts.cpp")
target_include_directories(file_server PRIVATE "demo/demo-includes")
