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

import androidx.annotation.Keep;

import java.io.OutputStream;
import java.io.PrintStream;
import java.net.URL;

/**
 * Command-line entry point for Wayland compositor mode.
 * JNI methods match the native implementations in lorie-wayland/main.c
 */
@Keep @SuppressLint({"StaticFieldLeak", "UnsafeDynamicallyLoadedCode"})
public class WaylandEntryPoint extends ICmdEntryInterface.Stub {
    public static final String ACTION_START = "com.termux.x11.WaylandEntryPoint.ACTION_START";
    static final Handler handler;
    public static Context ctx;
    private final Intent intent = createIntent();

    /**
     * Command-line entry point for Wayland compositor.
     *
     * @param args The command-line arguments
     */
    public static void main(String[] args) {
        android.util.Log.i("WaylandEntryPoint", "commit " + BuildConfig.COMMIT);
        handler.post(() -> new WaylandEntryPoint(args));
        Looper.loop();
    }

    WaylandEntryPoint(String[] args) {
        if (!start(args))
            System.exit(1);

        sendBroadcastDelayed();
    }

    @SuppressLint({"WrongConstant", "PrivateApi"})
    private Intent createIntent() {
        String targetPackage = getenv("TERMUX_X11_OVERRIDE_PACKAGE");
        if (targetPackage == null)
            targetPackage = "com.termux.x11";
        Bundle bundle = new Bundle();
        bundle.putBinder(null, this);

        Intent intent = new Intent(ACTION_START);
        intent.putExtra(null, bundle);
        intent.setPackage(targetPackage);

        if (getuid() == 0 || getuid() == 2000)
            intent.setFlags(0x00400000 /* FLAG_RECEIVER_FROM_SHELL */);

        return intent;
    }

    private void sendBroadcast() {
        sendBroadcast(intent);
    }

    static void sendBroadcast(Intent intent) {
        try {
            ctx.sendBroadcast(intent);
        } catch (Exception e) {
            if (e instanceof NullPointerException && ctx == null)
                Log.i("WaylandBroadcast", "Context is null, falling back to manual broadcasting");
            else
                Log.e("WaylandBroadcast", "Falling back to manual broadcasting, failed to broadcast intent through Context:", e);

            String packageName;
            try {
                packageName = android.app.ActivityThread.getPackageManager().getPackagesForUid(getuid())[0];
            } catch (RemoteException ex) {
                throw new RuntimeException(ex);
            }
            IActivityManager am;
            try {
                am = (IActivityManager) android.app.ActivityManager.class
                        .getMethod("getService")
                        .invoke(null);
            } catch (Exception e2) {
                try {
                    am = (IActivityManager) Class.forName("android.app.ActivityManagerNative")
                            .getMethod("getDefault")
                            .invoke(null);
                } catch (Exception e3) {
                    throw new RuntimeException(e3);
                }
            }

            assert am != null;
            IIntentSender sender = am.getIntentSender(1, packageName, null, null, 0, new Intent[] { intent },
                    null, PendingIntent.FLAG_CANCEL_CURRENT | PendingIntent.FLAG_ONE_SHOT, null, 0);
            try {
                IIntentSender.class
                        .getMethod("send", int.class, Intent.class, String.class, IBinder.class, IIntentReceiver.class, String.class, Bundle.class)
                        .invoke(sender, 0, intent, null, null, new IIntentReceiver.Stub() {
                            @Override public void performReceive(Intent i, int r, String d, Bundle e, boolean o, boolean s, int a) {}
                        }, null, null);
            } catch (Exception ex) {
                throw new RuntimeException(ex);
            }
        }
    }

    private void sendBroadcastDelayed() {
        if (!connected())
            sendBroadcast(intent);

        handler.postDelayed(this::sendBroadcastDelayed, 1000);
    }

    @SuppressLint("DiscouragedPrivateApi")
    public static Context createContext() {
        Context context;
        PrintStream err = System.err;
        try {
            java.lang.reflect.Field f = Class.forName("sun.misc.Unsafe").getDeclaredField("theUnsafe");
            f.setAccessible(true);
            Object unsafe = f.get(null);
            System.setErr(new PrintStream(new OutputStream() { public void write(int arg0) {} }));
            if (System.getenv("OLD_CONTEXT") != null) {
                context = android.app.ActivityThread.systemMain().getSystemContext();
            } else {
                context = ((android.app.ActivityThread) Class.
                        forName("sun.misc.Unsafe").
                        getMethod("allocateInstance", Class.class).
                        invoke(unsafe, android.app.ActivityThread.class))
                        .getSystemContext();
            }
        } catch (Exception e) {
            Log.e("WaylandContext", "Failed to instantiate context:", e);
            context = null;
        } finally {
            System.setErr(err);
        }
        return context;
    }

    // Native methods - implemented in lorie-wayland/main.c
    public static native boolean start(String[] args);
    public static native void stop();
    public native ParcelFileDescriptor getWaylandConnection();
    private static native boolean connected();

    static {
        try {
            if (Looper.getMainLooper() == null)
                Looper.prepareMainLooper();
        } catch (Exception e) {
            Log.e("WaylandEntryPoint", "Something went wrong when preparing MainLooper", e);
        }
        handler = new Handler();
        ctx = createContext();

        String path = "lib/" + Build.SUPPORTED_ABIS[0] + "/libXlorie.so";
        ClassLoader loader = WaylandEntryPoint.class.getClassLoader();
        URL res = loader != null ? loader.getResource(path) : null;
        String libPath = res != null ? res.getFile().replace("file:", "") : null;
        if (libPath != null) {
            try {
                System.load(libPath);
            } catch (Exception e) {
                Log.e("WaylandEntryPoint", "Failed to dlopen " + libPath, e);
                System.err.println("Failed to load native library. Did you install the right apk? Try the universal one.");
                System.exit(134);
            }
        } else {
            if (WaylandActivity.getInstance() == null) {
                System.err.println("Failed to acquire native library. Did you install the right apk? Try the universal one.");
                System.exit(134);
            }
        }
    }
}
