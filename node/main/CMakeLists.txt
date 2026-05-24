set(SRCS
    "main.c"
    "neopixel.c"
    "endstop.c"
    "config_loader.c"
    "gatt_server.c"
    "led_effects.c"
    "ota_handler.c"
    "wifi_node.c"
)

if(CONFIG_KLIPNODE_VARIANT_FULL)
    list(APPEND SRCS "heater.c")
endif()

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS
        "."
        "../../shared/protocol"
    REQUIRES
        led_strip
        bt
        nvs_flash
        spiffs
        app_update
        json
        esp_driver_gpio
        esp_wifi
        esp_event
        esp_netif
        esp_driver_ledc
)
