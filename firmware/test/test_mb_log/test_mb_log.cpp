/**
 * LIB-LOG Modbus Traffic Log — unit tests (native build)
 *
 * Run with:  pio test -e native -f test_mb_log
 *
 * No mocking needed — mb_log.cpp has zero hardware dependency, so this
 * relies on PlatformIO's normal Library Dependency Finder to link
 * lib/mb_log automatically (unlike test_mb_core, which pulls lib/mb_core
 * in manually — see test_mb_core/include_lib.cpp for why).
 */
#include <unity.h>
#include "mb_log.h"
#include <string.h>

static mb_log_entry_t make_entry(uint32_t ts, const char *summary)
{
    mb_log_entry_t e;
    memset(&e, 0, sizeof(e));
    e.timestamp_ms = ts;
    e.is_tx = true;
    e.raw_len = 0;
    strncpy(e.summary, summary, sizeof(e.summary) - 1);
    return e;
}

void setUp(void)
{
    mblog_init(3);
}

void tearDown(void) {}

void test_mblog_append_and_read_order(void)
{
    mb_log_entry_t a = make_entry(100, "first");
    mb_log_entry_t b = make_entry(200, "second");
    mb_log_entry_t c = make_entry(300, "third");

    mblog_append(&a);
    mblog_append(&b);
    mblog_append(&c);

    TEST_ASSERT_EQUAL_UINT32(3, mblog_count());

    mb_log_entry_t out[3];
    size_t n = mblog_get_recent(out, 3);

    TEST_ASSERT_EQUAL_UINT32(3, n);
    TEST_ASSERT_EQUAL_STRING("third", out[0].summary);   /* newest first */
    TEST_ASSERT_EQUAL_STRING("second", out[1].summary);
    TEST_ASSERT_EQUAL_STRING("first", out[2].summary);
}

void test_mblog_ring_wrap_drops_oldest(void)
{
    /* Capacity is 3 (set in setUp); append 5 -> oldest 2 are gone. */
    mb_log_entry_t entries[5] = {
        make_entry(1, "e1"), make_entry(2, "e2"), make_entry(3, "e3"),
        make_entry(4, "e4"), make_entry(5, "e5"),
    };
    for (int i = 0; i < 5; i++) {
        mblog_append(&entries[i]);
    }

    TEST_ASSERT_EQUAL_UINT32(3, mblog_count()); /* capped at capacity */

    mb_log_entry_t out[3];
    size_t n = mblog_get_recent(out, 3);

    TEST_ASSERT_EQUAL_UINT32(3, n);
    TEST_ASSERT_EQUAL_STRING("e5", out[0].summary);
    TEST_ASSERT_EQUAL_STRING("e4", out[1].summary);
    TEST_ASSERT_EQUAL_STRING("e3", out[2].summary); /* e1, e2 dropped */
}

void test_mblog_clear_empties_buffer(void)
{
    mb_log_entry_t a = make_entry(1, "a");
    mblog_append(&a);
    TEST_ASSERT_EQUAL_UINT32(1, mblog_count());

    mblog_clear();
    TEST_ASSERT_EQUAL_UINT32(0, mblog_count());

    mb_log_entry_t out[3];
    TEST_ASSERT_EQUAL_UINT32(0, mblog_get_recent(out, 3));
}

void test_mblog_get_recent_respects_max_count(void)
{
    mb_log_entry_t a = make_entry(1, "a");
    mb_log_entry_t b = make_entry(2, "b");
    mblog_append(&a);
    mblog_append(&b);

    mb_log_entry_t out[1];
    size_t n = mblog_get_recent(out, 1);
    TEST_ASSERT_EQUAL_UINT32(1, n);
    TEST_ASSERT_EQUAL_STRING("b", out[0].summary);
}

void test_mblog_total_appended_keeps_counting_past_ring_wrap(void)
{
    /* Capacity is 3 (set in setUp); append 5 -> mblog_count() caps at 3,
     * but a poller catching up on missed broadcasts needs the true total
     * appended, not the ring buffer's current occupancy. */
    mb_log_entry_t entries[5] = {
        make_entry(1, "e1"), make_entry(2, "e2"), make_entry(3, "e3"),
        make_entry(4, "e4"), make_entry(5, "e5"),
    };
    for (int i = 0; i < 5; i++) {
        mblog_append(&entries[i]);
    }

    TEST_ASSERT_EQUAL_UINT32(3, mblog_count());
    TEST_ASSERT_EQUAL_UINT32(5, mblog_total_appended());
}

void test_mblog_total_appended_survives_clear(void)
{
    /* mblog_clear() empties the visible buffer but must NOT reset the
     * total — a caller tracking "entries broadcast so far" against this
     * counter would otherwise re-broadcast (or double-count) around a
     * Clear button click. Only mblog_init() starts the total over. */
    mb_log_entry_t a = make_entry(1, "a");
    mb_log_entry_t b = make_entry(2, "b");
    mblog_append(&a);
    mblog_append(&b);

    mblog_clear();

    TEST_ASSERT_EQUAL_UINT32(0, mblog_count());
    TEST_ASSERT_EQUAL_UINT32(2, mblog_total_appended());
}

void test_mblog_total_appended_reset_by_init(void)
{
    mb_log_entry_t a = make_entry(1, "a");
    mblog_append(&a);
    TEST_ASSERT_EQUAL_UINT32(1, mblog_total_appended());

    mblog_init(3);

    TEST_ASSERT_EQUAL_UINT32(0, mblog_total_appended());
}

int main(int /*argc*/, char ** /*argv*/)
{
    UNITY_BEGIN();
    RUN_TEST(test_mblog_append_and_read_order);
    RUN_TEST(test_mblog_ring_wrap_drops_oldest);
    RUN_TEST(test_mblog_clear_empties_buffer);
    RUN_TEST(test_mblog_get_recent_respects_max_count);
    RUN_TEST(test_mblog_total_appended_keeps_counting_past_ring_wrap);
    RUN_TEST(test_mblog_total_appended_survives_clear);
    RUN_TEST(test_mblog_total_appended_reset_by_init);
    return UNITY_END();
}
