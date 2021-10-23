cmake_minimum_required(VERSION 2.8)

project(Kaleidoscope)
set(CMAKE_CXX_FLAGS "-Wall -rdynamic -std=c++14   -fno-exceptions -fno-rtti -D_GNU_SOURCE -D_DEBUG -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS")

find_package(LLVM REQUIRED CONFIG)

execute_process(COMMAND "llvm-config" "--libs" "core" "native" "orcjit" OUTPUT_VARIABLE LLVM_LIBS)
string(REGEX REPLACE "[ \t]*[\r\n]+[ \t]*" "" LLVM_LIBS ${LLVM_LIBS})


execute_process(COMMAND "llvm-config" "--system-libs" OUTPUT_VARIABLE  LLVM_SYSTEM_LIBS)
string(REGEX REPLACE "[ \t]*[\r\n]+[ \t]*" "" LLVM_SYSTEM_LIBS ${LLVM_SYSTEM_LIBS})


message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "llvm libs: ${LLVM_LIBS}")
message(STATUS "llvm sytem libs: ${LLVM_SYSTEM_LIBS}")

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_executable(kaleidoscope main.cpp lexer.cpp expressions.cpp codegen.cpp)
target_link_libraries(kaleidoscope ${LLVM_LIBS})
target_link_libraries(kaleidoscope ${LLVM_SYSTEM_LIBS})
target_link_libraries(kaleidoscope ncurses)

include_directories(kaleidoscope "./")
