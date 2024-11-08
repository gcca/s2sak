cmake_minimum_required(VERSION 3.30)
project(s2sak VERSION 0.1.0 LANGUAGES CXX)
option(S2SAK_DISABLE_TESTS "Disable tests" OFF)
find_package(Boost REQUIRED COMPONENTS program_options)
add_executable(s2sak s2sak.cc)
target_compile_options(s2sak PRIVATE -Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wnon-virtual-dtor -Wold-style-cast -Wfloat-equal -Wformat=2 -Wnull-dereference -Wundef -Wuninitialized -Wcast-align -Wformat-security -Wstrict-overflow -Wswitch-enum -Wunused-parameter -Wpointer-arith -Wcast-align -Wno-variadic-macros -fexceptions)

target_link_libraries(s2sak PRIVATE Boost::program_options)
