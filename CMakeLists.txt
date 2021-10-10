cmake_minimum_required(VERSION 2.8)

project(Kaleidoscope)
set(CMAKE_CXX_FLAGS "-O2 -std=c++14 -Wall")

find_package(LLVM REQUIRED CONFIG)

execute_process(COMMAND "llvm-config" "--libs"
        OUTPUT_VARIABLE LLVM_LIBS)
string(REGEX REPLACE "[ \t]*[\r\n]+[ \t]*" "" LLVM_LIBS ${LLVM_LIBS})

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_executable(kaleidoscope main.cpp)
target_link_libraries(kaleidoscope ${LLVM_LIBS})
target_link_libraries(kaleidoscope pthread)
target_link_libraries(kaleidoscope ncurses)

