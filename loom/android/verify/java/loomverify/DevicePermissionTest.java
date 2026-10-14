package loomverify;

import com.chatadhd.android.DevicePermissionPolicy;
import java.util.Arrays;
import java.util.List;

/** P13: which device requests of the WebView are handed to the page (plain javac, no Android classes). */
public final class DevicePermissionTest {
    private static final String APP = "https://appassets.androidplatform.net";
    private static final String SERVER = "https://loom.example.org:8443";
    private static final String[] AUDIO = {DevicePermissionPolicy.RESOURCE_AUDIO_CAPTURE};
    private static final String[] BOTH = {DevicePermissionPolicy.RESOURCE_AUDIO_CAPTURE, DevicePermissionPolicy.RESOURCE_VIDEO_CAPTURE,
        "android.webkit.resource.PROTECTED_MEDIA_ID", DevicePermissionPolicy.RESOURCE_AUDIO_CAPTURE};

    private static void require(boolean ok, String what) {
        if (!ok) throw new AssertionError(what);
    }

    public static void main(String[] args) {
        require("android.permission.RECORD_AUDIO".equals(DevicePermissionPolicy.androidPermissionFor(AUDIO[0])), "microphone needs RECORD_AUDIO");
        require("android.permission.CAMERA".equals(DevicePermissionPolicy.androidPermissionFor(DevicePermissionPolicy.RESOURCE_VIDEO_CAPTURE)), "camera needs CAMERA");
        require(DevicePermissionPolicy.androidPermissionFor("android.webkit.resource.PROTECTED_MEDIA_ID") == null, "protected media is not served");
        // the page the app loaded gets the devices it asks for - the ones this shell serves, once each
        List<String> own = DevicePermissionPolicy.servable(BOTH, APP + "/", APP, null);
        require(own.equals(Arrays.asList(DevicePermissionPolicy.RESOURCE_AUDIO_CAPTURE, DevicePermissionPolicy.RESOURCE_VIDEO_CAPTURE)), "own origin: " + own);
        require(DevicePermissionPolicy.servable(AUDIO, SERVER + "/app", APP, SERVER).size() == 1, "the chosen server's page is the app too");
        require(DevicePermissionPolicy.servable(AUDIO, "https://LOOM.example.org:8443/", APP, SERVER).size() == 1, "host case does not matter");
        // nobody else does: another origin, another port, another scheme, a lookalike, no origin, no request
        for (String stranger : new String[] {"https://evil.example", "https://appassets.androidplatform.net.evil.example/", "http://appassets.androidplatform.net/",
                "https://appassets.androidplatform.net:8443/", "https://loom.example.org/", "not a url", "", null}) {
            require(DevicePermissionPolicy.servable(AUDIO, stranger, APP, SERVER).isEmpty(), "origin refused: " + stranger);
        }
        require(DevicePermissionPolicy.servable(AUDIO, SERVER, APP, null).isEmpty(), "in local mode the server origin is nothing");
        require(DevicePermissionPolicy.servable(null, APP, APP, null).isEmpty(), "no resources, nothing to grant");
        require(DevicePermissionPolicy.servable(new String[] {"android.webkit.resource.PROTECTED_MEDIA_ID"}, APP, APP, null).isEmpty(), "a resource not served");
        System.out.println("DevicePermissionTest: all checks passed");
    }
}
