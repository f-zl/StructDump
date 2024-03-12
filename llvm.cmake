# https://llvm.org/docs/CMake.html#embedding-llvm-in-your-project

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# wrap llvm flags in a single interface library
# other targets only need to link to this `llvm` library to add necessary flags
add_library(llvm INTERFACE)
target_include_directories(llvm INTERFACE ${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
target_compile_definitions(llvm INTERFACE ${LLVM_DEFINITIONS_LIST})

# libs return by llvm_map_components_to_libnames doesn't exist on Arch Linux and msys2
# `llvm-config --libs` works
execute_process(COMMAND llvm-config --libs
                OUTPUT_VARIABLE LLVM_LIBS
                OUTPUT_STRIP_TRAILING_WHITESPACE)
# LLVM_LIBS has "-l", which works for target_link_libraries
target_link_libraries(llvm INTERFACE ${LLVM_LIBS})
