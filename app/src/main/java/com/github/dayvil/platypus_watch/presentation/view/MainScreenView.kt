package com.github.dayvil.platypus_watch.presentation.view

import android.util.Log
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.wear.compose.material3.Button
import androidx.wear.compose.material3.Icon
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

    Button(
        enabled = isConnected,
        onClick = {
            scope.launch {
                mqttClient.publishProfile(
                    profile = RecMessage(cmd = RecMessageVariants.NEXT)
                )
                    .onSuccess { Log.i(TAG, "MQTT profile publish response: $it") }
                    .onFailure { Log.e(TAG, "MQTT profile publish failed", it) }
            }
        }
    ) {
        Icon(
            imageVector = Icons.Default.Check,
            contentDescription = null
        )
        Spacer(Modifier.width(8.dp))
        Text("Next profile")
    }
}
