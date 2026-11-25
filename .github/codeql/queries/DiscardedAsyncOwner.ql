/**
 * @name Discarded cardio asynchronous owner
 * @description Destroying a temporary promise or cancellation registration can abandon pending work.
 * @kind problem
 * @problem.severity warning
 * @precision high
 * @id cardio/discarded-async-owner
 * @tags reliability
 */

import cpp

private predicate cannotOwnPendingWork(FunctionCall call) {
  // The source owns the operation. The returned promise observes its result.
  call.getTarget().hasQualifiedName("cardio", "promise_source", "get_promise")
  or
  call.getTarget().hasQualifiedName("cardio", ["resolved", "rejected"])
  or
  // A negative descriptor is rejected before a readiness wait is registered.
  call.getTarget().hasQualifiedName("cardio", "from_fd") and
  call.getArgument(0).getValue().toInt() < 0
}

from FunctionCall call, ExprStmt statement
where
  call.getTarget()
      .getType()
      .getUnspecifiedType()
      .(Class)
      .hasQualifiedName("cardio", ["promise", "cancellation_registration"]) and
  statement.getExpr().getUnconverted() = call and
  not cannotOwnPendingWork(call)
select call,
  "Keep the returned owner alive until the operation or registration is no longer needed."
