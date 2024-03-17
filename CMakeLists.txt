cmake_minimum_required(VERSION 3.20.0)
project(StructDump)

include(llvm.cmake)

# print a C struct variable in an elf
add_executable(StructDump StructDump.cpp)
target_link_libraries(StructDump llvm)

# similar to StructDump, generate code of a GUI config table
# has nothing to do with the LLVM TableGen library
add_executable(TableGen TableGen.cpp)
target_link_libraries(TableGen llvm)
