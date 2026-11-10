import java.io.File
import java.util.Properties

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
}

android {
    namespace = "com.conanizer.pockettracker"
    compileSdk {
        version = release(36)
    }
    // Must match `ndk:` in the F-Droid recipe — its offline builder provisions exactly this version.
    ndkVersion = "28.2.13676358"

    // Release signing reads the gitignored keystore.properties; without it (a fresh clone, CI) the
    // release build stays unsigned; debug builds use the normal development key.
    val keystorePropertiesFile = rootProject.file(
        providers.gradleProperty("ptSigningProperties").orElse("keystore.properties").get()
    )
    val keystoreProperties = Properties().apply {
        if (keystorePropertiesFile.exists()) keystorePropertiesFile.inputStream().use { load(it) }
    }

    defaultConfig {
        // Public TrackWerk is an independent Android installation. The private legacy
        // route is opt-in for the maintainer's existing installs only.
        applicationId = if (providers.gradleProperty("ptLegacyPrivateUpdate")
                .orElse("false").get() == "true") "com.conanizer.pockettracker.studio"
            else providers.gradleProperty("ptLegacyApplicationId")
                .orElse("com.kidbunka.trackwerk").get()
        minSdk = 26
        targetSdk = 34
        // Hand-bumped per release: 990 = 0.9.9, 991 = 0.9.9.1, 1000 = 1.0.0 next. F-Droid's update check and
        // the changelog file name (changelogs/<versionCode>.txt) need a literal value.
        versionCode = 90000
        versionName = "0.9.0"
        testInstrumentationRunner = providers.gradleProperty("ptTestRunner")
            .orElse("com.conanizer.pockettracker.CaptureSmoke").get()

        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }

        externalNativeBuild {
            cmake {
                // Shared STL, as Oboe requires.
                // ⚠️ Android 15+ devices with 16 KB pages refuse a library aligned to 4 KB — the app
                // would not start. The NDK option expands to `-Wl,-z,max-page-size=16384`. Only what
                // CMake builds here needs it; `libc++_shared.so` and `liboboe.so` arrive aligned.
                arguments += listOf(
                    "-DANDROID_STL=c++_shared",
                    "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON",
                )
                // CI passes `-PptWerror`; never on by default — F-Droid builds with its own NDK.
                if (project.hasProperty("ptWerror")) arguments += "-DPT_WERROR=ON"
            }
        }
    }

    externalNativeBuild {
        cmake {
            // The shared C++ core. file() is relative to app/.
            path = file("../native/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    // SDL2's Java half, compiled from the vendored tree. ⚠️ Not copied: SDLActivity refuses a
    // libSDL2.so of another version, and one directory keeps the Java and C halves the same release
    // (native/CMakeLists.txt checks it too — SDL_VERSION_LOCK).
    sourceSets {
        getByName("main") {
            java.srcDir("../native/vendor/SDL2/android/java")
        }
    }

    // F-Droid's scanner rejects AGP's extra "dependency metadata" signing block.
    dependenciesInfo {
        includeInApk = false
        includeInBundle = false
    }

    signingConfigs {
        // Only when keystore.properties exists.
        if (keystorePropertiesFile.exists()) {
            create("release") {
                storeFile = rootProject.file(keystoreProperties.getProperty("storeFile"))
                storePassword = keystoreProperties.getProperty("storePassword")
                keyAlias = keystoreProperties.getProperty("keyAlias")
                keyPassword = keystoreProperties.getProperty("keyPassword")
            }
        }
    }

    // Opt-in maintainer path for updating the earlier private .debug package in place.
    // It preserves the original package/key pair; it is never the public release identity.
    val legacyPrivateUpdate = providers.gradleProperty("ptLegacyPrivateUpdate")
        .orElse("false").get() == "true"

    testBuildType = providers.gradleProperty("ptTestBuildType").orElse("debug").get()

    buildTypes {
        debug {
            // A separate package (`.debug`) so a dev build installs beside a real one instead of
            // replacing it (different signing keys). Each has its own preferences and folder grant.
            applicationIdSuffix = ".debug"
        }
        release {
            // R8 and resource shrinking. The JNI keep rules are in proguard-rules.pro. Overlay PNGs
            // are in assets/, which resource shrinking leaves alone.
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
            // Never label a debug-signed package as a public release. A fresh clone may
            // build an unsigned release; maintainers supply a private signing-properties path.
            if (legacyPrivateUpdate) {
                applicationIdSuffix = ".debug"
                signingConfig = signingConfigs.getByName("debug")
            } else {
                signingConfig = if (keystorePropertiesFile.exists())
                    signingConfigs.getByName("release")
                else
                    null
            }
        }
    }
    compileOptions {
        isCoreLibraryDesugaringEnabled = true
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    kotlinOptions {
        jvmTarget = "11"
    }
    buildFeatures {
        prefab = true          // Oboe arrives as a Prefab package
    }
}

// Oboe (via Prefab), core-ktx (WindowCompat) and the splash-screen compat library — all the Kotlin
// side uses.
dependencies {
    implementation(project(":youtube-extractor"))
    coreLibraryDesugaring("com.android.tools:desugar_jdk_libs_nio:2.1.5")
    implementation(libs.oboe)
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.core.splashscreen)
}

// ─── The licence payload that ships INSIDE the APK ───────────────────────────────────────────────
//
// GPL-3.0 and BSD-3-Clause tie the notices to the artifact, not the source tree. The desktop
// packages stage this set into `licenses/` beside the executable; the APK carries it in
// `assets/licenses/`. Copied at build time, so `docs/licenses/THIRD-PARTY-NOTICES.md` stays the one
// source. `addGeneratedSourceDirectory` makes the asset merge consume this output, so no task order
// can skip it.
// ⚠️ Nothing reads these files, so a missing one breaks nothing and ships silently — a green build is
// no evidence. The release check reads every one back out of the finished APK.
abstract class StageLicenseAssets : DefaultTask() {
    // Name in the artifact → repo-relative source. ⚠️ Declared, not taken from the source: two
    // sources are both called `COPYING` (ogg, opus).
    @get:Input abstract val payload: MapProperty<String, String>

    // The sources again, so a change to a licence's content re-runs the task.
    @get:InputFiles abstract val sources: ConfigurableFileCollection

    @get:Internal abstract val repoRoot: DirectoryProperty

    // Becomes `assets/` in the APK.
    @get:OutputDirectory abstract val outputDir: DirectoryProperty

    @TaskAction
    fun stage() {
        val dir = outputDir.get().asFile.resolve("licenses")
        // Wiped first, so an entry dropped from `payload` leaves the APK too.
        dir.deleteRecursively()
        dir.mkdirs()
        val root = repoRoot.get().asFile
        payload.get().forEach { (name, rel) -> root.resolve(rel).copyTo(dir.resolve(name), overwrite = true) }
    }
}

// The set every artifact ships — the three desktop packaging scripts stage the same, keep them in
// step. All after the first three are cited by path in THIRD-PARTY-NOTICES.md; the `COPYING` files
// come from the vendored source that was compiled.
val licensePayload = mapOf(
    "DaisySP-GPL-CONVERSION.txt" to "docs/licenses/DaisySP-GPL-CONVERSION.txt",
    "NewPipe-PROVENANCE.txt" to "docs/licenses/NewPipe-PROVENANCE.txt",
    "NewPipe-Extractor-GPL3.txt" to "docs/licenses/NewPipe-Extractor-GPL3.txt",
    "NewPipe-Apache-2.0.txt" to "docs/licenses/NewPipe-Apache-2.0.txt",
    "NewPipe-Jsoup-MIT.txt" to "docs/licenses/NewPipe-Jsoup-MIT.txt",
    "NewPipe-Rhino-LICENSE.txt" to "docs/licenses/NewPipe-Rhino-LICENSE.txt",
    "NewPipe-Protobuf-LICENSE.txt" to "docs/licenses/NewPipe-Protobuf-LICENSE.txt",
    "NewPipe-Desugar-JDK-LICENSE.txt" to "docs/licenses/NewPipe-Desugar-JDK-LICENSE.txt",
    "Mutable-Instruments-MIT.txt" to "docs/licenses/Mutable-Instruments-MIT.txt",
    "Mutable-Instruments-PROVENANCE.txt" to "docs/licenses/Mutable-Instruments-PROVENANCE.txt",
    "Mutable-stmlib-LICENSE" to "native/vendor/mutable/stmlib/LICENSE",
    "LICENSE"                         to "LICENSE",                                    // GPL-3.0-or-later — PocketTracker's own
    "THIRD-PARTY-NOTICES.md"          to "docs/licenses/THIRD-PARTY-NOTICES.md",       // every statically linked component
    "CREDITS.md"                      to "CREDITS.md",                                 // the Gradle-resolved Android dependencies
    "OFL-1.1-LiberationSans.txt"      to "docs/licenses/OFL-1.1-LiberationSans.txt",   // Liberation Sans font
    "OFL-1.1-QuinqueFive.txt"         to "docs/licenses/OFL-1.1-QuinqueFive.txt",      // the 5×5 UI font's source
    "libogg-COPYING"                  to "native/vendor/ogg/COPYING",                  // BSD-3-Clause
    "libopus-COPYING"                 to "native/vendor/opus/COPYING",                 // BSD-3-Clause
    "libopus-LICENSE_PLEASE_READ.txt" to "native/vendor/opus/LICENSE_PLEASE_READ.txt", // upstream's patent note
)

val stageLicenseAssets = tasks.register<StageLicenseAssets>("stageLicenseAssets") {
    payload.set(licensePayload)
    sources.from(licensePayload.values.map { rootProject.file(it) })
    repoRoot.set(rootProject.layout.projectDirectory)
    outputDir.set(layout.buildDirectory.dir("generated/licenseAssets"))
}

androidComponents {
    onVariants { variant ->
        variant.sources.assets?.addGeneratedSourceDirectory(
            stageLicenseAssets, StageLicenseAssets::outputDir
        )
    }
}
