idf_component_register(
    SRCS "main.c" "wifi_roulette.c" "haptics.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_i2c esp_driver_ledc esp_timer json mqtt esp_wifi nvs_flash esp_event esp_netif esp-tls sensorlib
)

set(ENV_FILE "${CMAKE_SOURCE_DIR}/../.env")
if(EXISTS "${ENV_FILE}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ENV_FILE}")
  file(STRINGS "${ENV_FILE}" ENV_LINES)
  foreach(line IN LISTS ENV_LINES)
    if(line MATCHES "^[ \t]*([A-Za-z_][A-Za-z0-9_]*)=(.*)$")
      set(k "${CMAKE_MATCH_1}")
      string(REGEX REPLACE "^\"(.*)\"$" "\\1" v "${CMAKE_MATCH_2}")
      target_compile_definitions(${COMPONENT_LIB} PRIVATE "${k}=\"${v}\"")
    endif()
  endforeach()
else()
  message(FATAL_ERROR ".env not found")
endif()
