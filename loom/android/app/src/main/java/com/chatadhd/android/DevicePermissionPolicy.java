package com.chatadhd.android;

import java.net.URI;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Pure-Java (no android.* imports, so it is testable with plain javac; see verify/) rules for the
 * devices the web UI may use inside the WebView (P13): the microphone and the camera, asked for
 * through getUserMedia. The page declares them to the kernel (client.declare); the WebView only
 * hands them over when the request comes from the page this app itself loaded (the bundled asset
 * origin, or the server the user chose) and names a device this shell serves.
 */
public final class DevicePermissionPolicy {
    private DevicePermissionPolicy() {}

    /** android.webkit.PermissionRequest.RESOURCE_AUDIO_CAPTURE / RESOURCE_VIDEO_CAPTURE. */
    public static final String RESOURCE_AUDIO_CAPTURE = "android.webkit.resource.AUDIO_CAPTURE";
    public static final String RESOURCE_VIDEO_CAPTURE = "android.webkit.resource.VIDEO_CAPTURE";

    /** The Android runtime permission a WebView resource needs, or null for a resource this shell does not serve. */
    public static String androidPermissionFor(String resource) {
        if (RESOURCE_AUDIO_CAPTURE.equals(resource)) return "android.permission.RECORD_AUDIO";
        if (RESOURCE_VIDEO_CAPTURE.equals(resource)) return "android.permission.CAMERA";
        return null;
    }

    /**
     * The requested resources the page may be given: none unless the request's origin is the app's own
     * ({@code appOrigin}, e.g. "https://appassets.androidplatform.net") or the chosen server
     * ({@code serverOrigin}, null in local mode); of those only the devices this shell serves.
     */
    public static List<String> servable(String[] requested, String origin, String appOrigin, String serverOrigin) {
        List<String> out = new ArrayList<>();
        if (requested == null || !isOwnOrigin(origin, appOrigin, serverOrigin)) return out;
        for (String resource : requested) {
            if (androidPermissionFor(resource) != null && !out.contains(resource)) out.add(resource);
        }
        return out;
    }

    /** True when {@code origin} is exactly the origin (scheme, host, port) of {@code appOrigin} or {@code serverOrigin}. */
    public static boolean isOwnOrigin(String origin, String appOrigin, String serverOrigin) {
        String normal = originOf(origin);
        if (normal == null) return false;
        return normal.equals(originOf(appOrigin)) || (serverOrigin != null && normal.equals(originOf(serverOrigin)));
    }

    private static String originOf(String url) {
        if (url == null) return null;
        try {
            URI u = new URI(url);
            if (u.getScheme() == null || u.getHost() == null) return null;
            int port = u.getPort();
            String scheme = u.getScheme().toLowerCase(Locale.ROOT);
            if (port == -1) port = scheme.equals("https") ? 443 : scheme.equals("http") ? 80 : -1;
            return scheme + "://" + u.getHost().toLowerCase(Locale.ROOT) + ":" + port;
        } catch (Exception e) {
            return null;
        }
    }
}
