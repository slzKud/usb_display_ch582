#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "../printf.h"
#include <stdint.h>
float temp=256.0;
char str1[255];
int temp_integer=256;
// 极简版：将float视为定点数*100，只处理2位小数
char *simple_float_to_str(float val, char *sout) {
    char buffer[16];
    char *p = buffer;
    
    // 将浮点数乘以100并四舍五入（这里假设有基本整数运算）
    // 注意：实际环境中val是float，这里需要特殊处理
    // 简单处理：提取整数和小数部分
    
    // 由于没有浮点运算，我们用整数近似
    // 方法：将float视为整数运算
    uint32_t bits = *(uint32_t*)&val;
    int32_t sign = (bits >> 31) & 0x1;
    int32_t exponent = ((bits >> 23) & 0xFF) - 127;
    uint32_t mantissa = bits & 0x7FFFFF | 0x800000;
    
    // 简化计算：获取整数部分
    int32_t integer;
    int32_t fraction;
    
    if (exponent >= 0) {
        integer = mantissa << exponent >> 23;  // 近似计算
        fraction = ((mantissa << exponent) & 0x7FFFFF) * 100 >> 23;
    } else {
        integer = 0;
        fraction = (mantissa * 100) >> (23 - exponent);
    }
    
    // 处理进位
    if (fraction >= 100) {
        integer++;
        fraction -= 100;
    }
    
    // 构建字符串
    p = sout;
    
    // 符号
    if (sign) {
        *p++ = '-';
    }
    
    // 整数部分
    if (integer == 0) {
        *p++ = '0';
    } else {
        char temp[12];
        char *t = temp;
        int32_t n = integer;
        
        while (n > 0) {
            *t++ = (n % 10) + '0';
            n /= 10;
        }
        
        while (t > temp) {
            *p++ = *(--t);
        }
    }
    
    // 小数点和小数部分
    *p++ = '.';
    *p++ = (fraction / 10) + '0';
    *p++ = (fraction % 10) + '0';
    *p = '\0';
    
    return sout;
}
void refresh_temp_str(char *str){
    char temp_str[20];
    simple_float_to_str(temp,temp_str);
    //float_to_str(temp, temp_str);
    //sprintf(str, "Temp: %d.%d", temp_integer/10, abs(temp_integer%10));
    sprintf(str, "Temp: %s", temp_str);
}
void update_temp(float updatetemp){
    temp=updatetemp;
}