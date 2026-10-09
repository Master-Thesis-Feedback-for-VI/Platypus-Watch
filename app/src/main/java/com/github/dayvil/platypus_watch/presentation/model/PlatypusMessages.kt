package com.github.dayvil.platypus_watch.presentation.model

import kotlinx.serialization.KSerializer
import kotlinx.serialization.Serializable
import kotlinx.serialization.SerializationException
import kotlinx.serialization.descriptors.PrimitiveKind
import kotlinx.serialization.descriptors.PrimitiveSerialDescriptor
import kotlinx.serialization.descriptors.SerialDescriptor
import kotlinx.serialization.encoding.Decoder
import kotlinx.serialization.encoding.Encoder

@Serializable(with = RecMessageVariantsSerializer::class)
enum class RecMessageVariants(val value: Int) {
    PREV(0),
    NEXT(1),
    EMPTY(2);

    companion object {
        fun fromValue(value: Int): RecMessageVariants =
            entries.firstOrNull { it.value == value }
                ?: throw SerializationException("Unknown RecMessageVariants value: $value")
    }
}

object RecMessageVariantsSerializer : KSerializer<RecMessageVariants> {
    override val descriptor: SerialDescriptor =
        PrimitiveSerialDescriptor("RecMessageVariants", PrimitiveKind.INT)

    override fun serialize(encoder: Encoder, value: RecMessageVariants) =
        encoder.encodeInt(value.value)

    override fun deserialize(decoder: Decoder): RecMessageVariants =
        RecMessageVariants.fromValue(decoder.decodeInt())
}

@Serializable
data class RecMessage(
    val cmd: RecMessageVariants
)

@Serializable
data class SendMessage(
    val increaseDiff: Int
)