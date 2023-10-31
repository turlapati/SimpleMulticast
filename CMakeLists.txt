cmake_minimum_required(VERSION 3.26)
project(Multicast2 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++ -static")
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
set(BUILD_SHARED_LIBS OFF)
add_executable(SndMulCast SndMulCast.c)
add_executable(RcvMulCast RcvMulCast.c)