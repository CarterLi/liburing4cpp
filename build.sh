#!/bin/bash

# Move to the root directory of the project
# build.sh should be in the project root
cd "$(dirname "$0")"

# Build things in a build directory named 'build' in the project root
# Output executables in a directory named 'bin' in the project root
cmake . -B build
cmake --build build
