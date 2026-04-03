#ifndef VARIANT_H
#define VARIANT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 类型枚举
enum VarType {
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_BOOL,
    TYPE_STR
};

// 联合体结构
struct Variant {
    enum VarType type;
    union {
        int i;
        float f;
        bool b;
        char str[16];       // 固定长度16，存储字符串（含结尾'\0'）
    } data;
};

/**
 * 将 Variant 中的值格式化到字符串缓冲区。
 * @param buffer 目标缓冲区
 * @param size   缓冲区大小
 * @param var    数据指针
 * @param format 可选的格式字符串，为 NULL 时使用默认格式
 */
void format_variant(char *buffer, size_t size, const struct Variant *var, const char *format);

/**
 * 打包一个 Variant 为二进制包。
 * @param buffer 输出缓冲区
 * @param max_len 缓冲区最大长度
 * @param var    数据指针
 * @return 实际使用的字节数，失败返回 -1
 */
int pack_packet(unsigned char *buffer, size_t max_len, const struct Variant *var);

/**
 * 打包多个 Variant 到同一个缓冲区。
 * @param buffer 输出缓冲区
 * @param max_len 缓冲区最大长度
 * @param vars   数据指针数组
 * @param count  数据个数
 * @return 实际使用的总字节数，失败返回 -1
 */
int pack_multiple_packets(unsigned char *buffer, size_t max_len,
                          const struct Variant *vars[], int count);

/**
 * 从缓冲区解析一个包，返回包的长度（即下一个包的偏移）。
 * @param var     输出解析结果
 * @param buffer  输入缓冲区
 * @param buf_len 缓冲区长度
 * @return 包的长度（字节），失败返回 -1
 */
int parse_next_packet(struct Variant *var, const unsigned char *buffer, size_t buf_len);

/**
 * 解析整个缓冲区中的所有包，对每个包调用回调函数。
 * @param buffer   输入缓冲区
 * @param buf_len  缓冲区长度
 * @param callback 回调函数，参数为解析出的 Variant 和用户数据
 * @param user     用户数据，传递给回调函数
 * @return 成功返回 0，失败返回 -1
 */
int parse_packet_stream(const unsigned char *buffer, size_t buf_len,
                        void (*callback)(const struct Variant *var, void *user),
                        void *user);

#endif // VARIANT_H