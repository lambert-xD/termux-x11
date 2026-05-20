package com.termux.x11;

public class WaylandEntryPoint {
    public static native boolean start(String[] args);
    public static native void stop();
    public static native boolean connected();

    static { System.loadLibrary("Xlorie"); }
}
