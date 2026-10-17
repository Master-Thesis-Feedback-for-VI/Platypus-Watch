package com.github.dayvil.platypus_watch

import com.github.dayvil.platypus_watch.presentation.model.DeviceType
import com.github.dayvil.platypus_watch.presentation.model.LogData
import com.github.dayvil.platypus_watch.presentation.model.RecMessage
import com.github.dayvil.platypus_watch.presentation.model.RecMessageVariants
import com.github.dayvil.platypus_watch.presentation.model.SendMessage
import kotlinx.serialization.SerializationException
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.jsonObject
import org.junit.Assert.assertEquals
import org.junit.Test

class LogDataTest {
    @Test
    fun profileIsWrappedWithJsonStringData() {
        val profile = RecMessage(RecMessageVariants.NEXT)
        val envelope = LogData(
            DeviceType.GalaxyWatch,
            "profile",
            Json.encodeToString(profile)
        )

        val encoded = Json.encodeToString(envelope)
        val fields = Json.parseToJsonElement(encoded).jsonObject
        assertEquals(JsonPrimitive("GalaxyWatch"), fields["device"])
        assertEquals(JsonPrimitive("profile"), fields["tag"])
        assertEquals(JsonPrimitive("{\"cmd\":1}"), fields["data"])
        val decoded = Json.decodeFromString<LogData>(encoded)
        assertEquals(profile, Json.decodeFromString<RecMessage>(decoded.data))
    }

    @Test
    fun hapticIsDecodedFromWrappedJsonString() {
        for (device in DeviceType.entries) {
            val encoded = """{"device":"${device.name}","tag":"haptic","data":"{\"increaseDiff\":-2}"}"""
            val envelope = Json.decodeFromString<LogData>(encoded)

            assertEquals(device, envelope.device)
            assertEquals(SendMessage(-2), Json.decodeFromString<SendMessage>(envelope.data))
        }
    }

    @Test(expected = SerializationException::class)
    fun nestedObjectDataIsRejected() {
        Json.decodeFromString<LogData>(
            """{"device":"IOsApp","tag":"haptic","data":{"increaseDiff":1}}"""
        )
    }
}
