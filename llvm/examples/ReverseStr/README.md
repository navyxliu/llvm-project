# Task
Write a plugin of opt and clang that can capture global variables of c-str in a translation unit and reverse the string literals.


```c
$cat t.c
#include <stdio.h>
const char * GLOBAL_CONSTANT_MSG = "hello world @ compile";
int main() {
    puts(GLOBAL_CONSTANT_MSG);
}
```

# Approach
1. write a ModulePass using llvm.
2. idea
    a. iterates all global variables of a module;
    b. filter out whose type of initializer is an array of i8.
    c. get the contents of initializer.
    d. create a new Constant. the contents are the reversed string literal. replace the old initializer with the new Constant.

# Demo
```bash
$clang -emit-llvm -S t.c
$./bin/lli ./t.ll
hello world @ compile

$./bin/opt -load-pass-plugin=./lib/ReverseStr.so -passes=ReverseStr -debug ./t.ll > t.after.bc
Args: ./bin/opt -load-pass-plugin=./lib/ReverseStr.so -passes=ReverseStr -debug ./t.ll
global variable: @.str = private unnamed_addr constant [22 x i8] c"hello world @ compile\00", align 1
initializer: 0x556208a997a0
original c-string literal : hello world @ compile
after updated GV: @.str = private unnamed_addr constant [22 x i8] c"elipmoc @ dlrow olleh\00", align 1
global variable: @GLOBAL_CONSTANT_MSG = dso_local global ptr @.str, align 8

./bin/llvm-dis < t.after.bc| grep ^@
@.str = private unnamed_addr constant [22 x i8] c"elipmoc @ dlrow olleh\00", align 1
@GLOBAL_CONSTANT_MSG = dso_local global ptr @.str, align 8

$./bin/lli < t.after.bc
elipmoc @ dlrow olleh
```
