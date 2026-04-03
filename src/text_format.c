#include "text_format.h"
#include "variant/variant.h"
#include "data_process/data_process.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

// 检查格式符是否与 variant 类型匹配
static int format_specifier_matches(char spec, enum VarType type) {
    switch (spec) {
        case 'd': return type == TYPE_INT;
        case 'f': return type == TYPE_FLOAT;
        case 's': return type == TYPE_STR;
        case 'b': return type == TYPE_BOOL;
        default:  return 0;
    }
}

// 将单个 variant 按指定格式符格式化到缓冲区
static void format_with_specifier(char *buf, int buf_size, char spec, const struct Variant *var) {
    switch (spec) {
        case 'd':
            if (var->type == TYPE_INT) {
                snprintf(buf, buf_size, "%d", var->data.i);
            } else {
                // 类型不匹配，输出默认格式化值
                format_variant(buf, buf_size, var, NULL);
            }
            break;
        case 'f':
            if (var->type == TYPE_FLOAT) {
                // 使用 float_bits_to_str 避免 printf float 依赖
                float_bits_to_str(*(uint32_t *)&var->data.f, buf);
            } else {
                format_variant(buf, buf_size, var, NULL);
            }
            break;
        case 's':
            if (var->type == TYPE_STR) {
                snprintf(buf, buf_size, "%s", var->data.str);
            } else {
                format_variant(buf, buf_size, var, NULL);
            }
            break;
        case 'b':
            if (var->type == TYPE_BOOL) {
                snprintf(buf, buf_size, "%s", var->data.b ? "true" : "false");
            } else {
                format_variant(buf, buf_size, var, NULL);
            }
            break;
        default:
            // 未知格式符，输出默认值
            format_variant(buf, buf_size, var, NULL);
            break;
    }
}

void format_dynamic_text(const char *template, const struct Variant *var,
                         char *out_buf, int out_size) {
    if (template == NULL || var == NULL || out_buf == NULL || out_size <= 0) {
        return;
    }

    int out_pos = 0;
    const char *p = template;

    while (*p != '\0' && out_pos < out_size - 1) {
        if (*p == '%' && *(p + 1) != '\0') {
            char spec = *(p + 1);
            if (spec == 'd' || spec == 'f' || spec == 's' || spec == 'b') {
                // 格式化 variant 值到临时缓冲区
                char tmp[32];
                format_with_specifier(tmp, sizeof(tmp), spec, var);

                // 拷贝到输出缓冲区
                int len = strlen(tmp);
                if (out_pos + len >= out_size) {
                    len = out_size - 1 - out_pos;
                }
                memcpy(out_buf + out_pos, tmp, len);
                out_pos += len;
                p += 2;  // 跳过 % 和格式符
                continue;
            }
        }
        // 普通字符，原样输出
        out_buf[out_pos++] = *p++;
    }

    out_buf[out_pos] = '\0';
}
