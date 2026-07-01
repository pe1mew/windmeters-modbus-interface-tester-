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

static void mock_write(void * /*ctx*/, const uint8_t *buf, uint16_t len)
{
    uint16_t n = (len < MOCK_BUF_SIZE) ? len : (uint16_t)MOCK_BUF_SIZE;
    memcpy(s_last_tx, buf, n);
    s_last_tx_len = n;
    s_write_count++;
}

static uint16_t mock_read(void * /*ctx*/, uint8_t *buf, uint16_t max_len, uint16_t /*timeout_ms*/)
{
    if (s_queue_head >= s_queue_tail) {
        return 0;
    }
    mock_queue_entry_t *evt = &s_queue[s_queue_head++];
    if (evt->is_timeout) {
        return 0;
    }
    uint16_t n = (evt->len < max_len) ? evt->len : max_len;
    memcpy(buf, evt->data, n);
    return n;
}

mb_transport_t mock_transport = { mock_write, mock_read, 0 };

void mock_transport_reset(void)
{
    s_queue_head = 0;
    s_queue_tail = 0;
    memset(s_queue, 0, sizeof(s_queue));
    memset(s_last_tx, 0, sizeof(s_last_tx));
    s_last_tx_len = 0;
    s_write_count = 0;
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
