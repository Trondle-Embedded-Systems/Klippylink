#pragma once

#include <stdint.h>
#include "klip_protocol.h"

void command_parser_init(void);
void command_parser_feed(const uint8_t *data, size_t len);
