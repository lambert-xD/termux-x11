package com.termux.x11;

public class WaylandEntryPoint {
    public static native boolean start(String[] args);
    public static native void stop();
    public static native boolean connected();
    public static native void setSocketFd(int fd);

    static { System.loadLibrary("Xlorie"); }
}
