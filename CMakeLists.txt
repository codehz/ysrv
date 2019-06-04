cmake_minimum_required(VERSION 3.3)

project(you-server)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(deps/wsrpc wsrpc EXCLUDE_FROM_ALL)
add_subdirectory(deps/duktape duktape EXCLUDE_FROM_ALL)

add_executable(youserver src/main.cpp src/lib.cpp)
target_link_libraries(youserver rpcws duktape)
set_property(TARGET youserver PROPERTY CXX_STANDARD 17)

add_executable(youserverctl src/cli.cpp)
set_property(TARGET youserverctl PROPERTY CXX_STANDARD 17)
target_link_libraries(youserverctl rpcws)
