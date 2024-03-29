#
# Copyright (c) 2021-present, Trail of Bits, Inc.
# All rights reserved.
#
# This source code is licensed in accordance with the terms specified in
# the LICENSE file found in the root directory of this source tree.
#

cmake_minimum_required(VERSION 3.16)
include("cmake/options.cmake")
include("cmake/vcpkg_helper.cmake")

project(magnifier)

include(GNUInstallDirs)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(PLATFORM_LINUX true)

elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(PLATFORM_MACOS true)

else()
  message(FATAL_ERROR "Unsupported platform")
endif()


include("cmake/llvm.cmake")

option(MAGNIFIER_ENABLE_INSTALL "Set to true to enable the install target" true)
option(MAGNIFIER_ENABLE_UI      "Set to true to enable the magnifier-ui target" OFF)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(Filesystem REQUIRED)


find_llvm(llvm)

add_library(magnifier STATIC
        lib/BitcodeExplorer.cpp
        lib/ISubstitutionObserver.cpp
        lib/IdCommentWriter.cpp
        lib/IdCommentWriter.h
        )

target_compile_features(magnifier PUBLIC cxx_std_20)
set(magnifier_PUBLIC_HEADER_DIR "${PROJECT_SOURCE_DIR}/include/magnifier")
set(magnifier_PUBLIC_HEADERS
        "${magnifier_PUBLIC_HEADER_DIR}/BitcodeExplorer.h"
        "${magnifier_PUBLIC_HEADER_DIR}/Result.h"
        "${magnifier_PUBLIC_HEADER_DIR}/IFunctionResolver.h"
        "${magnifier_PUBLIC_HEADER_DIR}/ISubstitutionObserver.h"
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
target_link_libraries(repl PRIVATE magnifier std::filesystem)
target_include_directories(repl
        PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:include>
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        )

if(MAGNIFIER_ENABLE_UI)
    add_executable(magnifier-ui)
    target_sources(magnifier-ui PRIVATE
            bin/magnifier-ui/main.cpp
            bin/magnifier-ui/DeclPrinter.cpp
            bin/magnifier-ui/StmtPrinter.cpp
            bin/magnifier-ui/TypePrinter.cpp)
    target_link_libraries(magnifier-ui PRIVATE magnifier)
    target_include_directories(magnifier-ui
            PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
            $<INSTALL_INTERFACE:include>
            PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            )

    find_path(UWEBSOCKETS_INCLUDE_DIRS "uwebsockets/App.h")
    target_include_directories(magnifier-ui PRIVATE ${UWEBSOCKETS_INCLUDE_DIRS})

    find_library(USOCKETS_LIB uSockets)
    target_link_libraries(magnifier-ui PRIVATE ${USOCKETS_LIB})

    find_library(UV_LIB uv)
    target_link_libraries(magnifier-ui PRIVATE ${UV_LIB})

    find_package(rellic CONFIG REQUIRED)
    target_link_libraries(magnifier-ui PRIVATE rellic::rellic)
endif(MAGNIFIER_ENABLE_UI)

if(MAGNIFIER_ENABLE_INSTALL)
        export(PACKAGE "${PROJECT_NAME}")
        
        set(cmake_install_dir "lib/cmake/${PROJECT_NAME}")
        
        include(CMakePackageConfigHelpers)
        configure_package_config_file("${PROJECT_NAME}Config.cmake.in"
            "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
            INSTALL_DESTINATION "${cmake_install_dir}"
        )
      
        install(
            FILES
                "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
            DESTINATION "${cmake_install_dir}"
        )
        install(EXPORT "${PROJECT_NAME}Targets"
            DESTINATION "${cmake_install_dir}"
            NAMESPACE "${PROJECT_NAME}::"
        )
        install(
          TARGETS
            "llvm"
          EXPORT
            "${PROJECT_NAME}Targets"
        )
      
  install(
    TARGETS
      magnifier
      repl

    EXPORT
    magnifierTargets

    RUNTIME DESTINATION
      bin

    LIBRARY DESTINATION
      lib

    ARCHIVE DESTINATION
      lib

    INCLUDES DESTINATION
      include

    PUBLIC_HEADER DESTINATION
        "${CMAKE_INSTALL_INCLUDEDIR}/magnifier"
  )
endif(MAGNIFIER_ENABLE_INSTALL)
