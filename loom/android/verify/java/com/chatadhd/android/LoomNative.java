package com.chatadhd.android;

/**
 * Host mirror of app/src/main/java/com/chatadhd/android/LoomNative.kt: the
 * exact same native method names and JNI signatures, so this class links
 * against loom_jni.cpp's Java_com_chatadhd_android_LoomNative_* exports
 * unmodified (see loom/android/verify/README for how — the JNI glue itself
 * is compiled once and used by both the Android build and this host
 * verification harness; only the Java/Kotlin side differs, and only in
 * class-loading mechanics, never in signatures).
 */
public final class LoomNative {
    private LoomNative() {}

    public static native String nativeInit(String dataDir, String optionsJson);
    public static native void nativeShutdown();
    public static native String nativeVersion();
    public static native String nativeInvoke(String method, String argsJson);
    public static native String nativeChat(String requestJson, String callbackId);
    public static native String nativeImportFile(String path, String title, String callbackId);
    public static native long nativeSubscribe(String event, String callbackId);
    public static native int nativeUnsubscribe(long token);
    public static native void nativeSetLogLevel(int level);
    public static native int nativeHttpResponseBegin(long handle, int status, String headersJson);
    public static native int nativeHttpResponseWrite(long handle, byte[] data);
    public static native int nativeHttpResponseFail(long handle, int code, String message);
    public static native boolean nativeHttpResponseCancelled(long handle);
}
