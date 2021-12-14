cmake_minimum_required(VERSION 3.16)
project(magnifier)

set(CMAKE_CXX_STANDARD 17)

find_package(LLVM REQUIRED CONFIG)
add_definitions(${LLVM_DEFINITIONS})
include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})



add_executable(${PROJECT_NAME} magnifier/BitcodeExplorer.cpp magnifier/BitcodeExplorer.h magnifier/IDCommentWriter.cpp magnifier/IDCommentWriter.h)

target_sources(${PROJECT_NAME} PRIVATE ${PROJECT_NAME}/main.cpp)


target_link_libraries(${PROJECT_NAME} PRIVATE LLVM)
