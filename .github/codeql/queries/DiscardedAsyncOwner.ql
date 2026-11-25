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

from FunctionCall call, ExprStmt statement
where
  call.getTarget()
      .getType()
      .getUnspecifiedType()
      .(Class)
      .hasQualifiedName("cardio", ["promise", "cancellation_registration"]) and
  statement.getExpr().getUnconverted() = call
select call,
  "Keep the returned owner alive until the operation or registration is no longer needed."
