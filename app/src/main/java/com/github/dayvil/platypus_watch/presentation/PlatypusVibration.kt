package com.github.dayvil.platypus_watch.presentation

import android.content.Context
import android.os.VibrationEffect
import android.os.VibratorManager

class PlatypusVibration(context: Context, val durationMs: Long = 800) {
    private val vibrator = context
        .getSystemService(VibratorManager::class.java)
        .defaultVibrator

    private var activeAmplitude: Int? = null

    fun applyHaptics(strength: Int) {
        val level = strength.coerceIn(0, 10)

        if (!vibrator.hasVibrator()) return

        if (level == 0) {
            stop()
            return
        }

        val amplitude = if (vibrator.hasAmplitudeControl()) {
            40 + (level - 1) * (255 - 40) / 9
        } else {
            VibrationEffect.DEFAULT_AMPLITUDE
        }

        if (amplitude == activeAmplitude) return
        vibrator.vibrate(
            VibrationEffect.createWaveform(
                longArrayOf(durationMs),
                intArrayOf(amplitude),
                0
            )
        )
        activeAmplitude = amplitude
    }

    fun stop() {
        vibrator.cancel()
        activeAmplitude = null
    }

    fun supportsAmplitudeControl(): Boolean {
        return vibrator.hasAmplitudeControl()
    }
}
