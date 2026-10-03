import org.jetbrains.kotlin.gradle.tasks.KotlinCompile

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.chatadhd.android"
    compileSdk = 34
    ndkVersion = "27.0.12077973"

    defaultConfig {
        applicationId = "com.chatadhd.android"
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = "0.1.0"

        ndk {
            // Matches loom/CMakeLists.txt's target platforms for this shell.
            abiFilters += listOf("arm64-v8a", "x86_64")
        }

        externalNativeBuild {
            cmake {
                // HTTP is injected from Kotlin (LoomHttp.kt), so the core is
                // built with OpenSSL off; see app/src/main/cpp/CMakeLists.txt.
                arguments += listOf("-DANDROID_STL=c++_shared")
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
