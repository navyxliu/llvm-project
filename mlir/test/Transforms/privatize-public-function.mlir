// RUN: mlir-opt %s -privatize-public-function -remove-dead-values -split-input-file -verify-diagnostics | FileCheck %s
module {
  // the function signature is immutable because it is public.
  func.func public @public_fn_with_unused_argument(%unused: i32) -> () {
    return
  }
  // CHECK-LABEL: func.func @main
  // CHECK: call @__public_fn_with_unused_argument_privatized() : () -> ()
  func.func @main() -> () {
    %zero = arith.constant 0 : i32
    call @public_fn_with_unused_argument(%zero) : (i32) -> ()
    return
  }

  // CHECK-LABEL: func.func @main2
  // CHECK: call @__public_fn_with_unused_argument_privatized() : () -> ()
  func.func @main2(%arg0: i1) {
    %0 = scf.if %arg0 -> (i32) {
      %c1_i32 = arith.constant 1 : i32
      scf.yield %c1_i32 : i32
    } else {
      %c0_i32 = arith.constant 0 : i32
      scf.yield %c0_i32 : i32
    }

    call @public_fn_with_unused_argument(%0) : (i32) -> ()
    return
  }

  func.func public @fn_return_multiple(%arg0: i32) -> (i32, i32, i32) {
    %one = arith.constant 1 : i32
    %two = arith.constant 2 : i32
    %three = arith.constant 4 : i32

    return %one, %two, %three: i32, i32, i32
  }

  // CHECK-LABEL: func.func @main3
  // CHECK: call @__fn_return_multiple_privatized() : () -> (i32, i32, i32)
  func.func @main3(%arg: i32) -> () {
    %one = arith.constant 1 : i32
    %scalar = arith.addi %arg, %one: i32

    call @fn_return_multiple(%scalar) : (i32) -> (i32, i32, i32)
    return
  }
}
