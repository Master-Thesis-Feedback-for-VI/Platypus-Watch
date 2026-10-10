package com.github.dayvil.platypus_watch.presentation

import android.content.Context
import android.os.VibrationEffect
import android.os.VibratorManager

class PlatypusVibration(context: Context, val delay: Long = 200) {
    private val vibrator = context
        .getSystemService(VibratorManager::class.java)
        .defaultVibrator

    fun applyHaptics(strength: Int) {
        val level = strength.coerceIn(0, 10)

        if (!vibrator.hasVibrator()) return

        if (level == 0) {
            vibrator.cancel()
            return
        }

        // Scale duration too: some watches cannot vary vibration amplitude.
        val duration = (delay * level / 10).coerceAtLeast(1L)
        val amplitude = if (vibrator.hasAmplitudeControl()) {
            40 + (level - 1) * (255 - 40) / 9
        } else {
            VibrationEffect.DEFAULT_AMPLITUDE
        }

        vibrator.vibrate(
            VibrationEffect.createOneShot(
                duration,
                amplitude
            )
        )
    }

    fun stop() {
        vibrator.cancel()
    }

    fun supportsAmplitudeControl(): Boolean {
        return vibrator.hasAmplitudeControl()
    }
}
