package com.example.cardio;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

public final class CardioActivity extends Activity {
  private static CountDownLatch completion;
  private static volatile String failure;

  private Handler handler;

  static {
    System.loadLibrary("cardio_android_auto_test");
  }

  static synchronized void prepareTest() {
    completion = new CountDownLatch(1);
    failure = null;
  }

  static String awaitTest() throws InterruptedException {
    if (completion == null) {
      return "test completion was not initialized";
    }
    if (!completion.await(30, TimeUnit.SECONDS)) {
      return "timed out waiting for Java UI Looper test";
    }
    return failure;
  }

  private native void nativeStart();

  private native void nativeHandlerRan();

  private native void nativeStop();

  @Override
  protected void onCreate(Bundle state) {
    super.onCreate(state);
    handler = new Handler(Looper.getMainLooper());
    if (Looper.myLooper() != Looper.getMainLooper()) {
      completeFromJava("activity was not created on the Java UI thread");
      return;
    }

    if (!handler.post(new Runnable() {
      @Override
      public void run() {
        try {
          nativeStart();
          if (!handler.post(new Runnable() {
            @Override
            public void run() {
              nativeHandlerRan();
            }
          })) {
            completeFromJava("failed to post Java Handler callback");
          }
        } catch (Throwable error) {
          completeFromJava(error.toString());
        }
      }
    })) {
      completeFromJava("failed to post native test startup");
    }
  }

  private void completeFromJava(String message) {
    nativeStop();
    failure = message;
    completion.countDown();
  }

  private void onNativeComplete(final String message) {
    if (!handler.post(new Runnable() {
      @Override
      public void run() {
        nativeStop();
        failure = message;
        completion.countDown();
      }
    })) {
      failure = "failed to post native test cleanup";
      completion.countDown();
    }
  }

  @Override
  protected void onDestroy() {
    nativeStop();
    super.onDestroy();
  }
}
