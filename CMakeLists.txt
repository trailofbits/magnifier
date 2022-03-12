#
# Copyright (c) 2021-present, Trail of Bits, Inc.
# All rights reserved.
#
# This source code is licensed in accordance with the terms specified in
# the LICENSE file found in the root directory of this source tree.
#

cmake_minimum_required(VERSION 3.16)
project(magnifier)

include("cmake/llvm.cmake")

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(Filesystem REQUIRED)


find_llvm(llvm "${LLVM_DIR}")

add_library(magnifier STATIC
        lib/BitcodeExplorer.cpp
        lib/IdCommentWriter.cpp
        lib/IdCommentWriter.h
        )

target_compile_features(magnifier PUBLIC cxx_std_17)
set(magnifier_PUBLIC_HEADER_DIR "${PROJECT_SOURCE_DIR}/include/magnifier")
set(magnifier_PUBLIC_HEADERS
        "${magnifier_PUBLIC_HEADER_DIR}/BitcodeExplorer.h"
        "${magnifier_PUBLIC_HEADER_DIR}/Result.h"
        "${magnifier_PUBLIC_HEADER_DIR}/IFunctionResolver.h"
        "${magnifier_PUBLIC_HEADER_DIR}/ISubstitionObserver.h"
        )
set_target_properties(magnifier
        PROPERTIES
        PUBLIC_HEADER "${magnifier_PUBLIC_HEADERS}"
        LINKER_LANGUAGE CXX
        )

find_package(LLVM REQUIRED CONFIG)
target_link_libraries(magnifier PUBLIC llvm)
target_compile_definitions(magnifier PUBLIC ${LLVM_DEFINITIONS})
target_include_directories(magnifier SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS})
target_link_directories(magnifier PUBLIC ${LLVM_LIBRARY_DIRS})
target_include_directories(magnifier
        PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:include>
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        )

add_executable(repl)
target_sources(repl PRIVATE bin/repl/main.cpp)
target_link_libraries(repl PRIVATE magnifier)
target_include_directories(repl
        PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:include>
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        )
