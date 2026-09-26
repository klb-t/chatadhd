# Keep the JNI bridge classes: their method signatures are matched by name
# from native code (loom_jni.cpp) and by the WebView's addJavascriptInterface,
# neither of which R8 can see.
-keep class com.chatadhd.android.LoomNative { *; }
-keep class com.chatadhd.android.LoomCallbacks { *; }
-keep class com.chatadhd.android.LoomHttp { *; }
-keepclassmembers class com.chatadhd.android.LoomBridge {
    @android.webkit.JavascriptInterface <methods>;
}
