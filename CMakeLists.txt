cmake_minimum_required(VERSION 3.30)
project(s2sak VERSION 0.1.0 LANGUAGES CXX)

option(S2SAK_DISABLE_TESTS "Disable tests" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Boost REQUIRED COMPONENTS system json program_options)
find_package(PostgreSQL REQUIRED)
find_package(MySQL REQUIRED)

add_executable(s2sak s2sak.cc)
target_compile_options(s2sak PRIVATE -Wall -Wextra -Werror -Wpedantic -Wshadow -Weverything -Wconversion -Wsign-conversion -Wnon-virtual-dtor -Wold-style-cast -Wfloat-equal -Wformat=2 -Wnull-dereference -Wundef -Wuninitialized -Wcast-align -Wformat-security -Wstrict-overflow -Wswitch-enum -Wunused-variable -Wunused-parameter -Wpointer-arith -Wcast-align -Wno-variadic-macros -fexceptions -fsafe-buffer-usage-suggestions -Wno-c++98-compat -Wno-padded -Wno-covered-switch-default -Wno-unsafe-buffer-usage)
target_link_libraries(s2sak PRIVATE Boost::system Boost::json Boost::program_options PostgreSQL::PostgreSQL MySQL::MySQL)

add_executable(n2sak n2sak.cc)
target_compile_options(n2sak PRIVATE -Wall -Wextra -Werror -Wpedantic -Wshadow -Weverything -Wconversion -Wsign-conversion -Wnon-virtual-dtor -Wold-style-cast -Wfloat-equal -Wformat=2 -Wnull-dereference -Wundef -Wuninitialized -Wcast-align -Wformat-security -Wstrict-overflow -Wswitch-enum -Wunused-variable -Wunused-parameter -Wpointer-arith -Wcast-align -Wno-variadic-macros -fexceptions -fsafe-buffer-usage-suggestions -Wno-c++98-compat -Wno-padded -Wno-covered-switch-default -Wno-unsafe-buffer-usage)
target_link_libraries(n2sak PRIVATE Boost::system Boost::json Boost::program_options PostgreSQL::PostgreSQL MySQL::MySQL)
