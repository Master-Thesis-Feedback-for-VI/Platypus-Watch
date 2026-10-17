[group('esp')]
set-target:
    cd ./lilygo_platypus && idf.py set-target esp32s3

[group('esp')]
clean-esp:
    cd ./lilygo_platypus && rm -rf build/

[group('esp')]
flash: clean-esp set-target
    cd ./lilygo_platypus && idf.py build flash

[group('esp')]
flash-watch: clean-esp set-target
    cd ./lilygo_platypus && idf.py build flash monitor

[group('esp')]
build-esp: clean-esp set-target
    cd ./lilygo_platypus && idf.py build
    
haptic cmd="1":
    #!/usr/bin/env bash
    set -euo pipefail
    host="${MQTT_URL#*://}"; host="${host%%/*}"; host="${host%%:*}"
    for side in left right; do
      npx mqtt pub \
        -t "IoT/device/haptic_$side" \
        -m "{\"device\":\"IOsApp\",\"tag\":\"IoT/device/haptic_$side\",\"data\":\"{\\\"increaseDiff\\\":{{cmd}}}\"}" \
        -h "$host" \
        -p 443 \
        --protocol wss \
        -u "$MQTT_USER_AGENT_NAME" \
        -P "$MQTT_PASSWORD"
    done
