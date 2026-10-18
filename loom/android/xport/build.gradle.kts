// X-Port for Android: the archive tool (loom/tools/extract, Python) run by Chaquopy, one screen.
// Chaquopy version is confirmed by the first CI build; see docs/reports/INDEX.md.
plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("com.chaquo.python")
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

// The engine lives in the repo (loom/tools/extract + engine/ + core/ + loom/data/lexicons); it is copied
// into a build directory in the layout the engine expects (REPO_ROOT = three levels above the package),
// never committed twice.
val repoRoot = rootProject.projectDir.resolve("../..").canonicalFile
val enginePython = layout.buildDirectory.dir("generated/xport-python")

val syncEnginePython by tasks.registering(Sync::class) {
    into(enginePython)
    from(repoRoot.resolve("loom/tools/extract")) {
        into("loom/tools/extract")
        exclude("tests/**", "**/__pycache__/**", "**/*.pyc")
    }
    from(repoRoot.resolve("loom/data/lexicons")) { into("loom/data/lexicons") }
    from(repoRoot.resolve("engine")) { into("engine"); exclude("**/__pycache__/**", "**/*.pyc") }
    from(repoRoot.resolve("core")) { into("core"); exclude("**/__pycache__/**", "**/*.pyc") }
    doLast {
        // loom/ and loom/tools/ have no __init__.py in the repo; make them regular packages here.
        for (rel in listOf("loom", "loom/tools")) {
            val f = enginePython.get().asFile.resolve("$rel/__init__.py")
            f.parentFile.mkdirs()
            if (!f.exists()) f.writeText("")
        }
    }
}

chaquopy {
    defaultConfig {
        version = "3.11"
        pip { }
        // the engine reads its data files through __file__, so they must exist on disk
        extractPackages("loom")
    }
    sourceSets.getByName("main") {
        srcDir("src/main/python")
        srcDir(enginePython)
    }
}

tasks.configureEach {
    if (name.contains("PythonSources") || name.contains("PythonRequirements") || name.contains("PythonAssets")) {
        dependsOn(syncEnginePython)
    }
}
