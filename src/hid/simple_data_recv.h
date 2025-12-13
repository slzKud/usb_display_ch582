#include <stdint.h>
#define MAX_PACKET_SIZE 60
#define MAX_DATA_COUNT 11
typedef enum {
    DATA_TYPE_TEMPERATURE = 0x01,
    DATA_TYPE_HUMIDITY    = 0x02,
    DATA_TYPE_PRESSURE    = 0x03,
    DATA_TYPE_VOLTAGE     = 0x04,
    DATA_TYPE_CURRENT     = 0x05,
    DATA_TYPE_SPEED       = 0x06,
    DATA_TYPE_POSITION    = 0x07,
    DATA_TYPE_CUSTOM      = 0xFF
} DataType;

// 数据点结构
typedef struct {
    DataType type;
    float value;
} DataPoint;

// 解析后的数据
typedef struct {
    uint8_t data_count;
    DataPoint data_points[MAX_DATA_COUNT];
} ParsedData;

int parse_packet(const uint8_t* packet, size_t packet_len, ParsedData* parsed);