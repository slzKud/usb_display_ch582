#include "variant.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../data_process/data_process.h"

// 辅助函数：读取/写入小端16位整数
static uint16_t read_le16(const unsigned char *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void write_le16(unsigned char *p, uint16_t val) {
    p[0] = (unsigned char)(val & 0xFF);
    p[1] = (unsigned char)((val >> 8) & 0xFF);
}

void format_variant(char *buffer, size_t size, const struct Variant *var, const char *format) {
    if (format == NULL) {
        switch (var->type) {
            case TYPE_INT:
                snprintf(buffer, size, "%d", var->data.i);
                break;
            case TYPE_FLOAT:
                float_bits_to_str(*(uint32_t *)&var->data.f, buffer);
                break;
            case TYPE_BOOL:
                snprintf(buffer, size, "%s", var->data.b ? "true" : "false");
                break;
            case TYPE_STR:
                snprintf(buffer, size, "%s", var->data.str);
                break;
            default:
                snprintf(buffer, size, "<unknown type>");
        }
        return;
    }

    switch (var->type) {
        case TYPE_INT:
            snprintf(buffer, size, format, var->data.i);
            break;
        case TYPE_FLOAT:
            // 避免 snprintf 处理 float，使用 float_bits_to_str
            float_bits_to_str(*(uint32_t *)&var->data.f, buffer);
            break;
        case TYPE_BOOL:
            snprintf(buffer, size, format, var->data.b ? "true" : "false");
            break;
        case TYPE_STR:
            snprintf(buffer, size, format, var->data.str);
            break;
        default:
            snprintf(buffer, size, format, "<unknown type>");
    }
}

int pack_packet(unsigned char *buffer, size_t max_len, const struct Variant *var) {
    if (buffer == NULL || var == NULL || max_len < 3) {
        return -1;
    }

    int data_len = 0;
    const unsigned char *data_ptr = NULL;
    unsigned char tmp_bool;

    switch (var->type) {
        case TYPE_INT:
            data_len = sizeof(int);
            data_ptr = (const unsigned char *)&var->data.i;
            break;
        case TYPE_FLOAT:
            data_len = sizeof(float);
            data_ptr = (const unsigned char *)&var->data.f;
            break;
        case TYPE_BOOL:
            data_len = 1;
            tmp_bool = var->data.b ? 1 : 0;
            data_ptr = &tmp_bool;
            break;
        case TYPE_STR:
            data_len = (int)strlen(var->data.str);
            if (data_len > 15) {
                return -1;   // 字符串过长
            }
            data_ptr = (const unsigned char *)var->data.str;
            break;
        default:
            return -1;
    }

    size_t total_len = 3 + data_len;
    if (total_len > max_len) {
        return -1;
    }

    buffer[0] = (unsigned char)var->type;
    write_le16(buffer + 1, (uint16_t)data_len);
    if (data_len > 0) {
        memcpy(buffer + 3, data_ptr, data_len);
    }

    return (int)total_len;
}

int pack_multiple_packets(unsigned char *buffer, size_t max_len,
                          const struct Variant *vars[], int count) {
    if (buffer == NULL || vars == NULL || count <= 0) {
        return -1;
    }

    size_t offset = 0;
    for (int i = 0; i < count; i++) {
        int ret = pack_packet(buffer + offset, max_len - offset, vars[i]);
        if (ret < 0) {
            return -1;
        }
        offset += ret;
    }
    return (int)offset;
}

int parse_next_packet(struct Variant *var, const unsigned char *buffer, size_t buf_len) {
    if (var == NULL || buffer == NULL || buf_len < 3) {
        return -1;
    }

    uint8_t type = buffer[0];
    if (type > TYPE_STR) {
        return -1;
    }

    uint16_t data_len = read_le16(buffer + 1);
    if (buf_len < 3 + data_len) {
        return -2;
    }

    const unsigned char *data = buffer + 3;
    var->type = (enum VarType)type;

    switch (type) {
        case TYPE_INT:
            if (data_len != sizeof(int)) return -1;
            memcpy(&var->data.i, data, sizeof(int));
            break;
        case TYPE_FLOAT:
            if (data_len != sizeof(float)) return -1;
            memcpy(&var->data.f, data, sizeof(float));
            break;
        case TYPE_BOOL:
            if (data_len != 1) return -1;
            var->data.b = (data[0] != 0);
            break;
        case TYPE_STR:
            if (data_len > 15) return -1;
            memcpy(var->data.str, data, data_len);
            var->data.str[data_len] = '\0';
            break;
        default:
            return -3;
    }

    return (int)(3 + data_len);
}

int parse_packet_stream(const unsigned char *buffer, size_t buf_len,
                        void (*callback)(const struct Variant *var, void *user),
                        void *user) {
    size_t offset = 0;
    while (offset < buf_len) {
        struct Variant var;
        int pkt_len = parse_next_packet(&var, buffer + offset, buf_len - offset);
        if (pkt_len < 0) {
            return -1;
        }
        if (callback) {
            callback(&var, user);
        }
        offset += pkt_len;
    }
    return 0;
}