#pragma once

/* Call once after NVS initialization. Owns Wi-Fi and the default event loop.
 * on_connected runs on the default event loop whenever an IP is obtained.
 */
void wifi_roulette_start(void (*on_connected)(void));
