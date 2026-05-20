package com.termux.x11;

import static android.system.Os.getuid;
import static android.system.Os.getenv;

import android.annotation.SuppressLint;
import android.app.IActivityManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.IIntentReceiver;
import android.content.IIntentSender;
import android.content.Intent;
import android.os.Binder;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.util.Log;
import android.view.Surface;

import androidx.annotation.Keep;

import java.io.OutputStream;
import java.io.PrintStream;
import java.net.URL;

/**
 * Compatibility wrapper for Wayland compositor entry point.
 * Delegates to WaylandEntryPoint which matches the JNI implementation in lorie-wayland/main.c.
 * @deprecated Use WaylandEntryPoint directly instead.
 */
@Keep @SuppressLint({"StaticFieldLeak", "UnsafeDynamicallyLoadedCode"})
public class WaylandCmdEntryPoint {
    public static final String ACTION_START = WaylandEntryPoint.ACTION_START;

    /**
     * Command-line entry point for Wayland compositor.
     * Delegates to WaylandEntryPoint.main().
     *
     * @param args The command-line arguments
     */
    public static void main(String[] args) {
        WaylandEntryPoint.main(args);
    }
}
