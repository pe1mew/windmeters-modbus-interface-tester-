#include "mb_frame.h"

uint16_t mb_crc16(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t pos = 0; pos < len; pos++) {
        crc ^= (uint16_t)buf[pos];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x0001) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001);
            } else {
                crc = (uint16_t)(crc >> 1);
            }
        }
    }
    return crc;
}

/** @brief Append the CRC16 of the first @p len bytes of @p buf, low byte first. */
static void append_crc(uint8_t *buf, uint16_t len)
{
    uint16_t crc = mb_crc16(buf, len);
    buf[len]     = (uint8_t)(crc & 0xFF);
    buf[len + 1] = (uint8_t)(crc >> 8);
}

/** @brief Verify the trailing 2 CRC bytes of a received frame of length @p len. */
static bool crc_ok(const uint8_t *buf, uint16_t len)
{
    if (len < 2) {
        return false;
    }
    uint16_t computed = mb_crc16(buf, (uint16_t)(len - 2));
    uint16_t received = (uint16_t)((uint16_t)buf[len - 2] | ((uint16_t)buf[len - 1] << 8));
    return computed == received;
}

uint16_t mb_build_read_request(uint8_t *out, uint8_t addr, uint8_t fc,
                                uint16_t start, uint16_t count)
{
    out[0] = addr;
    out[1] = fc;
    out[2] = (uint8_t)(start >> 8);
    out[3] = (uint8_t)(start & 0xFF);
    out[4] = (uint8_t)(count >> 8);
    out[5] = (uint8_t)(count & 0xFF);
    append_crc(out, 6);
    return 8;
}

uint16_t mb_build_write_single_request(uint8_t *out, uint8_t addr,
                                        uint16_t reg, uint16_t value)
{
    out[0] = addr;
    out[1] = 0x06;
    out[2] = (uint8_t)(reg >> 8);
    out[3] = (uint8_t)(reg & 0xFF);
    out[4] = (uint8_t)(value >> 8);
    out[5] = (uint8_t)(value & 0xFF);
    append_crc(out, 6);
    return 8;
}

uint16_t mb_build_write_multiple_request(uint8_t *out, uint8_t addr,
                                          uint16_t start, uint8_t count,
                                          const uint16_t *values)
{
    out[0] = addr;
    out[1] = 0x10;
    out[2] = (uint8_t)(start >> 8);
    out[3] = (uint8_t)(start & 0xFF);
    out[4] = (uint8_t)(count >> 8);
    out[5] = (uint8_t)(count & 0xFF);
    uint8_t byte_count = (uint8_t)(count * 2);
    out[6] = byte_count;
    for (uint8_t i = 0; i < count; i++) {
        out[7 + i * 2]     = (uint8_t)(values[i] >> 8);
        out[7 + i * 2 + 1] = (uint8_t)(values[i] & 0xFF);
    }
    uint16_t payload_len = (uint16_t)(7 + byte_count);
    append_crc(out, payload_len);
    return (uint16_t)(payload_len + 2);
}

mb_frame_status_t mb_parse_read_response(const uint8_t *buf, uint16_t len,
                                          uint8_t expect_addr, uint8_t expect_fc,
                                          uint8_t expect_count, uint16_t *out,
                                          uint8_t *exception_code)
{
    if (len < 5) {
        return MB_FRAME_ERR_FRAMING;
    }
    if (!crc_ok(buf, len)) {
        return MB_FRAME_ERR_CRC;
    }
    if (buf[0] != expect_addr) {
        return MB_FRAME_ERR_FRAMING;
    }

    if (buf[1] == (uint8_t)(expect_fc | 0x80)) {
        if (len != 5) {
            return MB_FRAME_ERR_FRAMING;
        }
        if (exception_code != 0) {
            *exception_code = buf[2];
        }
        return MB_FRAME_ERR_EXCEPTION;
    }
    if (buf[1] != expect_fc) {
        return MB_FRAME_ERR_FRAMING;
    }

    uint8_t byte_count = buf[2];
    if (byte_count != (uint8_t)(expect_count * 2)) {
        return MB_FRAME_ERR_FRAMING;
    }
    if (len != (uint16_t)(3 + byte_count + 2)) {
        return MB_FRAME_ERR_FRAMING;
    }

    for (uint8_t i = 0; i < expect_count; i++) {
        out[i] = (uint16_t)(((uint16_t)buf[3 + i * 2] << 8) | (uint16_t)buf[3 + i * 2 + 1]);
    }
    return MB_FRAME_OK;
}

mb_frame_status_t mb_parse_write_single_response(const uint8_t *buf, uint16_t len,
                                                  uint8_t expect_addr,
                                                  uint16_t expect_reg,
                                                  uint16_t expect_value,
                                                  uint8_t *exception_code)
{
    if (len < 5) {
        return MB_FRAME_ERR_FRAMING;
    }
    if (!crc_ok(buf, len)) {
        return MB_FRAME_ERR_CRC;
    }
    if (buf[0] != expect_addr) {
        return MB_FRAME_ERR_FRAMING;
    }

    if (buf[1] == (0x06 | 0x80)) {
        if (len != 5) {
            return MB_FRAME_ERR_FRAMING;
        }
        if (exception_code != 0) {
            *exception_code = buf[2];
        }
        return MB_FRAME_ERR_EXCEPTION;
    }
    if (buf[1] != 0x06 || len != 8) {
        return MB_FRAME_ERR_FRAMING;
    }

    uint16_t reg_echo   = (uint16_t)(((uint16_t)buf[2] << 8) | buf[3]);
    uint16_t value_echo = (uint16_t)(((uint16_t)buf[4] << 8) | buf[5]);
    if (reg_echo != expect_reg || value_echo != expect_value) {
        return MB_FRAME_ERR_FRAMING;
    }
    return MB_FRAME_OK;
}

mb_frame_status_t mb_parse_write_multiple_response(const uint8_t *buf, uint16_t len,
                                                    uint8_t expect_addr,
                                                    uint16_t expect_start,
                                                    uint8_t expect_count,
                                                    uint8_t *exception_code)
{
    if (len < 5) {
        return MB_FRAME_ERR_FRAMING;
    }
    if (!crc_ok(buf, len)) {
        return MB_FRAME_ERR_CRC;
    }
    if (buf[0] != expect_addr) {
        return MB_FRAME_ERR_FRAMING;
    }

    if (buf[1] == (0x10 | 0x80)) {
        if (len != 5) {
            return MB_FRAME_ERR_FRAMING;
        }
        if (exception_code != 0) {
            *exception_code = buf[2];
        }
        return MB_FRAME_ERR_EXCEPTION;
    }
    if (buf[1] != 0x10 || len != 8) {
        return MB_FRAME_ERR_FRAMING;
    }

    uint16_t start_echo = (uint16_t)(((uint16_t)buf[2] << 8) | buf[3]);
    uint16_t count_echo = (uint16_t)(((uint16_t)buf[4] << 8) | buf[5]);
    if (start_echo != expect_start || count_echo != expect_count) {
        return MB_FRAME_ERR_FRAMING;
    }
    return MB_FRAME_OK;
}
