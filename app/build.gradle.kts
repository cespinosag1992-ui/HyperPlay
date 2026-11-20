import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

// Firma de release: en local desde keystore.properties (no versionado); en GitHub Actions desde
// los secretos HP_KEYSTORE_* (el workflow decodifica el .jks). Sin firma, el release queda sin firmar.
val signingProps = Properties().apply {
    val f = rootProject.file("keystore.properties")
    if (f.isFile) f.inputStream().use { load(it) }
}
fun signingValue(prop: String, env: String): String? =
    System.getenv(env)?.takeIf { it.isNotBlank() } ?: signingProps.getProperty(prop)

// Versión: el workflow de release la pasa desde la etiqueta (v1.2.3 -> 1.2.3, código = run number).
val hpVersionName = System.getenv("HP_VERSION_NAME") ?: (project.findProperty("hpVersionName") as String?) ?: "1.0.0"
val hpVersionCode = (System.getenv("HP_VERSION_CODE") ?: (project.findProperty("hpVersionCode") as String?))?.toIntOrNull() ?: 1

android {
    namespace = "com.murdoc820.hyperplay"
    compileSdk = 35
    ndkVersion = "28.2.13676358"

    defaultConfig {
        applicationId = "com.murdoc820.hyperplay"
        minSdk = 31
        targetSdk = 35
        versionCode = hpVersionCode
        versionName = hpVersionName

        // Actualizaciones (GitHub Releases) e informes de errores (Cloudflare Worker -> Issues).
        buildConfigField("String", "GITHUB_REPO", "\"cespinosag1992-ui/HyperPlay\"")
        buildConfigField("String", "REPORT_URL", "\"${System.getenv("HP_REPORT_URL") ?: (project.findProperty("hpReportUrl") as String?) ?: ""}\"")

        ndk {
            abiFilters += "arm64-v8a"
        }
        externalNativeBuild {
            cmake {
                cppFlags += listOf("-std=c++20", "-Wall", "-Wextra", "-Werror")
                arguments += listOf("-DANDROID_STL=c++_static")
            }
        }
    }

    signingConfigs {
        val store = signingValue("storeFile", "HP_KEYSTORE_FILE")
        if (store != null && file(store).isFile) {
            create("release") {
                storeFile = file(store)
                storePassword = signingValue("storePassword", "HP_KEYSTORE_PASSWORD")
                keyAlias = signingValue("keyAlias", "HP_KEY_ALIAS")
                keyPassword = signingValue("keyPassword", "HP_KEY_PASSWORD")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfigs.findByName("release")?.let { signingConfig = it }
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildFeatures {
        compose = true
        buildConfig = true
    }

    packaging {
        // Extrae las libs a nativeLibraryDir (rutas reales en disco para dlopen/BionicLibraryStager).
        jniLibs.useLegacyPackaging = true
    }
}

kotlin {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.15.0")
    implementation("androidx.activity:activity-compose:1.10.1")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.8.7")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.8.7")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.9.0")
    implementation(platform("androidx.compose:compose-bom:2025.04.00"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.foundation:foundation")
    implementation("androidx.compose.material3:material3")

    testImplementation("junit:junit:4.13.2")
    testImplementation("org.jetbrains.kotlinx:kotlinx-coroutines-test:1.9.0")
}
