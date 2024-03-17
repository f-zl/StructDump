# StructDump

## Requirement

Given an elf and a C struct global variable, print the  members' name, type, offset, size, and theoretically initial value\
This code can be modified to do tasks like generating GUI table config, generating XCP A2L file

## Implementation

All the type information is in the elf's .debug_info section in DWARF format. The LLVM library is used to inspect .debug_info. The initial value can also be derived by inspecting the .data section (not done).

The [LLVM document](https://llvm.org/docs/CMake.html#embedding-llvm-in-your-project) provides a example CMakeLists. But during linking the libraries `-lLLVMCore -lLLVMIRReader` cannot be found in Arch Linux and msys2. In the [PKGBUILD](<https://gitlab.archlinux.org/archlinux/packaging/packages/llvm>), the LLVM static libraries goes to llvm-libs package in a single libLLVM.so. The flags provided by `llvm-config --cxxflags`，`llvm-config --libs` work.

The code of this repo is based on [llvm-dwarfdump](https://github.com/llvm/llvm-project/tree/main/llvm/tools/llvm-dwarfdump).

## Usage

`StructDump <elf> <variable>`

Example:
```c
// example.c
#include <stdint.h>
typedef struct PidTag{
  float p;
  float i;
  float d;
} Pid;
typedef uint8_t uint8;
typedef struct {
  uint8 u8;
  int32_t i32;
  float f32;
  float mapX[5];
  float mapY[5];
  Pid pid;
} MyStruct;
MyStruct g_param = {.u8 = 1,
                    .i32 = -1,
                    .f32 = 5.5f,
                    .mapX = {1.0f, 1.5f, 2.0f, 2.5f, 3.0f},
                    .mapY = {-10.0f, -9.5f, -9.0f, -8.5f, -8.0f},
                    .pid = {.p = 0.25f, .i = 0.5f, .d = 0.75f}};
```

```sh
gcc -g -c example.c
StructDump example.o g_param
```
Output:
```
type of g_param is MyStruct
struct (anonymous) size 64
-member name u8 offset 0
--typedef: uint8 -> uint8_t -> __uint8_t
---base type unsigned char size 1
-member name i32 offset 4
--typedef: int32_t -> __int32_t
---base type int size 4
-member name f32 offset 8
--base type float size 4
-member name mapX offset 12
--array of float length 5
-member name mapY offset 32
--array of float length 5
-member name pid offset 52
--typedef: Pid
---struct PidTag size 12
----member name p offset 0
-----base type float size 4
----member name i offset 4
-----base type float size 4
----member name d offset 8
-----base type float size 4
```