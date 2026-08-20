package com.example.cardio;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Intent;
import android.os.Bundle;
import java.io.PrintWriter;
import java.io.StringWriter;

public final class CardioInstrumentation extends Instrumentation {
  @Override
  public void onCreate(Bundle arguments) {
    super.onCreate(arguments);
    start();
  }

  @Override
  public void onStart() {
    final Bundle results = new Bundle();
    int resultCode = Activity.RESULT_CANCELED;
    Activity activity = null;

    try {
      for (int iteration = 0; iteration < 8; ++iteration) {
        CardioActivity.prepareTest();
        final Intent intent = new Intent(Intent.ACTION_MAIN);
        intent.setClassName(
            getTargetContext(), CardioActivity.class.getName());
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        activity = startActivitySync(intent);

        final String failure = CardioActivity.awaitTest();
        if (failure != null) {
          throw new AssertionError(
              "iteration " + iteration + ": " + failure);
        }

        final Activity target = activity;
        runOnMainSync(new Runnable() {
          @Override
          public void run() {
            target.finish();
          }
        });
        waitForIdleSync();
        activity = null;
      }

      results.putString(
          REPORT_KEY_STREAMRESULT, "cardio_android_auto_test: PASS\n");
      resultCode = Activity.RESULT_OK;
    } catch (Throwable error) {
      final StringWriter trace = new StringWriter();
      error.printStackTrace(new PrintWriter(trace));
      results.putString(
          REPORT_KEY_STREAMRESULT,
          "cardio_android_auto_test: FAIL\n" + trace.toString());
    } finally {
      if (activity != null) {
        final Activity target = activity;
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
