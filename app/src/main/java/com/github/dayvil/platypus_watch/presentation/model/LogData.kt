package com.github.dayvil.platypus_watch.presentation.model

import kotlinx.serialization.Serializable

@Serializable
enum class DeviceType {
    IOsApp,
    LillyGo,
    GalaxyWatch
}

@Serializable
data class LogData(
    val device: DeviceType,
    val tag: String,
    val data: String
)
