; RUN: opt %loadnewpmreversestr -passes=ReverseStr < %s -S | FileCheck %s

; CHECK: c"elipmoc @ dlrow olleh\00"
@.str = private unnamed_addr constant [22 x i8] c"hello world @ compile\00", align 1
@GLOBAL_CONSTANT_MSG = dso_local global i8* getelementptr inbounds ([22 x i8], [22 x i8]* @.str, i32 0, i32 0), align 8
