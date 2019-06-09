cmake_minimum_required(VERSION 3.3)

add_library(duktape duktape.c)
set_source_files_properties(duktape.c PROPERTIES LANGUAGE CXX)
target_include_directories(duktape PUBLIC .)