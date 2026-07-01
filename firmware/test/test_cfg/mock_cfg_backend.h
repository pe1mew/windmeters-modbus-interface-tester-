/**
 * @file mock_cfg_backend.h
 * @brief In-memory fake cfg_backend_t for the native unit-test build.
 *
 * Each key remembers whichever type it was last set() with (get_u32 on a
 * key last set via set_str, or a never-set key, returns the default) —
 * models real Preferences behaviour closely enough for these tests.
 *
 * mock_cfg_reset() is test-harness isolation between test cases (wipes
 * everything so tests don't see each other's data); it's not the same
 * thing as the product's own cfg_reset_defaults(), which goes through the
 * backend's clear() and is exercised as a test subject, not a fixture.
 */
#pragma once

#include "cfg_backend.h"

/** @brief Wipe all stored entries. Call from setUp() for test isolation. */
void mock_cfg_reset(void);

extern cfg_backend_t mock_cfg_backend;
