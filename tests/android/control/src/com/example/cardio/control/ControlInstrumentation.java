package com.example.cardio.control;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.system.Os;
import android.system.OsConstants;
import java.io.PrintWriter;
import java.io.StringWriter;

/** Runs eight Activity lifecycles, optionally recreating each before accepting focus. */
public final class ControlInstrumentation extends Instrumentation {
  private Bundle arguments;

  @Override
  public void onCreate(Bundle arguments) {
    super.onCreate(arguments);
    this.arguments = arguments;
    start();
  }

  @Override
  public void onStart() {
    final Bundle results = new Bundle();
    final boolean recreate = "true".equals(arguments.getString("recreate"));
    final String testName = recreate
        ? "android_activity_control_recreation_test" : "android_activity_control_test";
    int resultCode = Activity.RESULT_CANCELED;
    ControlActivity.Lifecycle lifecycle = null;

    try {
      final int expectedApi = Integer.parseInt(arguments.getString("api"));
      final long expectedPageSize = Long.parseLong(arguments.getString("page_size"));
      final long pageSize = Os.sysconf(OsConstants._SC_PAGESIZE);
      if (Build.VERSION.SDK_INT != expectedApi || pageSize != expectedPageSize) {
        throw new AssertionError(
            "Unexpected device: API " + Build.VERSION.SDK_INT + ", page size " + pageSize);
      }

      for (int iteration = 0; iteration < 8; ++iteration) {
        lifecycle = ControlActivity.prepareTest();
        final Intent intent = new Intent(Intent.ACTION_MAIN);
        intent.setClassName(getTargetContext(), ControlActivity.class.getName());
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        intent.putExtra(ControlActivity.RECREATE_BEFORE_FOCUS, recreate);
        final Activity started = startActivitySync(intent);
        if (recreate) {
          runOnMainSync(new Runnable() {
            @Override
            public void run() {
              started.recreate();
            }
          });
        }
        lifecycle.awaitFocus();

        final ControlActivity.Lifecycle target = lifecycle;
        runOnMainSync(new Runnable() {
          @Override
          public void run() {
            target.finish();
          }
        });
        lifecycle.awaitFinished();
        waitForIdleSync();
        lifecycle = null;
      }

      results.putString(REPORT_KEY_STREAMRESULT,
          testName + ": PASS (API " + Build.VERSION.SDK_INT
              + ", page size " + pageSize + ", 8 lifecycles)\n");
      resultCode = Activity.RESULT_OK;
    } catch (Throwable error) {
      final StringWriter trace = new StringWriter();
      error.printStackTrace(new PrintWriter(trace));
      results.putString(REPORT_KEY_STREAMRESULT,
          testName + ": FAIL\n" + trace.toString());
    } finally {
      if (lifecycle != null) {
        final ControlActivity.Lifecycle target = lifecycle;
        runOnMainSync(new Runnable() {
          @Override
          public void run() {
            target.finish();
          }
        });
      }
    }
    finish(resultCode, results);
  }
}
