#
# Copyright (c) 2021-present, Trail of Bits, Inc.
# All rights reserved.
#
# This source code is licensed in accordance with the terms specified in
# the LICENSE file found in the root directory of this source tree.
#

cmake_minimum_required(VERSION 3.16)
project(magnifier)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(Filesystem REQUIRED)

add_library(magnifier STATIC
        magnifier/BitcodeExplorer.cpp
        magnifier/BitcodeExplorer.h
        magnifier/IDCommentWriter.cpp
        magnifier/IDCommentWriter.h
        )
target_compile_features(magnifier PUBLIC cxx_std_17)

find_package(LLVM REQUIRED CONFIG)
target_link_libraries(magnifier PUBLIC LLVM)
target_compile_definitions(magnifier PUBLIC ${LLVM_DEFINITIONS})
target_include_directories(magnifier SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS})
target_link_directories(magnifier PUBLIC ${LLVM_LIBRARY_DIRS})


add_executable(repl)
target_sources(repl PRIVATE repl/main.cpp)
target_link_libraries(repl PRIVATE magnifier)

