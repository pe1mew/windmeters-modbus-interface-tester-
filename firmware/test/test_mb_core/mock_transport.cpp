#include "mock_transport.h"
#include <string.h>

#define MOCK_QUEUE_MAX 8
#define MOCK_BUF_SIZE  256

typedef struct {
    bool     is_timeout;
    uint8_t  data[MOCK_BUF_SIZE];
    uint16_t len;
} mock_queue_entry_t;

static mock_queue_entry_t s_queue[MOCK_QUEUE_MAX];
static int s_queue_head = 0;
static int s_queue_tail = 0;

static uint8_t s_last_tx[MOCK_BUF_SIZE];
static int     s_last_tx_len = 0;
static int     s_write_count = 0;

/* Stale-RX-flush modelling: s_staged_stale stands in for bytes a real UART
 * would have accumulated in its RX buffer before we transmit. mock_flush()
 * drains it (like Serial2's read loop); mock_write() flags it if it ever runs
 * while stale bytes are still queued — i.e. a write that beat its flush, the
 * exact ordering bug the pre-TX flush exists to prevent. */
static uint16_t s_staged_stale = 0;
static int      s_flush_count = 0;
static bool     s_write_saw_stale = false;

static void mock_write(void * /*ctx*/, const uint8_t *buf, uint16_t len)
{
    if (s_staged_stale > 0) {
        s_write_saw_stale = true; /* wrote before flushing — must never happen */
    }
    uint16_t n = (len < MOCK_BUF_SIZE) ? len : (uint16_t)MOCK_BUF_SIZE;
    memcpy(s_last_tx, buf, n);
    s_last_tx_len = n;
    s_write_count++;
}

static uint16_t mock_read(void * /*ctx*/, uint8_t *buf, uint16_t max_len, uint16_t /*timeout_ms*/)
{
    if (s_queue_head >= s_queue_tail) {
        return 0; /* nothing queued -> default to "no response" */
    }
    mock_queue_entry_t *evt = &s_queue[s_queue_head++];
    if (evt->is_timeout) {
        return 0;
    }
    uint16_t n = (evt->len < max_len) ? evt->len : max_len;
    memcpy(buf, evt->data, n);
    return n;
}

static uint16_t mock_flush(void * /*ctx*/)
{
    s_flush_count++;
    uint16_t n = s_staged_stale;
    s_staged_stale = 0; /* one flush drains everything currently buffered, like Serial2 */
    return n;
}

mb_transport_t mock_transport = { mock_write, mock_read, mock_flush, 0 };

void mock_transport_reset(void)
{
    s_queue_head = 0;
    s_queue_tail = 0;
    memset(s_queue, 0, sizeof(s_queue));
    memset(s_last_tx, 0, sizeof(s_last_tx));
    s_last_tx_len = 0;
    s_write_count = 0;
    s_staged_stale = 0;
    s_flush_count = 0;
    s_write_saw_stale = false;
}

void mock_transport_queue_response(const uint8_t *bytes, uint16_t len)
{
    if (s_queue_tail >= MOCK_QUEUE_MAX) {
        return;
    }
    mock_queue_entry_t *evt = &s_queue[s_queue_tail++];
    evt->is_timeout = false;
    uint16_t n = (len < MOCK_BUF_SIZE) ? len : (uint16_t)MOCK_BUF_SIZE;
    memcpy(evt->data, bytes, n);
    evt->len = n;
}

void mock_transport_queue_timeout(void)
{
    if (s_queue_tail >= MOCK_QUEUE_MAX) {
        return;
    }
    mock_queue_entry_t *evt = &s_queue[s_queue_tail++];
    evt->is_timeout = true;
    evt->len = 0;
}

int mock_transport_get_transmitted(uint8_t *buf, int max_len)
{
    int n = (s_last_tx_len < max_len) ? s_last_tx_len : max_len;
    memcpy(buf, s_last_tx, (size_t)n);
    return n;
}

int mock_transport_get_write_count(void)
{
    return s_write_count;
}

void mock_transport_stage_stale_bytes(uint16_t n)
{
    s_staged_stale = n;
}

int mock_transport_get_flush_count(void)
{
    return s_flush_count;
}

int mock_transport_write_saw_unflushed_stale(void)
{
    return s_write_saw_stale ? 1 : 0;
}
