#pragma once

#include <stdint.h>

#define KLIPPROTO_MAGIC 0x4B4C

typedef enum {
    KLIPCMD_PING = 0x01,
    KLIPCMD_PONG = 0x02,
    KLIPCMD_SET_MOTOR_SPEED = 0x10,
    KLIPCMD_READ_SENSOR = 0x20,
    KLIPCMD_SENSOR_RESPONSE = 0x21,
    KLIPCMD_SET_CONFIG = 0x30,
    KLIPCMD_GET_STATUS = 0x40,
    KLIPCMD_STATUS_RESPONSE = 0x41,
    KLIPCMD_ERROR = 0xFF,
} klip_command_t;

typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint8_t command;
    uint8_t length;
    uint8_t payload[];
} klip_packet_t;

#define KLIP_HEADER_SIZE offsetof(klip_packet_t, payload)
#define KLIP_MAX_PAYLOAD_SIZE 244
#define KLIP_MAX_PACKET_SIZE (KLIP_HEADER_SIZE + KLIP_MAX_PAYLOAD_SIZE)
