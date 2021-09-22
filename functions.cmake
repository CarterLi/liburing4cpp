################################################
## Defaults, Definitions and helper functions ##
################################################

# PROJECT_IS_TOP_LEVEL is a variable added in CMake 3.21 that checks if the
# current project is the top-level project. This checks if it's been defined,
# and if not, it defines it.
if(NOT DEFINED PROJECT_IS_TOP_LEVEL)
    if("${CMAKE_PROJECT_NAME}" STREQUAL "${PROJECT_NAME}")
        set(PROJECT_IS_TOP_LEVEL ON)
    else()
        set(PROJECT_IS_TOP_LEVEL OFF)
    endif()
endif()

# Defines some useful constants representing terminal codes to print things
# in color.
if(NOT WIN32)
  string(ASCII 27 Esc)
  set(ColorReset  "${Esc}[m")
  set(ColorBold   "${Esc}[1m")
  set(Red         "${Esc}[31m")
  set(Green       "${Esc}[32m")
  set(Yellow      "${Esc}[33m")
  set(Blue        "${Esc}[34m")
  set(Magenta     "${Esc}[35m")
  set(Cyan        "${Esc}[36m")
  set(White       "${Esc}[37m")
  set(BoldRed     "${Esc}[1;31m")
  set(BoldGreen   "${Esc}[1;32m")
  set(BoldYellow  "${Esc}[1;33m")
  set(BoldBlue    "${Esc}[1;34m")
  set(BoldMagenta "${Esc}[1;35m")
  set(BoldCyan    "${Esc}[1;36m")
  set(BoldWhite   "${Esc}[1;37m")
endif()

# Define a function 'note' that prints a message in bold cyan
function(note msg)
    message("🐈 ${BoldCyan}says: ${msg}${ColorReset}")
endfunction()

####################################################
## Sec. 2: Dependency Management via FetchContent ##
####################################################

set(remote_dependencies "")

# If ALWAYS_FETCH is ON, then find_or_fetch will always fetch any remote
# dependencies rather than using the ones provided by the system. This is
# useful for creating a static executable.
option(
    ALWAYS_FETCH
    "Tells find_or_fetch to always fetch packages"
    OFF)


include(FetchContent)
# find_or_fetch will search for a system installation of ${package} via
# find_package. If it fails to find one, it'll use FetchContent to download and
# build it locally.
function(find_or_fetch package repo tag)
    if (NOT ALWAYS_FETCH)
        find_package(${package} QUIET)
    endif()

    if (ALWAYS_FETCH OR NOT ${${package}_FOUND})
        note("Fetching dependency '${package}' from ${repo}")
        include(FetchContent)
        FetchContent_Declare(
            "${package}"
            GIT_REPOSITORY "${repo}"
            GIT_TAG "${tag}"
        )
        list(APPEND remote_dependencies "${package}")
        set (remote_dependencies  ${remote_dependencies} PARENT_SCOPE)
    else()
        note("Using system cmake package for dependency '${package}'")
    endif()
endfunction()

#####################################################################
## Sec. 3: Convinience Functions to add targets more automatically ##
#####################################################################


# Adds every top-level .cpp file in the given directory as an executable. Arguments
# provided after the directory name are interpreted as libraries, and it'll link
# targets in that directory against those libraries.
function(add_source_dir dir)
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${dir}")
        file(GLOB all_targets "${dir}/*.cpp")
        string(REPLACE ";" ", " library_list "${ARGN}")
        foreach(filename ${all_targets})
            get_filename_component(target ${filename} NAME_WLE)
            note("Adding '${target}' from ${dir}/${target}.cpp with libraries ${library_list}")
            add_executable("${target}" "${filename}")
            target_link_libraries("${target}" PRIVATE ${ARGN})
        endforeach()
    else()
        note("add_source_dir: Skipping ${dir}. Directory not found.")
    endif()
endfunction()

# Adds every top-level .cpp file in the given directory as an executable. Arguments
# provided after the directory name are interpreted as libraries, and it'll link
# targets in that directory against those libraries. Each target will also be
# registered as a test via CTest
function(add_test_dir dir)
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${dir}")
        include(CTest)
        file(GLOB all_targets "${dir}/*.cpp")
        string(REPLACE ";" ", " library_list "${ARGN}")
        foreach(filename ${all_targets})
            get_filename_component(target ${filename} NAME_WLE)
            note("Adding test '${target}' from ${dir}/${target}.cpp with libraries ${library_list}")
            add_executable("${target}" "${filename}")
            target_link_libraries("${target}" PRIVATE ${ARGN})
            add_test(NAME "${target}" COMMAND "${target}")
        endforeach()
    else()
        note("add_test_dir: Skipping ${dir}. Directory not found.")
    endif()
endfunction()


# Targets C++20 for a given target. also adds additional compiler options
# in order to ensure greater levels of compatibility.
function(target_cpp_20 target_name)
    target_compile_features(${target_name} INTERFACE cxx_std_20)

    # The /EHa flag enables standard C++ stack unwinding
    # See: https://docs.microsoft.com/en-us/cpp/build/reference/eh-exception-handling-model?view=msvc-160
    if (MSVC)
        target_compile_options(${target_name} INTERFACE "/EHa")
    endif()

    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        # This definition is needed b/c the <coroutines> header needs it in order
        # to work on clang
        target_compile_definitions(${target_name} INTERFACE __cpp_impl_coroutine=1)
    endif()

    # Enables GCC support for coroutines (these are standard C++ now but GCC still
    # requires a flag for them)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target_name} INTERFACE "-fcoroutines")
    endif()
endfunction()


function(add_submodules libname dir)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${dir}")
        file(GLOB all_modules "${dir}/*")
        foreach(module_dir ${all_modules})
            get_filename_component(module ${module_dir} NAME)
            note("Linked ${module} @ ${dir}/${module}")
            add_subdirectory("${dir}/${module}")
            target_link_libraries(${libname} INTERFACE ${module})
        endforeach()
    endif()
endfunction()
