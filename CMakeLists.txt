cmake_minimum_required(VERSION 3.20.0)
project(SimpleProject)

include(llvm.cmake)

add_executable(simple-tool tool.cpp SectionSizes.cpp Statistics.cpp)

target_link_libraries(simple-tool llvm)

# 用于dump嵌入式elf里的某个结构体变量
add_executable(StructDump StructDump.cpp)
target_link_libraries(StructDump llvm)

add_executable(TableGen TableGen.cpp)
target_link_libraries(TableGen llvm)
