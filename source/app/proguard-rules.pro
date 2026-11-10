# ─── The by-name JNI callbacks from native into Kotlin ─────────────────────────────────────────
#
# The C++ shell finds these MainActivity methods by name (GetMethodID in shell/android/*.cpp): button
# feedback, the pad check, MIDI out and in, the Storage Access Framework, background playback.
# ⚠️ R8 must not rename or strip them. @Keep is not enough on its own here: debug never runs R8, so a
# missing rule shows only in a release APK — a render-killing release bug once came from exactly this.
# A rename fails quietly (a dead MIDI port, ADD FOLDER… doing nothing, SAVE FAILED), never at launch.
# CI reads the names out of the block below — no comments inside it.
# The activity class is a keep root already (the launcher); only its members need pinning.
-keepclassmembers class com.conanizer.pockettracker.MainActivity {
    void onButtonFeedback(int, boolean, boolean, int, boolean, int);
    boolean hasPhysicalGameButtons();
    void setLandscapeAllowed(boolean);
    java.lang.String describeInputDevices();
    int midiDeviceCount();
    java.lang.String midiDeviceName(int);
    boolean midiOpenDevice(int);
    void midiCloseDevice();
    boolean midiSend(int, int, int, int);
    boolean midiSendBytes(byte[], int);
    int midiInDeviceCount();
    java.lang.String midiInDeviceName(int);
    boolean midiInOpenDevice(int);
    void midiInCloseDevice();
    int midiInRead(byte[]);
    int safRootCount();
    java.lang.String safRootInfo(int);
    java.lang.String safListChildren(java.lang.String);
    byte[] safReadFile(java.lang.String);
    int safOpenFd(java.lang.String, java.lang.String);
    java.lang.String safCreateDir(java.lang.String, java.lang.String);
    java.lang.String safCreateFile(java.lang.String, java.lang.String);
    boolean safDelete(java.lang.String);
    java.lang.String safRename(java.lang.String, java.lang.String);
    java.lang.String safMove(java.lang.String, java.lang.String, java.lang.String);
    java.lang.String safHomeRootId();
    boolean safSetHomeRoot(java.lang.String);
    boolean safForgetRoot(java.lang.String);
    boolean safRequestRoot();
    void stopPlaybackService();
    java.lang.String captureInputsJson();
    java.lang.String captureStateJson();
    boolean captureStart(int, int, int);
    void captureStop();
    boolean sourceCommand(java.lang.String, java.lang.String);
    java.lang.String sourceStateJson();
}
# The JNI natives: their class and method names ARE the C symbol names (android-main.cpp).
-keepclasseswithmembernames class com.conanizer.pockettracker.MainActivity { native <methods>; }
-keepclasseswithmembernames class com.conanizer.pockettracker.PlaybackService { native <methods>; }

# ─── SDL2's Java glue ─────────────────────────────────────────────────────────────────────────
#
# org.libsdl.app.* is JNI in both directions by name: libSDL2.so registers natives against these
# classes, and SDL's C calls back into them (getContext, SDLAudioManager, SDLControllerManager,
# HIDDevice…), none of which our Kotlin references. To R8 it is unreachable — stripped, the app dies
# at launch in release only. `{ *; }` deliberately: upstream's C decides which members it calls.
-keep class org.libsdl.app.** { *; }
-keepclassmembers class org.libsdl.app.** { *; }

# Release drops Log.v/d/i calls and their string building; Log.w/e stay.
-assumenosideeffects class android.util.Log {
    public static int v(...);
    public static int d(...);
    public static int i(...);
}

# Google AutoService references javax.annotation.processing.*, which exists only at compile time.
-dontwarn javax.annotation.processing.**
-dontwarn com.google.auto.service.**
# NewPipe Extractor's documented Rhino retention rules (v0.27.0 README).
-keep class org.mozilla.javascript.** { *; }
-keep class org.mozilla.classfile.ClassFileWriter
-dontwarn org.mozilla.javascript.tools.**
-dontwarn javax.script.**

# Rhino1.8 optional Java-host integration and JDK Dynalink optimizer are not
# Android APIs. NewPipe's JavaScript helper explicitly calls
# Context.setInterpretedMode(true) and initSafeStandardObjects(), so its player
# decipher path uses neither JavaBeans wrapping nor the Dynalink optimizer.
# Keep warnings scoped to the exact absent host APIs, not arbitrary classes.
-dontwarn java.beans.BeanDescriptor
-dontwarn java.beans.BeanInfo
-dontwarn java.beans.IntrospectionException
-dontwarn java.beans.Introspector
-dontwarn java.beans.PropertyDescriptor
-dontwarn jdk.dynalink.CallSiteDescriptor
-dontwarn jdk.dynalink.DynamicLinker
-dontwarn jdk.dynalink.DynamicLinkerFactory
-dontwarn jdk.dynalink.NamedOperation
-dontwarn jdk.dynalink.Namespace
-dontwarn jdk.dynalink.NamespaceOperation
-dontwarn jdk.dynalink.Operation
-dontwarn jdk.dynalink.RelinkableCallSite
-dontwarn jdk.dynalink.StandardNamespace
-dontwarn jdk.dynalink.StandardOperation
-dontwarn jdk.dynalink.linker.GuardedInvocation
-dontwarn jdk.dynalink.linker.GuardingDynamicLinker
-dontwarn jdk.dynalink.linker.LinkRequest
-dontwarn jdk.dynalink.linker.LinkerServices
-dontwarn jdk.dynalink.linker.TypeBasedGuardingDynamicLinker
-dontwarn jdk.dynalink.linker.support.CompositeTypeBasedGuardingDynamicLinker
-dontwarn jdk.dynalink.linker.support.Guards
-dontwarn jdk.dynalink.support.ChainedCallSite

# Stable production interpreter entry point for the on-device release-runtime
# compatibility check. This keeps only the small adapter, not the whole extractor.
-keep class org.schabi.newpipe.extractor.utils.JavaScript { *; }
