package com.github.dayvil.platypus_watch.presentation.view

import android.util.Log
import androidx.compose.foundation.layout.Column
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.rotary.onRotaryScrollEvent
import androidx.wear.compose.material3.Text
import com.github.dayvil.platypus_watch.presentation.PlatypusMQTT
import com.github.dayvil.platypus_watch.presentation.model.RecMessage
import com.github.dayvil.platypus_watch.presentation.model.RecMessageVariants
import kotlinx.coroutines.launch

const val TAG = "MainScreenView"

@Composable
fun MainScreenView() {
    val mqttClient = remember { PlatypusMQTT() }
    var isConnected by remember { mutableStateOf(false) }
    var rotation by remember { mutableFloatStateOf(0f) }

    val scope = rememberCoroutineScope()

    DisposableEffect(mqttClient) {
        onDispose { mqttClient.close() }
    }

    LaunchedEffect(mqttClient) {
        val connection = mqttClient.connectAndSubscribe()
        if (connection.isFailure) {
            Log.e(TAG, "MQTT connection/subscription failed", connection.exceptionOrNull())
            return@LaunchedEffect
        }
        isConnected = true

        mqttClient.hapticMessages.collect {
            Log.i(TAG, "$it")
        }
    }

    Column(
        modifier = Modifier
            .onRotaryScrollEvent { event ->
                rotation += event.verticalScrollPixels
                val profile = if (event.verticalScrollPixels > 0f) {
                    RecMessage(cmd = RecMessageVariants.NEXT)
                } else {
                    RecMessage(cmd = RecMessageVariants.PREV)
                }

                scope.launch {
                    mqttClient.publishProfile(profile)
                        .onSuccess { Log.i(TAG, "MQTT profile publish response: $it") }
                        .onFailure { Log.e(TAG, "MQTT profile publish failed", it) }
                }

                true
            }) {
        Text(
            "Client is: ${
                if (isConnected) {
                    "Connected"
                } else {
                    "Disconnected"
                }
            }"
        )

        Text("Rotation: $rotation")
    }
}
