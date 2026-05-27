package com.termux.x11;

import android.content.ClipboardManager;
import android.content.ClipData;
import android.content.Context;
import android.util.AttributeSet;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
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
        setFocusable(true);
        setFocusableInTouchMode(true);
        if (clipboard == null) {
            clipboard = (ClipboardManager) getContext().getSystemService(Context.CLIPBOARD_SERVICE);
        }
    }

    @Override public void surfaceCreated(SurfaceHolder h) {}
    @Override public void surfaceChanged(SurfaceHolder h, int f, int w, int h2) {
        surfaceChanged(h.getSurface(), w, h2);
    }
    @Override public void surfaceDestroyed(SurfaceHolder h) { surfaceChanged(null, 0, 0); }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        int action = event.getActionMasked();
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                requestFocus();
                int idx = event.getActionIndex();
                sendTouchEvent(0, event.getPointerId(idx),
                               (int) event.getX(idx), (int) event.getY(idx));
                break;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP: {
                int idx = event.getActionIndex();
                sendTouchEvent(1, event.getPointerId(idx),
                               (int) event.getX(idx), (int) event.getY(idx));
                break;
            }
            case MotionEvent.ACTION_MOVE: {
                for (int i = 0; i < event.getPointerCount(); i++) {
                    sendTouchEvent(2, event.getPointerId(i),
                                   (int) event.getX(i), (int) event.getY(i));
                }
                break;
            }
        }
        return true;
    }

    @Override
    public boolean onGenericMotionEvent(MotionEvent event) {
        int source = event.getSource();
        if ((source & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE ||
            (source & InputDevice.SOURCE_TOUCHPAD) == InputDevice.SOURCE_TOUCHPAD) {
            int action = event.getActionMasked();
            float x = event.getX();
            float y = event.getY();
            switch (action) {
                case MotionEvent.ACTION_HOVER_MOVE:
                    sendMouseEvent(x, y, 0, false, false);
                    return true;
                case MotionEvent.ACTION_BUTTON_PRESS:
                    sendMouseEvent(x, y, event.getActionButton(), true, false);
                    return true;
                case MotionEvent.ACTION_BUTTON_RELEASE:
                    sendMouseEvent(x, y, event.getActionButton(), false, false);
                    return true;
            }
        }
        return super.onGenericMotionEvent(event);
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        int action = event.getAction();
        if (action == KeyEvent.ACTION_DOWN || action == KeyEvent.ACTION_UP) {
            if (sendKeyEvent(event.getScanCode(), event.getKeyCode(),
                             action == KeyEvent.ACTION_DOWN)) {
                return true;
            }
        }
        return super.dispatchKeyEvent(event);
    }

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
