#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "simple_data_recv.h"
// 解析数据包
int parse_packet(const uint8_t* packet, size_t packet_len, ParsedData* parsed) {
    // 检查参数
    if (packet == NULL || parsed == NULL) {
        return -1;
    }
    
    // 检查最小长度：至少需要1字节的DATA_COUNT
    if (packet_len < 1) {
        return -2;
    }
    
    // 获取数据点数量
    parsed->data_count = packet[0];
    
    // 检查数据点数量是否合理
    if (parsed->data_count == 0) {
        return 0;  // 空数据包，正常返回
    }
    
    if (parsed->data_count > MAX_DATA_COUNT) {
        return -3;  // 数据点太多
    }
    
    // 计算预期数据包长度
    size_t expected_len = 1 + parsed->data_count * (1 + 4);  // DATA_COUNT + (TYPE + FLOAT)*N
    if (packet_len < expected_len) {
        return -4;  // 数据包长度不足
    }
    
    // 解析每个数据点
    size_t offset = 1;  // 跳过DATA_COUNT
    
    for (int i = 0; i < parsed->data_count; i++) {
        // 读取DATA_TYPE
        parsed->data_points[i].type = (DataType)packet[offset];
        offset++;
        
        // 读取4字节浮点数
        uint32_t float_bytes;
        memcpy(&float_bytes, &packet[offset], 4);
        offset += 4;
        
        // 转换浮点数
        memcpy(&parsed->data_points[i].value, &packet[offset-4], 4);
    }
    
    return 0;  // 成功
}