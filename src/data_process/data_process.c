#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "../printf.h"
#include <stdint.h>
typedef union {
    float f;
    uint32_t u;
} float_union_t;
float_union_t temp;
char str1[255];
int temp_integer=256;
// 联合体用于安全访问浮点位模式

char *float_bits_to_str(uint32_t bits, char *sout) {
    // 提取符号、指数、尾数
    int32_t sign = (bits >> 31) & 0x1;
    int32_t exponent = ((bits >> 23) & 0xFF) - 127;  // 减去偏移量127
    uint32_t mantissa = bits & 0x7FFFFF;
    
    // 处理0
    if (exponent == -127 && mantissa == 0) {
        if (sign) {
            sout[0] = '-';
            sout[1] = '0';
            sout[2] = '.';
            sout[3] = '0';
            sout[4] = '0';
            sout[5] = '\0';
        } else {
            sout[0] = '0';
            sout[1] = '.';
            sout[2] = '0';
            sout[3] = '0';
            sout[4] = '\0';
        }
        return sout;
    }
    
    // 添加隐含的1（对于规格化数）
    uint32_t full_mantissa = mantissa;
    if (exponent != -127) {  // 不是非规格化数
        full_mantissa |= 0x800000;  // 设置隐含的1
    }
    
    // 计算整数和小数部分
    uint32_t integer_part = 0;
    uint32_t fraction_part = 0;
    
    if (exponent >= 0) {
        // 值 >= 1.0
        if (exponent <= 23) {
            // 整数部分 = 尾数右移(23-exponent)位
            integer_part = full_mantissa >> (23 - exponent);
            
            // 小数部分 = 尾数低(23-exponent)位 * 100 / 2^(23-exponent)
            uint32_t frac_mask = (1U << (23 - exponent)) - 1;
            uint32_t frac_bits = full_mantissa & frac_mask;
            
            // 使用32位运算计算小数部分
            // 先乘以100，然后除以2^(23-exponent)
            // 为了更好的精度，我们使用乘以100再右移的方法
            fraction_part = (frac_bits * 100) >> (23 - exponent);
            
            // 四舍五入：检查下一位
            // 计算第三位小数的位
            if (exponent <= 21) {  // 确保有第三位小数
                uint32_t third_digit_shift = (23 - exponent) - 3;
                if (third_digit_shift < 32) {
                    uint32_t third_digit = (frac_bits >> third_digit_shift) & 0x1;
                    if (third_digit) {
                        fraction_part++;
                        if (fraction_part >= 100) {
                            fraction_part = 0;
                            integer_part++;
                        }
                    }
                }
            }
        } else {
            // 只有整数部分
            integer_part = full_mantissa << (exponent - 23);
            fraction_part = 0;
        }
    } else {
        // 值 < 1.0
        integer_part = 0;
        int32_t shift = -exponent;
        if (shift <= 23) {
            uint32_t frac_bits = full_mantissa >> shift;
            fraction_part = (frac_bits * 100) >> (23 - shift);
            
            // 四舍五入
            if (shift <= 21) {  // 确保有第三位小数
                uint32_t third_digit_shift = (23 - shift) - 3;
                if (third_digit_shift < 32) {
                    uint32_t third_digit = (frac_bits >> third_digit_shift) & 0x1;
                    if (third_digit) {
                        fraction_part++;
                        if (fraction_part >= 100) {
                            fraction_part = 0;
                            integer_part = 1;  // 从0.999...进位到1
                        }
                    }
                }
            }
        } else {
            fraction_part = 0;
        }
    }
    
    // 构建字符串
    char *p = sout;
    
    // 添加符号
    if (sign && (integer_part != 0 || fraction_part != 0)) {
        *p++ = '-';
    }
    
    // 整数部分
    if (integer_part == 0) {
        *p++ = '0';
    } else {
        char temp[12];
        char *t = temp;
        uint32_t n = integer_part;
        
        // 提取数字（反向）
        while (n > 0) {
            *t++ = (n % 10) + '0';
            n /= 10;
        }
        
        // 反转得到正确顺序
        while (t > temp) {
            *p++ = *(--t);
        }
    }
    
    // 小数点和小数部分（总是2位）
    *p++ = '.';
    *p++ = (fraction_part / 10) + '0';
    *p++ = (fraction_part % 10) + '0';
    *p = '\0';
    
    return sout;
}

// 从浮点位模式中提取整数部分
static uint32_t float_bits_to_int(uint32_t bits) {
    // 提取符号、指数、尾数
    int32_t sign = (bits >> 31) & 0x1;
    int32_t exponent = ((bits >> 23) & 0xFF) - 127;  // 减去偏移量127
    uint32_t mantissa = bits & 0x7FFFFF;
    
    // 处理0
    if (exponent == -127 && mantissa == 0) {
        return 0;
    }
    
    // 添加隐含的1（对于规格化数）
    uint32_t full_mantissa = mantissa;
    if (exponent != -127) {  // 不是非规格化数
        full_mantissa |= 0x800000;  // 设置隐含的1
    }
    
    // 计算整数部分
    uint32_t integer_part = 0;
    
    if (exponent >= 0) {
        if (exponent <= 23) {
            // 整数部分 = 尾数右移(23-exponent)位
            integer_part = full_mantissa >> (23 - exponent);
        } else {
            // 只有整数部分
            integer_part = full_mantissa << (exponent - 23);
        }
    }
    // 如果exponent < 0，整数部分为0
    
    // 如果原始是负数，我们需要返回负数
    // 注意：这里我们只返回绝对值，符号在外部处理
    return integer_part;
}

void refresh_temp_str(char *str){
    char temp_str[20];
    uint32_t temp_int=0;
    temp_int=float_bits_to_int(temp.u);
    float_bits_to_str(temp.u,temp_str);
    //float_to_str(temp, temp_str);
    //sprintf(str, "Temp: %d.%d", temp_integer/10, abs(temp_integer%10));
    sprintf(str, "Temp: %s %d", temp_str , temp_int);
}
void update_temp(float updatetemp){
    temp.f=updatetemp;
}