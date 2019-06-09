cmake_minimum_required(VERSION 2.8)

project(liburing-http-demo)

find_package(Boost OPTIONAL_COMPONENTS context)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(USE_LIBAIO "Use libaio instead of liburing for old kernel support")

add_executable(${PROJECT_NAME} "main.cpp" "global.cpp")
target_link_libraries(${PROJECT_NAME} LINK_PRIVATE fmt)

if(USE_LIBAIO)
    add_compile_definitions(USE_LIBAIO=1)
    target_link_libraries(${PROJECT_NAME} LINK_PRIVATE aio)
else()
    target_link_libraries(${PROJECT_NAME} LINK_PRIVATE uring)
endif()
if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIR})
    target_link_libraries(${PROJECT_NAME} LINK_PRIVATE ${Boost_LIBRARIES})
endif()

string(APPEND CMAKE_CXX_FLAGS_DEBUG " -fno-omit-frame-pointer -fsanitize=address -D_GLIBCXX_DEBUG")
string(APPEND CMAKE_LINKER_FLAGS_DEBUG " -fno-omit-frame-pointer -fsanitize=address")
