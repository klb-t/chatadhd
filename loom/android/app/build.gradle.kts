import org.jetbrains.kotlin.gradle.tasks.KotlinCompile

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// ── Packaging knobs (CI: .github/workflows/package.yml) ────────────────
// All optional; without them the build is exactly what it was before.
//   -Ploom.versionCode=N     monotonically increasing, so a phone accepts the APK as an update
//   -Ploom.versionName=X     shown in the system app settings
//   -Ploom.abis=a,b          ABIs to build (default arm64-v8a,x86_64; phones need only arm64-v8a)
//   -Ploom.ccache=true       use ccache as the C/C++ compiler launcher (the native build dominates the time)
//   -Ploom.nativeBuildType=T CMake build type of the native core (Release/RelWithDebInfo/Debug/MinSizeRel);
//                            default: what Gradle picks for the variant (Debug for assembleDebug, which
//                            makes the core slow on a phone - the package workflow asks for Release)
// The one source of the version (R36): loom/VERSION, "x.yy.zz". versionName defaults to it; versionCode defaults to
// x*10^9 + yy*10^7 + zz*10^5 (+ the build number CI passes as loom.buildNumber, < 100000), so a newer version, and a
// newer build of the same version, always installs as an update.
val loomVersionText = rootProject.file("../VERSION").readText().trim()
val loomVersionParts = Regex("""^(\d+)\.(\d\d)\.(\d\d)$""").matchEntire(loomVersionText)?.groupValues?.drop(1)?.map { it.toInt() }
    ?: throw GradleException("loom/VERSION must read x.yy.zz (got '$loomVersionText')")
val loomBuildNumber = providers.gradleProperty("loom.buildNumber").map { it.trim().toInt() % 100000 }.orElse(0)
val loomVersionCode = providers.gradleProperty("loom.versionCode").map { it.trim().toInt() }
    .orElse(loomBuildNumber.map { loomVersionParts[0] * 1_000_000_000 + loomVersionParts[1] * 10_000_000 + loomVersionParts[2] * 100_000 + it })
val loomVersionName = providers.gradleProperty("loom.versionName").map { it.trim() }.orElse(loomVersionText)
val loomAbis = providers.gradleProperty("loom.abis")
    .map { v -> v.split(",").map { it.trim() }.filter { it.isNotEmpty() } }
    .orElse(listOf("arm64-v8a", "x86_64"))
val loomCcache = providers.gradleProperty("loom.ccache").map { it.trim() == "true" }.orElse(false)
val loomNativeBuildType = providers.gradleProperty("loom.nativeBuildType").map { it.trim() }.orElse("")

android {
    namespace = "com.chatadhd.android"
    compileSdk = 34
    ndkVersion = "27.0.12077973"

    defaultConfig {
        applicationId = "com.chatadhd.android"
        minSdk = 26
        targetSdk = 34
        versionCode = loomVersionCode.get()
        versionName = loomVersionName.get()

        ndk {
            // Matches loom/CMakeLists.txt's target platforms for this shell.
            abiFilters += loomAbis.get()
        }

        externalNativeBuild {
            cmake {
                // HTTP is injected from Kotlin (LoomHttp.kt), so the core is
                // built with OpenSSL off; see app/src/main/cpp/CMakeLists.txt.
                arguments += listOf("-DANDROID_STL=c++_shared")
                if (loomNativeBuildType.get().isNotEmpty()) {
                    // user arguments come after the ones the plugin adds, so this wins over the variant's default
                    arguments += listOf("-DCMAKE_BUILD_TYPE=${loomNativeBuildType.get()}")
                }
                if (loomCcache.get()) {
                    arguments += listOf("-DCMAKE_C_COMPILER_LAUNCHER=ccache", "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache")
                }
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.24.0+"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
        debug {
            isDebuggable = true
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    packaging {
        // One copy of libc++_shared.so per ABI is enough; both loom_jni.so
        // variants (arm64-v8a, x86_64) link it identically.
        jniLibs.useLegacyPackaging = false
    }

    buildFeatures {
        buildConfig = false
    }
}

tasks.withType<KotlinCompile>().configureEach {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("androidx.activity:activity-ktx:1.9.3")
    implementation("com.google.android.material:material:1.12.0")
    // WebViewAssetLoader: serves app/src/main/assets/web/ over a virtual
    // https://appassets.androidx.startup/ origin instead of file://, so the
    // WebView never gets raw filesystem access (see MainActivity.kt).
    implementation("androidx.webkit:webkit:1.12.1")
}

// ── Copy the web UI's production build into our assets ──────────────────
// The other agent's loom/web project builds to loom/web/dist (npm run
// build). We only copy; we never write into loom/web ourselves. Using Sync
// (not Copy) so files removed from a later `npm run build` are removed here
// too instead of lingering as stale assets.
// rootProject.projectDir is loom/android; loom/web/dist is one level up
// from there, then into web/dist.
val webDistDir = rootProject.projectDir.resolve("../web/dist")
val copyWebAssets = tasks.register<Sync>("copyWebAssets") {
    description = "Copies loom/web/dist (npm run build output) into app/src/main/assets/web"
    group = "loom"
    from(webDistDir)
    into(layout.projectDirectory.dir("src/main/assets/web"))
    // The web project is built by a different agent/pipeline and may not
    // exist yet in a given checkout; don't fail the Android build over it,
    // just warn so `./gradlew assembleDebug` still works for JNI-only work.
    onlyIf {
        val exists = webDistDir.exists() && webDistDir.isDirectory && (webDistDir.list()?.isNotEmpty() == true)
        if (!exists) {
            logger.warn("loom-android: ${webDistDir} not found or empty (run `npm run build` in loom/web first) " +
                "- assets/web will keep whatever was copied last time")
        }
        exists
    }
}

tasks.named("preBuild") { dependsOn(copyWebAssets) }
