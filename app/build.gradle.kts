plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
    alias(libs.plugins.kotlin.serialization)
}

val env = rootProject.file(".env").readLines()
    .filter { it.isNotBlank() && !it.trimStart().startsWith("#") }.associate { line ->
        val (key, value) = line.split("=", limit = 2)
        key.trim() to value.trim()
    }

fun buildConfigString(value: String): String =
    "\"" + value
        .replace("\\", "\\\\")
        .replace("\"", "\\\"") + "\""

android {
    namespace = "com.github.dayvil.platypus_watch"
    compileSdk {
        version = release(37)
    }

    defaultConfig {
        applicationId = "com.github.dayvil.platypus_watch"
        minSdk = 35
        targetSdk = 37
        versionCode = 1
        versionName = "1.0"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    buildTypes {
        release {
            optimization {
                enable = true
                packageScope = setOf("androidx.**", "kotlin.**", "kotlinx.**")
            }
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    buildFeatures {
        compose = true
        buildConfig = true
    }

    defaultConfig {
        buildConfigField(
            "String",
            "MQTT_USER_AGENT_NAME",
            buildConfigString(requireNotNull(env["MQTT_USER_AGENT_NAME"]))
        )

        buildConfigField(
            "String",
            "MQTT_PASSWORD",
            buildConfigString(requireNotNull(env["MQTT_PASSWORD"]))
        )

        buildConfigField(
            "String",
            "MQTT_URL",
            buildConfigString(requireNotNull(env["MQTT_URL"]))
        )

        buildConfigField(
            "String",
            "TOPIC_RIGHT",
            buildConfigString(requireNotNull(env["TOPIC_RIGHT"]))
        )

        buildConfigField(
            "String",
            "TOPIC_PROFILE_SWITCH",
            buildConfigString(requireNotNull(env["TOPIC_PROFILE_SWITCH"]))
        )
    }
}

dependencies {
    val composeBom = platform(libs.androidx.compose.bom)

    implementation(libs.androidx.core.ktx)
    implementation(libs.kotlinx.serialization.json)
    implementation(libs.androidx.activity.compose)
    implementation(libs.kempmobil.mqtt.core)
    implementation(libs.kempmobil.mqtt.client)
    implementation(libs.kempmobil.mqtt.client.ws)
    implementation(libs.ktor.client.cio)

    // The app and instrumentation tests each need the BOM's version constraints.
    //noinspection GradleDuplicateDependency
    implementation(composeBom)
    implementation(libs.androidx.compose.foundation)
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.ui.tooling.preview)
    implementation(libs.androidx.wear.compose.material3)
    debugImplementation(libs.androidx.compose.ui.tooling)
    debugImplementation(libs.androidx.compose.ui.test.manifest)
    testImplementation(libs.junit)
    //noinspection GradleDuplicateDependency
    androidTestImplementation(composeBom)
    androidTestImplementation(libs.androidx.compose.ui.test.junit4)
    androidTestImplementation(libs.androidx.junit)

    // Coroutines
    implementation(libs.kotlinx.coroutines.core)
    implementation(libs.kotlinx.coroutines.android)

    // Compose lifecycle integration
    implementation(libs.androidx.lifecycle.runtime.compose)
    implementation(libs.androidx.lifecycle.viewmodel.compose)

    // Coroutine testing
    testImplementation(libs.kotlinx.coroutines.test)
}
