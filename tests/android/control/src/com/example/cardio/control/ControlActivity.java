package com.example.cardio.control;

import android.app.Activity;
import android.os.Bundle;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/** A plain Activity that exercises the window and input services without JNI. */
public final class ControlActivity extends Activity {
  static final String RECREATE_BEFORE_FOCUS = "com.example.cardio.control.RECREATE_BEFORE_FOCUS";

  // A lifecycle belongs to one test iteration, including replacement instances.
  static final class Lifecycle {
    private final CountDownLatch focused = new CountDownLatch(1);
    private final CountDownLatch finished = new CountDownLatch(1);
    // Activity references and finish requests are only accessed on the UI thread.
    private ControlActivity activity;
    private boolean finishRequested;

    void awaitFocus() throws InterruptedException {
      // This is a deadlock guard; success depends on the window callback.
      if (!focused.await(30, TimeUnit.SECONDS)) {
        throw new AssertionError("Activity did not receive window focus");
      }
    }

    void finish() {
      finishRequested = true;
      if (activity != null) {
        activity.finish();
      }
    }

    void awaitFinished() throws InterruptedException {
      if (!finished.await(30, TimeUnit.SECONDS)) {
        throw new AssertionError("Activity did not finish");
      }
    }
  }

  private static volatile Lifecycle pendingLifecycle;
  private Lifecycle lifecycle;

  static Lifecycle prepareTest() {
    final Lifecycle lifecycle = new Lifecycle();
    pendingLifecycle = lifecycle;
    return lifecycle;
  }

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    lifecycle = pendingLifecycle;
    lifecycle.activity = this;
    if (savedInstanceState == null
        && getIntent().getBooleanExtra(RECREATE_BEFORE_FOCUS, false)) {
      // Keep the first window unfocused until instrumentation requests recreation.
      setVisible(false);
    }
    if (lifecycle.finishRequested) {
      finish();
    }
  }

  @Override
  public void onWindowFocusChanged(boolean hasFocus) {
    super.onWindowFocusChanged(hasFocus);
    if (hasFocus) {
      lifecycle.focused.countDown();
    }
  }

  @Override
  protected void onDestroy() {
    super.onDestroy();
    if (lifecycle.activity == this) {
      lifecycle.activity = null;
    }
    if (isFinishing()) {
      lifecycle.finished.countDown();
    }
  }
}
