package com.github.dayvil.platypus_watch.presentation

import com.github.dayvil.platypus_watch.BuildConfig
import com.github.dayvil.platypus_watch.presentation.model.Credentials
import com.github.dayvil.platypus_watch.presentation.model.RecMessage
import com.github.dayvil.platypus_watch.presentation.model.SendMessage
import de.kempmobil.ktor.mqtt.PublishRequest
import de.kempmobil.ktor.mqtt.PublishResponse
import de.kempmobil.ktor.mqtt.QoS
import de.kempmobil.ktor.mqtt.buildFilterList
import de.kempmobil.ktor.mqtt.packet.hasFailure
import de.kempmobil.ktor.mqtt.ws.MqttClient
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.filter
import kotlinx.coroutines.flow.map
import kotlinx.serialization.json.Json

class PlatypusMQTT : AutoCloseable {
    val hapticTopic = BuildConfig.TOPIC_RIGHT
    val profileTopic = BuildConfig.TOPIC_PROFILE_SWITCH
    val credentials = Credentials()
    val serverUri = BuildConfig.MQTT_URL

    private val client = MqttClient(serverUri) {
        clientId = "Platypus-Watch"
        username = credentials.username
        password = credentials.password
    }
    private val json = Json

    val hapticMessages: Flow<SendMessage> = client.publishedPackets
        .filter {
            it.topic.name == hapticTopic
        }
        .map {
            val content = it.payload.toByteArray().decodeToString()
            json.decodeFromString<SendMessage>(content)
        }

    suspend fun connectAndSubscribe(): Result<Unit> = runCatching {
        val connack = client.connect().getOrThrow()
        check(connack.isSuccess) {
            "MQTT connection rejected: ${connack.reason}"
        }

        val suback = client.subscribe(
            buildFilterList {
                +hapticTopic
            }
        ).getOrThrow()
        check(!suback.hasFailure) {
            "Subscription rejected: ${suback.reasons}"
        }
    }

    suspend fun publishProfile(profile: RecMessage): Result<PublishResponse> {
        val encodedProfile = json.encodeToString(profile)
        val publishRequest = PublishRequest(profileTopic) {
            desiredQoS = QoS.AT_LEAST_ONCE
            payload(encodedProfile)
        }

        return client.publish(publishRequest)
    }


    suspend fun disconnect() {
        client.disconnect()
    }

    override fun close() {
        client.close()
    }
}
