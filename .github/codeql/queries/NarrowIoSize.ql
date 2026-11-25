/**
 * @name Unchecked native I/O length conversion
 * @description Passing a wide length to a narrower native I/O parameter can truncate the request.
 * @kind problem
 * @problem.severity warning
 * @precision high
 * @id cardio/narrow-io-size
 * @tags reliability
 */

import cpp
import semmle.code.cpp.controlflow.Guards
import semmle.code.cpp.rangeanalysis.SimpleRangeAnalysis
import semmle.code.cpp.rangeanalysis.RangeAnalysisUtils
import semmle.code.cpp.valuenumbering.GlobalValueNumbering

private int lengthIndex(Function f) {
  f.hasGlobalName(["ReadFile", "WriteFile"]) and result = 2
  or
  f.hasGlobalName(["io_uring_prep_read", "io_uring_prep_write"]) and result = 3
}

private predicate sizeCheck(FunctionCall check) {
  check.getTarget().hasQualifiedName("cardio::win32::internal", "is_valid_buffer_size") or
  check.getTarget().hasQualifiedName("cardio::internal", "is_valid_io_uring_buffer_size")
}

private predicate checkedAt(Expr value, Expr point) {
  exists(FunctionCall check |
    sizeCheck(check) and
    globalValueNumber(check.getArgument(0)) = globalValueNumber(value) and
    check.(GuardCondition).controls(point.getBasicBlock(), true)
  )
}

private predicate checkedCapture(Expr value, FunctionCall call) {
  exists(LambdaExpression lambda, LambdaCapture capture |
    lambda.getLambdaFunction() = call.getEnclosingFunction() and
    capture = lambda.getACapture() and
    not capture.isCapturedByReference() and
    (
      value.(VariableAccess).getTarget() = capture.getField() or
      value.(VariableAccess).getTarget() = capture.getInitializer().(VariableAccess).getTarget()
    ) and
    checkedAt(capture.getInitializer(), lambda)
  )
}

// A wrapper can store the converted count in a local before calling the OS.
// Follow reaching definitions so an earlier, overwritten value is ignored.
private Expr wideLength(Expr arg, IntegralType native) {
  if arg.getUnconverted().getUnspecifiedType().(IntegralType).getSize() > native.getSize()
  then result = arg.getUnconverted()
  else
    exists(SsaDefinition def, StackVariable local |
      def.getAUse(local) = arg.getUnconverted() and
      result = wideLength(def.getAnUltimateDefiningValue(local), native)
    )
}

from FunctionCall call, int i, Expr size, IntegralType native
where
  i = lengthIndex(call.getTarget()) and
  native = call.getTarget().getParameter(i).getUnspecifiedType() and
  size = wideLength(call.getArgument(i), native) and
  upperBound(size) > typeUpperBound(native) and
  not checkedAt(size, size) and
  not checkedAt(size, call) and
  not checkedCapture(size, call)
select call, "Check the buffer length before passing it to this narrower native I/O parameter."
