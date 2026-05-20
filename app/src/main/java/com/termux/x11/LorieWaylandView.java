package com.termux.x11;

import android.content.Context;
import android.util.AttributeSet;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

public class LorieWaylandView extends SurfaceView implements SurfaceHolder.Callback {
    static { System.loadLibrary("Xlorie"); nativeInit(); }

    public LorieWaylandView(Context c) { super(c); init(); }
    public LorieWaylandView(Context c, AttributeSet a) { super(c, a); init(); }
    public LorieWaylandView(Context c, AttributeSet a, int d) { super(c, a, d); init(); }

    private void init() { getHolder().addCallback(this); }

    @Override public void surfaceCreated(SurfaceHolder h) {}
    @Override public void surfaceChanged(SurfaceHolder h, int f, int w, int h2) {
        surfaceChanged(h.getSurface());
    }
    @Override public void surfaceDestroyed(SurfaceHolder h) { surfaceChanged(null); }

    public static native void nativeInit();
    public static native void surfaceChanged(Surface surface);
    public static native void sendMouseEvent(float x, float y, int button,
                                              boolean down, boolean relative);
    public static native void sendTouchEvent(int action, int id, int x, int y);
    public static native boolean sendKeyEvent(int scanCode, int keyCode,
                                               boolean down);
    public static native void sendTextEvent(byte[] text);
    public static native void sendClipboardEvent(byte[] text);
}
