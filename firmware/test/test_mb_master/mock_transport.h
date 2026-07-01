/**
 * @file mock_transport.h
 * @brief Same fake mb_transport_t as test_mb_core/mock_transport.h.
 *
 * Duplicated rather than shared across test directories — PlatformIO
 * compiles each test/<name>/ directory as its own isolated binary, so
 * keeping each one self-contained (no cross-test-directory relative
 * includes) matches how the other test dirs in this project are built.
 */
#pragma once

#include <stdint.h>
#include "../../lib/mb_core/mb_transport.h"

void mock_transport_reset(void);
void mock_transport_queue_response(const uint8_t *bytes, uint16_t len);
void mock_transport_queue_timeout(void);
int mock_transport_get_transmitted(uint8_t *buf, int max_len);
int mock_transport_get_write_count(void);

extern mb_transport_t mock_transport;
