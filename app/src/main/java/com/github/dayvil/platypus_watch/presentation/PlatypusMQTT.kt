package com.github.dayvil.platypus_watch.presentation

import android.content.Context
import android.net.Uri
import android.util.Log
import com.github.dayvil.platypus_watch.presentation.model.Credentials
import com.github.dayvil.platypus_watch.presentation.model.RecMessage
import com.github.dayvil.platypus_watch.presentation.model.SendMessage
import info.mqtt.android.service.MqttAndroidClient
import kotlinx.serialization.json.Json
import org.eclipse.paho.client.mqttv3.IMqttActionListener
import org.eclipse.paho.client.mqttv3.IMqttDeliveryToken
import org.eclipse.paho.client.mqttv3.IMqttToken
import org.eclipse.paho.client.mqttv3.MqttCallbackExtended
import org.eclipse.paho.client.mqttv3.MqttConnectOptions
import org.eclipse.paho.client.mqttv3.MqttMessage
import java.util.UUID

class PlatypusMQTT(ctx: Context, url: Uri, credentials: Credentials) {
    val clientId = "android-" + UUID.randomUUID().toString().take(12)
    val client = MqttAndroidClient(ctx, url.toString(), clientId)
    val opts = MqttConnectOptions().apply {
        userName = credentials.userName
        password = credentials.password.toCharArray()
        isAutomaticReconnect = true
        isCleanSession = true
    }

    fun connect() {
        this.client.connect(this.opts)
        this.client.setCallback(object : MqttCallbackExtended {
            override fun connectComplete(reconnect: Boolean, serverURI: String) {
                subscribe("IoT/device/haptic_left")
            }

            override fun messageArrived(topic: String, message: MqttMessage) {
                val text = String(message.payload, Charsets.UTF_8)
                val json = Json
                val msg = json.decodeFromString<SendMessage>(text)

                Log.d("MQTT", "$topic: $msg")
            }

            override fun connectionLost(cause: Throwable?) {
                Log.w("MQTT", "Connection lost", cause)
            }

            override fun deliveryComplete(token: IMqttDeliveryToken?) {}
        })
    }

    fun publish(msg: RecMessage) {
        val json = Json
        val toSend = json.encodeToString(msg)
        val message = MqttMessage(toSend.toByteArray(Charsets.UTF_8)).apply {
            this.qos = 1
            isRetained = true
        }
        this.client.publish("IoT/device/profile_swap", message)
    }

    private fun subscribe(topic: String, qos: Int = 1) {
        client.subscribe(topic, qos, null, object : IMqttActionListener {
            override fun onSuccess(token: IMqttToken?) {
                Log.d("MQTT", "Subscribed to $topic")
            }

            override fun onFailure(token: IMqttToken?, e: Throwable?) {
                Log.e("MQTT", "Subscribe failed", e)
            }
        })
    }
}