cmake_minimum_required(VERSION 3.14)  # CMake version check

project("liburing4cpp")
set(libname "liburing4cpp")
set(CMAKE_CXX_STANDARD 20)
add_library("${libname}" INTERFACE)

#######################
## Find dependencies ##
#######################
# Find a threads library
find_package(Threads REQUIRED)

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

###################
## Configuration ##
###################

# This creates a file compile_commands.json in the build directory
# That contains the compile commands used to actually compile the project
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Use pthreads as the Threads library, if availible
set(THREADS_PREFER_PTHREAD_FLAG ON)

########################
## Define the library ##
########################

target_include_directories(
    ${libname}
    INTERFACE
    include/)

# The library has fmt, uring, and Threads::Threads as dependencies
target_link_libraries(
    ${libname}
    INTERFACE
    fmt uring Threads::Threads)

##############################################
## Build demo when the project is top level ##
##############################################

# NB: If this project is included as a dependency as another project,
# Then demo won't get built

if (PROJECT_IS_TOP_LEVEL)
    note("${PROJECT_NAME} is top level. Building demo projects")
    # Target all the source files in demo
    add_source_dir("demo" ${libname})

    include(CTest)

    add_test_dir("tests" ${libname})
endif()
