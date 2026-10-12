#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize once before handling MQTT messages. Missing hardware disables
 * haptics without preventing the rest of the application from running.
 */
void haptic_init(void);

/* Queue a strength (clamped to 0..10). Zero stops playback immediately;
 * nonzero commands update the amplitude and restart the 5-second timeout
 * without blocking the MQTT event handler.
 */
void apply_haptic(int strength);

#ifdef __cplusplus
}
#endif
