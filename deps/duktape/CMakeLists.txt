cmake_minimum_required(VERSION 3.3)

add_library(duktape duktape.c)
target_include_directories(duktape PUBLIC .)