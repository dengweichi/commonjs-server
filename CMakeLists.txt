cmake_minimum_required(VERSION 3.5.1)
project(commonjs_server)

set(CMAKE_CXX_STANDARD 14)

include_directories(${PROJECT_SOURCE_DIR}/include/v8)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x -pthread -DV8_COMPRESS_POINTERS")
add_executable(commonjs_server src/main.cpp)
target_link_libraries(commonjs_server ${PROJECT_SOURCE_DIR}/deps/v8/libv8_monolith.a ${PROJECT_SOURCE_DIR}/deps/v8/libv8_libplatform.a ${PROJECT_SOURCE_DIR}/deps/v8/libv8_libbase.a)
