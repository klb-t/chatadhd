// Root build script: declares plugin versions once (resolved, not applied,
// at this level; app/build.gradle.kts applies them).
plugins {
    id("com.android.application") version "8.6.1" apply false
    id("org.jetbrains.kotlin.android") version "2.0.21" apply false
}
