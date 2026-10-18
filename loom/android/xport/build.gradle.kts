// X-Port for Android: the archive tool (loom/tools/extract, Python) run by Chaquopy, one screen.
// Chaquopy version is confirmed by the first CI build; see docs/reports/INDEX.md.
plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("com.chaquo.python") version "15.0.1"
}

android {
    namespace = "com.chatadhd.xport"
    compileSdk = 34
    defaultConfig {
        applicationId = "com.chatadhd.xport"
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = "0.1.0"
        ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}

chaquopy {
    defaultConfig {
        version = "3.11"
        pip { }
    }
    sourceSets.getByName("main") {
        srcDir("src/main/python")
    }
}
