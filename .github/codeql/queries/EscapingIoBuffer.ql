/**
 * @name Asynchronous I/O outlives a local buffer
 * @description An asynchronous operation returned to the caller borrows a buffer with automatic storage duration.
 * @kind problem
 * @problem.severity warning
 * @precision high
 * @id cardio/escaping-io-buffer
 * @tags reliability
 */

import cpp
import semmle.code.cpp.controlflow.DefinitionsAndUses
import semmle.code.cpp.dataflow.new.DataFlow

private Expr bufferArgument(FunctionCall call) {
  exists(int i |
    call.getTarget()
        .hasQualifiedName("cardio::" + ["win32", "iocps", "io_urings"], ["read", "write"]) and
    call.getTarget().getParameter(i).getUnspecifiedType().(Class).hasQualifiedName("std", "span") and
    result = call.getArgument(i)
  )
}

// Follow storage through span construction and views, but not through a
// namespace-unrelated function that might copy the data into another owner.
private Expr borrowedStorage(Expr view) {
  exists(ConstructorCall ctor |
    ctor = view.getUnconverted() and
    ctor.getTarget().getDeclaringType().hasQualifiedName("std", "span") and
    result = ctor.getArgument(0)
  )
  or
  exists(FunctionCall getter |
    getter = view.getUnconverted() and
    getter
        .getTarget()
        .hasQualifiedName("std", ["array", "span"], ["data", "subspan", "first", "last"]) and
    result = getter.getQualifier()
  )
  or
  exists(FunctionCall bytes |
    bytes = view.getUnconverted() and
    bytes.getTarget().hasQualifiedName("std", ["as_bytes", "as_writable_bytes"]) and
    result = bytes.getArgument(0)
  )
  or
  exists(LocalVariable local |
    view.getUnconverted() = local.getAnAccess() and
    local.getType().getUnspecifiedType().(Class).hasQualifiedName("std", "span") and
    result = local.getInitializer().getExpr() and
    definitionUsePair(local, result, view.getUnconverted())
  )
}

private predicate returnsOperation(FunctionCall call, ReturnStmt ret) {
  DataFlow::localExprFlow(call, ret.getExpr())
  or
  exists(LocalVariable owner |
    owner.getInitializer().getExpr().getUnconverted() = call and
    owner.getAnAccess() = ret.getExpr().getUnconverted().(ConstructorCall).getArgument(0)
  )
}

from FunctionCall call, LocalVariable storage, ReturnStmt ret
where
  storage instanceof StackVariable and
  (
    storage.getType().getUnspecifiedType() instanceof ArrayType or
    storage.getType().getUnspecifiedType().(Class).hasQualifiedName("std", "array")
  ) and
  storage.getAnAccess() = borrowedStorage*(bufferArgument(call)).getUnconverted() and
  returnsOperation(call, ret)
select call, "Keep the buffer alive until this I/O operation has completed."
