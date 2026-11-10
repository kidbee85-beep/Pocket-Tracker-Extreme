package com.conanizer.pockettracker;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.UriPermission;
import android.os.Build;
import android.os.Bundle;
import android.os.SystemClock;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Iterator;
import java.util.List;

/** Pure framework/Java runner so it also works against the older, unminified APK. */
public final class UpgradePreservationSmoke extends Instrumentation {
    private Bundle args = new Bundle();
    @Override public void onCreate(Bundle arguments) {
        super.onCreate(arguments); if (arguments != null) args = arguments; start();
    }
    private static void require(boolean yes, String what) {
        if (!yes) throw new AssertionError(what);
    }
    private static byte[] read(InputStream source) throws Exception {
        try (InputStream in = source; java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream()) {
            byte[] buffer = new byte[8192]; int count;
            while ((count = in.read(buffer)) != -1) out.write(buffer, 0, count);
            return out.toByteArray();
        }
    }
    private static byte[] bytes(File file) throws Exception { return read(new FileInputStream(file)); }
    private static String text(File file) throws Exception { return new String(bytes(file), StandardCharsets.UTF_8); }
    private static void write(File file, byte[] value) throws Exception {
        File parent = file.getParentFile(); if (parent != null) parent.mkdirs();
        try (FileOutputStream out = new FileOutputStream(file)) { out.write(value); out.getFD().sync(); }
    }
    private static void writeText(File file, String value) throws Exception { write(file, value.getBytes(StandardCharsets.UTF_8)); }
    private static String sha(File file) throws Exception {
        byte[] hash = MessageDigest.getInstance("SHA-256").digest(bytes(file));
        StringBuilder out = new StringBuilder(); for (byte b : hash) out.append(String.format("%02x", b & 255));
        return out.toString();
    }
    private JSONArray grants() {
        List<String> all = new ArrayList<>();
        for (UriPermission p : getTargetContext().getContentResolver().getPersistedUriPermissions())
            all.add(p.getUri() + "|" + p.isReadPermission() + "|" + p.isWritePermission());
        Collections.sort(all); return new JSONArray(all);
    }
    private void launchAndClose() throws Exception {
        File log = new File(getTargetContext().getFilesDir(), "pockettracker-log.txt");
        require(!log.exists() || log.delete(), "Could not reset the emulator fixture startup log");
        @SuppressWarnings("deprecation") int code = getTargetContext().getPackageManager()
            .getPackageInfo(getTargetContext().getPackageName(),0).versionCode;
        Activity screen = startActivitySync(new Intent().setClassName(getTargetContext().getPackageName(),
            "com.conanizer.pockettracker.MainActivity").addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
        waitForIdleSync();
        boolean ready = false; long until = SystemClock.elapsedRealtime() + 20000;
        while (!ready && SystemClock.elapsedRealtime() < until) {
            if (log.isFile()) {
                String content = text(log);
                ready = content.contains(code >= 20000 ? "first SDL_RenderPresent returned" : "settings:");
            }
            if (!ready) SystemClock.sleep(50);
        }
        require(ready, "Native UI did not finish startup before the lifecycle test");
        if (code < 20000) SystemClock.sleep(500); // Old .06 predates the first-frame log marker.
        runOnMainSync(screen::finish); waitForIdleSync();
        until = SystemClock.elapsedRealtime() + 10000;
        while (!screen.isDestroyed() && SystemClock.elapsedRealtime() < until) SystemClock.sleep(50);
        require(screen.isDestroyed(), "Activity did not complete its clean exit");
        SystemClock.sleep(200);
    }
    private static String normalized(Object value) throws Exception {
        if (value instanceof JSONObject) {
            JSONObject object = (JSONObject)value; List<String> keys = new ArrayList<>();
            Iterator<String> it = object.keys(); while (it.hasNext()) keys.add(it.next()); Collections.sort(keys);
            StringBuilder out = new StringBuilder("{");
            for (String key : keys) { if (out.length() > 1) out.append(','); out.append(JSONObject.quote(key)).append(':').append(normalized(object.get(key))); }
            return out.append('}').toString();
        }
        if (value instanceof JSONArray) {
            JSONArray a = (JSONArray)value; StringBuilder out = new StringBuilder("[");
            for (int i = 0; i < a.length(); ++i) { if (i > 0) out.append(','); out.append(normalized(a.get(i))); }
            return out.append(']').toString();
        }
        return value instanceof String ? JSONObject.quote((String)value) : String.valueOf(value);
    }
    private static void checkMedia(File media, JSONObject hashes) throws Exception {
        Iterator<String> it = hashes.keys(); while (it.hasNext()) {
            String name = it.next(); require(sha(new File(media,name)).equals(hashes.getString(name)), "Media changed: " + name);
        }
    }
    @Override public void onStart() {
        Bundle result = new Bundle(); boolean success = false;
        try {
            require(Build.HARDWARE.equals("ranchu") || Build.HARDWARE.equals("goldfish") || Build.PRODUCT.contains("sdk"), "Emulator only");
            Context app = getTargetContext(); require(app.getPackageName().equals("com.conanizer.pockettracker.studio.debug") || app.getPackageName().equals("com.conanizer.pockettracker.studio"), "Pocket Tracker Extreme test packages only");
            File root = app.getFilesDir(); File external = app.getExternalFilesDir(null); require(external != null, "Scoped external storage unavailable");
            File media = new File(external,"UpgradePreservationFixture"), snapshot = new File(root,"upgrade-preservation-snapshot.json");
            SharedPreferences prefs = app.getSharedPreferences("pockettracker_ui", Context.MODE_PRIVATE);
            String marker = "PocketTracker-private-update-preservation";
            @SuppressWarnings("deprecation") int code = app.getPackageManager().getPackageInfo(app.getPackageName(),0).versionCode;
            int expectedSeed = Integer.parseInt(args.getString("expectedSeedVersionCode", "10006"));
            int expectedUpdate = Integer.parseInt(args.getString("expectedUpdateVersionCode", "20003"));
            if ("seed".equals(args.getString("phase"))) {
                require(code == expectedSeed, "Expected actual old APK version code " + expectedSeed);
                for (String name : new String[]{"settings.json","config.json"}) write(new File(root,name), read(getContext().getAssets().open("upgrade-"+name)));
                require(prefs.edit().putString("upgrade_keep_token",marker).commit(), "Preference commit failed");
                JSONObject fixture = new JSONObject().put("Samples/Drums/Keep.wav","RIFF-user-sample-fixture").put("Projects/Keep.ptp","{\"preservation_fixture\":true}")
                    .put("Presets/Instruments/Keep.pti","{\"preset_fixture\":true}").put("Themes/Keep.theme","custom user material").put("Recordings/Keep.wav","user recording fixture");
                Iterator<String> it = fixture.keys(); while (it.hasNext()) { String name = it.next(); writeText(new File(media,name),fixture.getString(name)); }
                launchAndClose(); JSONObject hashes = new JSONObject(); it = fixture.keys(); while (it.hasNext()) { String name=it.next(); hashes.put(name,sha(new File(media,name))); }
                JSONObject saved = new JSONObject().put("media",hashes).put("settingsHash",sha(new File(root,"settings.json"))).put("configHash",sha(new File(root,"config.json")))
                    .put("settings",new JSONObject(text(new File(root,"settings.json")))).put("grants",grants());
                writeText(snapshot,saved.toString(2)); result.putString("phase","seed-old-"+expectedSeed);
            } else {
                require(code == expectedUpdate,"Expected update APK version code "+expectedUpdate); JSONObject saved = new JSONObject(text(snapshot));
                require(sha(new File(root,"settings.json")).equals(saved.getString("settingsHash")),"Settings changed during APK installation");
                require(sha(new File(root,"config.json")).equals(saved.getString("configHash")),"Controller config changed during APK installation");
                require(marker.equals(prefs.getString("upgrade_keep_token",null)),"Preferences lost"); require(normalized(grants()).equals(normalized(saved.getJSONArray("grants"))),"Folder grants changed");
                JSONObject hashes=saved.getJSONObject("media"); checkMedia(media,hashes); launchAndClose(); checkMedia(media,hashes);
                require(sha(new File(root,"config.json")).equals(saved.getString("configHash")),"Controller config changed after startup");
                JSONObject before=saved.getJSONObject("settings"), after=new JSONObject(text(new File(root,"settings.json")));
                for(String key:new String[]{"appTheme","theme","visualizer","abxy","portrait_skin","overlay_name","buttonSound","buttonVibro","scalingBilinear"})
                    require(normalized(before.get(key)).equals(normalized(after.get(key))),"Changed setting: "+key);
                if(expectedUpdate>=20003) {
                    require(after.optInt("sampleFolderMemoryVersion")==1,"Sample memory schema missing");
                    require(after.optBoolean("rememberFolder"),"Requested sample memory migration was not enabled");
                }
                require(marker.equals(prefs.getString("upgrade_keep_token",null)),"Preferences changed after startup");
                require(normalized(grants()).equals(normalized(saved.getJSONArray("grants"))),"Folder grants changed after startup");
                result.putString("phase","verify-in-place-"+expectedUpdate); result.putInt("preservedMediaFiles",hashes.length()); result.putInt("persistedGrantCount",grants().length());
            }
            success=true; result.putString("stream","\nPASS: "+result.getString("phase")+"; emulator fixture only, no physical-device or nonempty SAF-grant claim\n");
        } catch(Throwable error) { result.putString("error",error.toString()); result.putString("stream","\nFAIL: "+error+"\n"); }
        result.putBoolean("success",success); result.putBoolean("virtualEmulatorOnly",true);
        finish(success ? Activity.RESULT_OK : Activity.RESULT_CANCELED,result);
    }
}
