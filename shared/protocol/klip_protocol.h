#pragma once

#include <stdint.h>
#include <stddef.h>

#define KLIPPROTO_MAGIC         0x4B4C
#define KLIPPROTO_MAX_NODES     8

typedef enum {
    /* Basic */
    KLIPCMD_PING                = 0x01,
    KLIPCMD_PONG                = 0x02,

    /* Motor */
    KLIPCMD_SET_MOTOR_SPEED     = 0x10,

    /* Generic sensor */
    KLIPCMD_READ_SENSOR         = 0x20,
    KLIPCMD_SENSOR_RESPONSE     = 0x21,

    /* Configuration */
    KLIPCMD_SET_CONFIG          = 0x30,
    KLIPCMD_GET_STATUS          = 0x40,
    KLIPCMD_STATUS_RESPONSE     = 0x41,

    /* NeoPixel / WS2812B LEDs
     * SET       payload: [led_idx u8, r u8, g u8, b u8]
     * SET_ALL   payload: [r u8, g u8, b u8]
     * SET_RANGE payload: [start u8, count u8, r u8, g u8, b u8]
     */
    KLIPCMD_NEOPIXEL_SET        = 0x50,
    KLIPCMD_NEOPIXEL_SET_ALL    = 0x51,
    KLIPCMD_NEOPIXEL_SET_RANGE  = 0x52,

    /* LED zone effects (zone = contiguous range on one strip running an effect)
     * LED_ZONE_SET  payload: klip_led_zone_t
     * LED_ZONE_CLR  payload: [zone_id u8]
     */
    KLIPCMD_LED_ZONE_SET        = 0x53,
    KLIPCMD_LED_ZONE_CLR        = 0x54,

    /* Endstop
     * QUERY     payload: [endstop_idx u8]
     * STATE     payload: [endstop_idx u8, triggered u8]
     * SUBSCRIBE payload: [endstop_idx u8, enable u8]
     */
    KLIPCMD_ENDSTOP_QUERY       = 0x60,
    KLIPCMD_ENDSTOP_STATE       = 0x61,
    KLIPCMD_ENDSTOP_SUBSCRIBE   = 0x62,

    /* Device info
     * INFO_REQ  no payload
     * INFO_RESP payload: JSON string (null-terminated)
     */
    KLIPCMD_DEVICE_INFO_REQ     = 0x70,
    KLIPCMD_DEVICE_INFO_RESP    = 0x71,

    /* OTA firmware update
     * OTA_BEGIN  payload: [size u32 LE, crc32 u32 LE]
     * OTA_DATA   payload: firmware chunk (up to 244 bytes)
     * OTA_END    no payload
     * OTA_STATUS payload: [status u8]  0=ok 1=error 2=in-progress
     */
    KLIPCMD_OTA_BEGIN           = 0x80,
    KLIPCMD_OTA_DATA            = 0x81,
    KLIPCMD_OTA_END             = 0x82,
    KLIPCMD_OTA_STATUS          = 0x83,

    /* WiFi control (node-side)
     * WIFI_ENABLE  payload: [ssid_len u8, ssid bytes, pass_len u8, pass bytes]
     * WIFI_STATUS  payload: [ip_addr 4 bytes LE] or [0,0,0,0] if down
     */
    KLIPCMD_WIFI_ENABLE         = 0xA0,
    KLIPCMD_WIFI_STATUS         = 0xA1,

    /* Heater / PID control (VARIANT_FULL nodes only)
     * HEATER_SET   payload: klip_heater_cfg_t
     * HEATER_STATE payload: [temp_current f32 LE, temp_target f32 LE, duty u8]
     */
    KLIPCMD_HEATER_SET          = 0xB0,
    KLIPCMD_HEATER_STATE        = 0xB1,

    /* Heartbeat — no payload; node must reply HEARTBEAT within 1 s */
    KLIPCMD_HEARTBEAT           = 0xC0,

    KLIPCMD_ERROR               = 0xFF,
} klip_command_t;

/* OTA status codes */
#define KLIP_OTA_OK             0x00
#define KLIP_OTA_ERROR          0x01
#define KLIP_OTA_IN_PROGRESS    0x02

typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint8_t  command;
    uint8_t  length;
    uint8_t  payload[];
} klip_packet_t;

#define KLIP_HEADER_SIZE        offsetof(klip_packet_t, payload)
#define KLIP_MAX_PAYLOAD_SIZE   244
#define KLIP_MAX_PACKET_SIZE    (KLIP_HEADER_SIZE + KLIP_MAX_PAYLOAD_SIZE)

/* Payload structs */

typedef struct __attribute__((packed)) {
    uint8_t led_idx;
    uint8_t r, g, b;
} klip_neopixel_set_t;

typedef struct __attribute__((packed)) {
    uint8_t r, g, b;
} klip_neopixel_set_all_t;

typedef struct __attribute__((packed)) {
    uint8_t start;
    uint8_t count;
    uint8_t r, g, b;
} klip_neopixel_set_range_t;

/* LED zone effect IDs (must match led_effect_id_t in led_effects.h) */
#define KLIP_EFFECT_SOLID    0
#define KLIP_EFFECT_BLINK    1
#define KLIP_EFFECT_BREATHE  2
#define KLIP_EFFECT_RAINBOW  3
#define KLIP_EFFECT_CHASE    4
#define KLIP_EFFECT_WIPE     5
#define KLIP_EFFECT_TWINKLE  6

typedef struct __attribute__((packed)) {
    uint8_t zone_id;
    uint8_t strip_idx;
    uint8_t start;
    uint8_t count;
    uint8_t effect;
    uint8_t r,  g,  b;   /* primary color   */
    uint8_t r2, g2, b2;  /* secondary color */
    uint8_t brightness;  /* 0–255           */
    uint8_t speed;       /* 0–255           */
} klip_led_zone_t;

typedef struct __attribute__((packed)) {
    uint8_t endstop_idx;
    uint8_t triggered;
} klip_endstop_state_t;

typedef struct __attribute__((packed)) {
    uint32_t size;
    uint32_t crc32;
} klip_ota_begin_t;

typedef struct __attribute__((packed)) {
    float target_temp;
    float kp, ki, kd;
} klip_heater_cfg_t;

typedef struct __attribute__((packed)) {
    float   temp_current;
    float   temp_target;
    uint8_t duty;
} klip_heater_state_t;
