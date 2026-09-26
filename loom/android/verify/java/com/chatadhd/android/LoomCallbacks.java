package com.chatadhd.android;

import java.util.concurrent.BlockingQueue;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.LinkedBlockingQueue;

/**
 * Host stand-in for LoomCallbacks.kt. The real Kotlin class hops to the
 * WebView UI thread and calls window.__loomCallbacks[id](chunkJson, done);
 * this one collects chunks into a per-callback-id queue the test thread
 * polls, which exercises exactly the same native -> Java call
 * (loom_jni.cpp's deliver_chunk -> Java_..._LoomCallbacks.onChunk, found via
 * JNI_OnLoad's FindClass/GetStaticMethodID) without needing a WebView.
 */
public final class LoomCallbacks {
    public static final class Chunk {
        public final String json;
        public final boolean done;

        Chunk(String json, boolean done) {
            this.json = json;
            this.done = done;
        }
    }

    private static final ConcurrentHashMap<String, BlockingQueue<Chunk>> QUEUES = new ConcurrentHashMap<>();

    private LoomCallbacks() {}

    public static BlockingQueue<Chunk> register(String callbackId) {
        BlockingQueue<Chunk> q = new LinkedBlockingQueue<>();
        QUEUES.put(callbackId, q);
        return q;
    }

    /** Called from native code (loom_jni.cpp: deliver_chunk). */
    public static void onChunk(String callbackId, String chunkJson, boolean done) {
        BlockingQueue<Chunk> q = QUEUES.get(callbackId);
        if (q == null) {
            System.err.println("[LoomCallbacks] chunk for unregistered id " + callbackId + ": " + chunkJson);
            return;
        }
        q.offer(new Chunk(chunkJson, done));
    }
}
