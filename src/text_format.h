#ifndef __TEXT_FORMAT_H__
#define __TEXT_FORMAT_H__

#include "variant/variant.h"

/**
 * 将动态文本模板格式化为输出字符串。
 * 扫描 template 中的 %d/%f/%s/%b 格式符，
 * 用 var 的值替换。如果格式符与 var->type 不匹配，
 * 输出该 variant 的默认格式化值。
 * @param template  格式化模板，如 "Temp: %f"
 * @param var       对应的 variant 指针
 * @param out_buf   输出缓冲区
 * @param out_size  缓冲区大小
 */
void format_dynamic_text(const char *template, const struct Variant *var,
                         char *out_buf, int out_size);

#endif
