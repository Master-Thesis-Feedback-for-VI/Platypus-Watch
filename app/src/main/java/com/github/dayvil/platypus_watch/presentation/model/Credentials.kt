package com.github.dayvil.platypus_watch.presentation.model

import com.github.dayvil.platypus_watch.BuildConfig

class Credentials {
    var username: String = BuildConfig.MQTT_USER_AGENT_NAME
        private set

    var password: String = BuildConfig.MQTT_PASSWORD
        private set
}