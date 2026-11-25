/**
 * @name Dispatcher accessed without its lifetime lock
 * @description A delayed dispatcher access must hold the mutex protecting its lifetime.
 * @kind problem
 * @problem.severity warning
 * @precision high
 * @id cardio/unlocked-dispatcher
 * @tags reliability
 */

import cpp
import semmle.code.cpp.dataflow.new.DataFlow
import semmle.code.cpp.valuenumbering.GlobalValueNumbering

private predicate lifetimePointer(FieldAccess pointer) {
  pointer.getTarget().hasQualifiedName("cardio::internal", "dispatcher_lifetime", "target") or
  pointer.getTarget().hasQualifiedName("cardio::internal", "dispatcher_group_lifetime", "group")
}

// Accessing a shared_ptr twice calls operator-> twice, but the mutex and
// dispatcher still belong to the same token.
private Expr tokenObject(Expr e) {
  if
    e.(FunctionCall).getTarget().hasName(["operator->", "get"]) and
    e.(FunctionCall)
        .getQualifier()
        .getType()
        .stripType()
        .(Class)
        .hasQualifiedName("std", "shared_ptr")
  then result = e.(FunctionCall).getQualifier()
  else result = e
}

private predicate sameToken(Expr a, Expr b) {
  globalValueNumber(tokenObject(a)) = globalValueNumber(tokenObject(b))
  or
  exists(LocalScopeVariable v |
    tokenObject(a) = v.getAnAccess() and
    tokenObject(b) = v.getAnAccess() and
    not v.getAnAccess().isModified()
  )
  or
  exists(FieldAccess x, FieldAccess y |
    x = tokenObject(a) and
    y = tokenObject(b) and
    x.getTarget() = y.getTarget() and
    sameToken(x.getQualifier(), y.getQualifier())
  )
}

// A lock_guard remains alive in handlers of a later try statement in the same
// block. The exception handler can have a separate entry in the control-flow graph.
private predicate inGuardScope(DeclStmt decl, Expr access) {
  exists(BlockStmt block, int start, int current |
    block.getStmt(start) = decl and
    block.getStmt(current) = access.getEnclosingStmt().getParentStmt*() and
    decl.getEnclosingFunction() = access.getEnclosingFunction() and
    start < current
  )
}

private predicate guarded(FieldAccess pointer, FunctionCall use) {
  exists(DeclStmt decl, LocalVariable guard, ConstructorCall ctor, FieldAccess mutex |
    decl.getADeclaration() = guard and
    guard
        .getType()
        .getUnspecifiedType()
        .(Class)
        .hasQualifiedName("std", ["lock_guard", "scoped_lock"]) and
    ctor = guard.getInitializer().getExpr().getUnconverted() and
    mutex = ctor.getAnArgument() and
    mutex.getTarget().hasName("mutex") and
    sameToken(mutex.getQualifier(), pointer.getQualifier()) and
    inGuardScope(decl, pointer) and
    inGuardScope(decl, use)
  )
}

from FunctionCall call, FieldAccess pointer
where
  DataFlow::localExprFlow(pointer, call.getQualifier()) and
  (
    lifetimePointer(pointer) and not guarded(pointer, call)
    or
    // The original regression bypassed the lifetime token altogether.
    pointer.getTarget().hasQualifiedName("cardio::internal", "promise_state_base", "group") and
    call.getEnclosingFunction().hasQualifiedName("cardio::internal", "finish_promise")
  )
select call, "Hold the mutex of this lifetime token while using its dispatcher."
