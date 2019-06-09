cmake_minimum_required(VERSION 3.9)

project(you-server)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(deps/wsrpc wsrpc EXCLUDE_FROM_ALL)
add_subdirectory(deps/duktape duktape EXCLUDE_FROM_ALL)

add_executable(ysrv src/main.cpp src/lib.cpp)
set_property(TARGET ysrv PROPERTY CXX_STANDARD 20)
set_property(TARGET ysrv PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
target_link_libraries(ysrv rpcws duktape stdc++fs)

add_executable(ysrvctl src/cli.cpp)
set_property(TARGET ysrvctl PROPERTY CXX_STANDARD 20)
set_property(TARGET ysrvctl PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
target_link_libraries(ysrvctl rpcws)
