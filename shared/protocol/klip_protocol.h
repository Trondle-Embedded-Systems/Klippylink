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

typedef struct __attribute__((packed)) {
    uint8_t endstop_idx;
    uint8_t triggered;
} klip_endstop_state_t;

typedef struct __attribute__((packed)) {
    uint32_t size;
    uint32_t crc32;
} klip_ota_begin_t;
