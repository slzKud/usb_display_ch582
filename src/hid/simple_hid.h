#ifndef __SIMPLE_HID_H__
#define __SIMPLE_HID_H__

#include "../variant/variant.h"

extern struct Variant variant_slots[16];
extern uint8_t Ready;

#define SUCCESS 0
#define FAILED 1

#define PARSE_STATUS_SUCCESS 0x00
#define PARSE_STATUS_FAILED 0x01
#define PARSE_STATUS_CHECKSUM_ERROR 0x02
#define PARSE_STATUS_PACKET_FORMAT_ERROR 0x03
#define PARSE_STATUS_INVALID_COMMAND 0x04
#define PARSE_STATUS_PACKET_DATA_LEN_INVALID 0x05

#define MAGIC_CODE_1 'D'
#define MAGIC_CODE_2 'G'
#define MAGIC_CODE_LENGTH 2

#define COMMAND_MCU_VER 0x0
#define COMMAND_PORT_INFO 0x1
#define COMMAND_MCU_OPT 0x2
#define COMMAND_SEND_DATA 0x3
#define COMMAND_RECV_DATA 0x4

#define MCU_OPT_COMMAND_GPIO_LEVEL 0x01
#define MCU_OPT_GPIO_SET_DIRECTION 0x02
#define MCU_OPT_SPI_W25Q64_COMMAND 0x03
#define MCU_OPT_DATAFLASH_COMMAND 0x04
#define MCU_OPT_VAR_SET_COMMAND 0x05
#ifdef ENABLE_FATFS
#define MCU_OPT_FATFS_COMMAND 0x06

#define FATFS_ACTION_CHECK_FORMATTED    0x01
#define FATFS_ACTION_SCAN_FILES         0x02
#define FATFS_ACTION_GET_FILE_INFO      0x03
#define FATFS_ACTION_READ_FILE          0x04
#define FATFS_ACTION_CREATE_FILE        0x05
#define FATFS_ACTION_WRITE_FILE         0x06
#define FATFS_ACTION_GET_FS_INFO        0x07
#define FATFS_ACTION_FORMAT             0x08

#define FATFS_MAX_FILES       64
#define FATFS_MAX_FILENAME    32
#define FATFS_READ_CHUNK_SIZE 52
#endif

#define MCU_GPIO_WRITE 0x1
#define MCU_GPIO_READ 0x0
#define MCU_GPIO_OUTPUT 0x0
#define MCU_GPIO_INPUT 0x1
#define MCU_GPIO_HIGH 0x1
#define MCU_GPIO_LOW 0x0

#define MCU_OPT_GPIO_SUCCESS 0x0
#define MCU_OPT_GPIO_FAILED 0x1
#define MCU_OPT_GPIO_NUMBER_INVALID 0x2

// GPIO映射表 - 将逻辑GPIO编号映射到实际CH58x引脚
// 目前只有PB4，后续按需扩展
typedef struct {
    uint32_t pin_mask;    // GPIO_Pin_x 宏
    uint8_t  port;        // 0=GPIOA, 1=GPIOB
} gpio_mapping_t;

#define GPIO_MAP_COUNT 1
extern const gpio_mapping_t gpio_map[GPIO_MAP_COUNT];

#define ERROR_RESP 0xFF

void print_hex(const char* label, const uint8_t* data, size_t length);
uint8_t make_checksum(uint8_t *data,uint8_t data_length);
int verify_checksum(uint8_t *data,uint8_t data_length);

uint8_t make_mcu_ver_req(uint8_t **req_data);
uint8_t make_send_data_req(int port_number,uint8_t *recv_data,uint8_t recv_data_length,uint8_t **req_data);
uint8_t make_mcu_opt_gpio_req(int opt,int gpio_number,int gpio_direction,int gpio_level,uint8_t **req_data);

uint8_t make_mcu_ver_resp(int major,int minor,uint8_t **req_data);
uint8_t make_simple_code_resp(uint8_t req_code,uint8_t simple_code,uint8_t **req_data);
uint8_t make_recv_data_resp(int port_number,uint8_t *recv_data,uint8_t recv_data_length,uint8_t **req_data);
uint8_t make_error_resp(int error_code,uint8_t **req_data);
uint8_t make_port_info_resp(int port_number,int connection,uint8_t **req_data);
uint8_t make_mcu_opt_gpio_resp(int status,int gpio_number,int gpio_direction,uint8_t **req_data);

int parse_data(uint8_t *data,uint8_t data_length,int port_number,uint8_t **resp_data,uint8_t *resp_data_length);

int handle_mcu_ver(uint8_t *data,uint8_t data_length,int port_number,uint8_t **resp_data,uint8_t *resp_data_length);
int handle_port_info(uint8_t *data,uint8_t data_length,int port_number,uint8_t **resp_data,uint8_t *resp_data_length);
int handle_mcu_opt_gpio_level(uint8_t *data,uint8_t data_length,int port_number,uint8_t **resp_data,uint8_t *resp_data_length);
int handle_mcu_opt_gpio_set_direction(uint8_t *data,uint8_t data_length,int port_number,uint8_t **resp_data,uint8_t *resp_data_length);
int handle_mcu_opt(uint8_t *data,uint8_t data_length,int port_number,uint8_t **resp_data,uint8_t *resp_data_length);
#ifdef ENABLE_FATFS
int handle_mcu_opt_FATFS_COMMAND(uint8_t *data,uint8_t data_length,int port_number,uint8_t **resp_data,uint8_t *resp_data_length);
#endif
int handle_send_data(uint8_t *data,uint8_t data_length,int port_number,uint8_t **resp_data,uint8_t *resp_data_length);
#endif