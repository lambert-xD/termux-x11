package com.termux.x11;

import android.content.ClipboardManager;
import android.content.ClipData;
import android.content.Context;
import android.util.AttributeSet;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

public class LorieWaylandView extends SurfaceView implements SurfaceHolder.Callback {
    private static ClipboardManager clipboard;

    static { System.loadLibrary("Xlorie"); nativeInit(); }

    public LorieWaylandView(Context c) { super(c); init(); }
    public LorieWaylandView(Context c, AttributeSet a) { super(c, a); init(); }
    public LorieWaylandView(Context c, AttributeSet a, int d) { super(c, a, d); init(); }

    private void init() {
        getHolder().addCallback(this);
        if (clipboard == null) {
            clipboard = (ClipboardManager) getContext().getSystemService(Context.CLIPBOARD_SERVICE);
        }
    }

    @Override public void surfaceCreated(SurfaceHolder h) {}
    @Override public void surfaceChanged(SurfaceHolder h, int f, int w, int h2) {
        surfaceChanged(h.getSurface(), w, h2);
    }
    @Override public void surfaceDestroyed(SurfaceHolder h) { surfaceChanged(null, 0, 0); }

    public static native void nativeInit();
    public static native void surfaceChanged(Surface surface, int width, int height);
    public static native void sendMouseEvent(float x, float y, int button,
                                              boolean down, boolean relative);
    public static native void sendTouchEvent(int action, int id, int x, int y);
    public static native boolean sendKeyEvent(int scanCode, int keyCode,
                                               boolean down);
    public static native void sendTextEvent(byte[] text);
    public static native void sendClipboardEvent(byte[] text);

    /** Called from native code to set Android clipboard from Wayland selection. */
    public static void setClipboardText(byte[] text) {
        if (clipboard != null && text != null) {
            String s = new String(text, java.nio.charset.StandardCharsets.UTF_8);
            clipboard.setPrimaryClip(ClipData.newPlainText("Wayland clipboard", s));
        }
    }
}
