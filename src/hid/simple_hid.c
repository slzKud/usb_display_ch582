#include "../printf.h"
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "simple_hid.h"
#include "../w25qxx/w25qxx.h"
#include "../variant/variant.h"
#include "../ver.h"
#ifdef ENABLE_FATFS
#include "../fatfs/ff.h"
#endif
#include "CH58x_common.h"

uint8_t *recv_data_buffer = NULL;

// GPIO映射表: 逻辑编号 -> 实际CH58x引脚
// [0] = PB4
const gpio_mapping_t gpio_map[GPIO_MAP_COUNT] = {
    { GPIO_Pin_4, 1 },  // PB4
};

// 辅助函数: 根据映射表对引脚执行ModeCfg
static void gpio_apply_direction(uint8_t gpio_number, uint8_t direction)
{
    const gpio_mapping_t *m = &gpio_map[gpio_number];
    if (m->port == 0) {
        GPIOA_ModeCfg(m->pin_mask, direction == MCU_GPIO_OUTPUT ? GPIO_ModeOut_PP_5mA : GPIO_ModeIN_Floating);
    } else {
        GPIOB_ModeCfg(m->pin_mask, direction == MCU_GPIO_OUTPUT ? GPIO_ModeOut_PP_5mA : GPIO_ModeIN_Floating);
    }
}

// 辅助函数: 根据映射表对引脚执行Set/Reset
static void gpio_apply_level(uint8_t gpio_number, uint8_t level)
{
    const gpio_mapping_t *m = &gpio_map[gpio_number];
    if (m->port == 0) {
        if (level == MCU_GPIO_HIGH) GPIOA_SetBits(m->pin_mask);
        else GPIOA_ResetBits(m->pin_mask);
    } else {
        if (level == MCU_GPIO_HIGH) GPIOB_SetBits(m->pin_mask);
        else GPIOB_ResetBits(m->pin_mask);
    }
}

// 辅助函数: 根据映射表读取引脚电平
static uint8_t gpio_read_level(uint8_t gpio_number)
{
    const gpio_mapping_t *m = &gpio_map[gpio_number];
    if (m->port == 0) {
        return GPIOA_ReadPortPin(m->pin_mask) ? MCU_GPIO_HIGH : MCU_GPIO_LOW;
    } else {
        return GPIOB_ReadPortPin(m->pin_mask) ? MCU_GPIO_HIGH : MCU_GPIO_LOW;
    }
}

void print_hex(const char *label, const uint8_t *data, size_t length)
{
    printf("%s: ", label);
    for (size_t i = 0; i < length; i++)
    {
        printf("%02X ", data[i]);
    }
    printf("\n");
}

uint8_t make_checksum(uint8_t *data, uint8_t data_length)
{ // 这里的data_length不包含checksum
    int i = 0;
    uint8_t n = 0;
    for (i = 0; i < data_length; i++)
    {
        n += *(data + i);
        // //printf("plus:%x,%x\n",*(data+i),n);
    }
    // //printf("checksum:%x,%x\n",n,(n & 0xff));
    return (n & 0xff);
}
int verify_checksum(uint8_t *data, uint8_t data_length)
{ // 这里的data_length包含checksum
    uint8_t ori_checksum = *(data + (data_length - 1));
    uint8_t new_checksum = make_checksum(data, data_length - 1);
    if (ori_checksum == new_checksum)
        return SUCCESS;
    return FAILED;
}
uint8_t *make_header()
{
    uint8_t *req = NULL;
    req = malloc(sizeof(uint8_t) * 64);
    memset(req, 0, 64);
    *(req) = MAGIC_CODE_1;
    *(req + 1) = MAGIC_CODE_2;
    return req;
}
uint8_t make_mcu_ver_req(uint8_t **req_data)
{
    uint8_t *req = make_header();
    uint8_t *data = (req + MAGIC_CODE_LENGTH);
    uint8_t checksum = 0;
    *(data) = COMMAND_MCU_VER;
    *(data + 1) = 0x0;                        // 长度为0
    checksum = make_checksum(req, 2 + 1 + 1); // 2个魔法字，1个命令类型，1个长度
    *(data + 2) = checksum;
    *req_data = req;
    return 2 + 1 + 1 + 1;
}

uint8_t make_mcu_ver_resp(int major, int minor, uint8_t **req_data)
{
    uint8_t *req = make_header();
    uint8_t *data = (req + MAGIC_CODE_LENGTH);
    uint8_t checksum = 0;
    *(data) = COMMAND_MCU_VER | 0x20;
    *(data + 1) = 0x2;                            // 长度为2
    *(data + 2) = major;                          // 长度为0
    *(data + 3) = minor;                          // 长度为0
    checksum = make_checksum(req, 2 + 1 + 1 + 2); // 2个魔法字，1个命令类型，1个长度，2个数据
    *(data + 4) = checksum;
    *req_data = req;
    return 2 + 1 + 1 + 2 + 1;
}
uint8_t make_send_data_req(int port_number, uint8_t *recv_data, uint8_t recv_data_length, uint8_t **req_data)
{
    int resp_data_len = MAGIC_CODE_LENGTH;
    uint8_t *resp = make_header();
    uint8_t *data = (resp + MAGIC_CODE_LENGTH);
    uint8_t checksum = 0;
    *(data) = COMMAND_SEND_DATA;
    *(data + 1) = recv_data_length + 1; // 长度为N+1
    *(data + 2) = port_number;
    memcpy(data + 3, recv_data, recv_data_length);
    resp_data_len += (recv_data_length + 1 + 1 + 1);
    checksum = make_checksum(resp, resp_data_len);
    *(data + 3 + recv_data_length) = checksum;
    resp_data_len += 1;
    *req_data = resp;
    return resp_data_len;
}
uint8_t make_simple_code_resp(uint8_t req_code, uint8_t simple_code, uint8_t **req_data)
{
    uint8_t *req = make_header();
    uint8_t *data = (req + MAGIC_CODE_LENGTH);
    uint8_t checksum = 0;
    *(data) = req_code | 0x20;
    *(data + 1) = 0x1; // 长度为1
    *(data + 2) = simple_code;
    checksum = make_checksum(req, 2 + 1 + 1 + 1); // 2个魔法字，1个命令类型，1个长度,1个数据
    *(data + 3) = checksum;
    *req_data = req;
    return 2 + 1 + 1 + 1 + 1;
}
uint8_t make_recv_data_resp(int port_number, uint8_t *recv_data, uint8_t recv_data_length, uint8_t **req_data)
{
    int resp_data_len = MAGIC_CODE_LENGTH;
    uint8_t *resp = make_header();
    uint8_t *data = (resp + MAGIC_CODE_LENGTH);
    uint8_t checksum = 0;
    *(data) = COMMAND_RECV_DATA;
    *(data + 1) = recv_data_length + 1; // 长度为N+1
    *(data + 2) = port_number;
    memcpy(data + 3, recv_data, recv_data_length);
    resp_data_len += (recv_data_length + 1 + 1 + 1);
    checksum = make_checksum(resp, resp_data_len);
    *(data + 3 + recv_data_length) = checksum;
    resp_data_len += 1;
    *req_data = resp;
    return resp_data_len;
}
uint8_t make_error_resp(int error_code, uint8_t **req_data)
{
    uint8_t *req = make_header();
    uint8_t *data = (req + MAGIC_CODE_LENGTH);
    uint8_t checksum = 0;
    *(data) = ERROR_RESP;
    *(data + 1) = 0x1;                            // 长度为1
    *(data + 2) = error_code;                     // 错误码
    checksum = make_checksum(req, 2 + 1 + 1 + 1); // 2个魔法字，1个命令类型，1个长度，1个数据
    *(data + 3) = checksum;
    *req_data = req;
    return 2 + 1 + 1 + 1 + 1;
}

uint8_t make_port_info_resp(int port_number, int connection, uint8_t **req_data)
{
    uint8_t *req = make_header();
    uint8_t *data = (req + MAGIC_CODE_LENGTH);
    uint8_t checksum = 0;
    *(data) = COMMAND_PORT_INFO | 0x20;
    *(data + 1) = 0x4; // 长度为4
    *(data + 2) = port_number;
    *(data + 3) = connection;
    *(data + 4) = 0;
    *(data + 5) = 0;
    checksum = make_checksum(req, 2 + 1 + 1 + 4); // 2个魔法字，1个命令类型，1个长度,4个数据
    *(data + 6) = checksum;
    *req_data = req;
    return 2 + 1 + 1 + 4 + 1;
}

uint8_t make_mcu_opt_gpio_req(int opt, int gpio_number, int gpio_direction, int gpio_level, uint8_t **req_data)
{
    //printf("make_mcu_opt_gpio_req:opt=%d,gpio_number=%d,gpio_direction=%d,gpio_level=%d\n", opt, gpio_number, gpio_direction, gpio_level);
    uint8_t mcu_opt_gpio_req_array[4] = {0, 0, 0, 0};
    int mcu_opt_gpio_req_array_len = 4;
    mcu_opt_gpio_req_array[0] = opt;
    mcu_opt_gpio_req_array[1] = gpio_number;
    if (opt == MCU_OPT_COMMAND_GPIO_LEVEL)
    {
        mcu_opt_gpio_req_array[2] = gpio_direction;
        if (gpio_direction == MCU_GPIO_WRITE)
            mcu_opt_gpio_req_array[3] = gpio_level;
        mcu_opt_gpio_req_array_len = (gpio_direction == MCU_GPIO_WRITE) ? 4 : 3;
    }
    else if (opt == MCU_OPT_GPIO_SET_DIRECTION)
    {
        mcu_opt_gpio_req_array[2] = gpio_direction;
        mcu_opt_gpio_req_array_len = 3;
    }
    //print_hex("make_mcu_opt_gpio_req:", mcu_opt_gpio_req_array, mcu_opt_gpio_req_array_len);
    uint8_t *req = make_header();
    uint8_t *data = (req + MAGIC_CODE_LENGTH);
    uint8_t checksum = 0;
    uint8_t resp_data_len = 4;
    *(data) = COMMAND_MCU_OPT;
    *(data + 1) = mcu_opt_gpio_req_array_len; // 长度为N+1
    memcpy(data + 2, mcu_opt_gpio_req_array, mcu_opt_gpio_req_array_len);
    resp_data_len += (mcu_opt_gpio_req_array_len);
    checksum = make_checksum(req, resp_data_len);
    *(data + 2 + mcu_opt_gpio_req_array_len) = checksum;
    resp_data_len += 1;
    *req_data = req;
    return resp_data_len;
}
uint8_t make_mcu_opt_gpio_resp(int status, int gpio_number, int gpio_direction, uint8_t **req_data)
{
    uint8_t *req = make_header();
    uint8_t *data = (req + MAGIC_CODE_LENGTH);
    uint8_t checksum = 0;
    *(data) = COMMAND_MCU_OPT | 0x20;
    *(data + 1) = 0x3; // 长度为3
    *(data + 2) = status;
    *(data + 3) = gpio_number;
    *(data + 4) = gpio_direction;
    checksum = make_checksum(req, 2 + 1 + 1 + 3); // 2个魔法字，1个命令类型，1个长度,3个数据
    *(data + 5) = checksum;
    *req_data = req;
    return 2 + 1 + 1 + 3 + 1;
}
int handle_mcu_ver(uint8_t *data, uint8_t data_length, int port_number, uint8_t **resp_data, uint8_t *resp_data_length)
{
    uint8_t data_len = make_mcu_ver_resp(MAJOR_VER, MINOR_VER, resp_data);
    *resp_data_length = data_len;
    return PARSE_STATUS_SUCCESS;
}

int handle_port_info(uint8_t *data, uint8_t data_length, int port_number, uint8_t **resp_data, uint8_t *resp_data_length)
{
    int query_port_number = port_number;
    if (data_length >= 1)
        query_port_number = *(data);
    if (query_port_number > 1)
        return PARSE_STATUS_FAILED;
    //printf("handle_port_info:port %d\n", query_port_number);
    uint8_t data_len = 0;
    if (query_port_number == 0)
    {
        data_len = make_port_info_resp(0, Ready, resp_data);
    }
    else
    {
        // Port1 (U2Ready) 已移除，写死未连接
        data_len = make_port_info_resp(1, 0, resp_data);
    }
    *resp_data_length = data_len;
    return PARSE_STATUS_SUCCESS;
}
int handle_mcu_opt_gpio_level(uint8_t *data, uint8_t data_length, int port_number, uint8_t **resp_data, uint8_t *resp_data_length)
{
    //printf("handle_mcu_opt_gpio_level:data_length:%d\n", data_length);
    //print_hex("handle_mcu_opt_gpio_level:data", data, data_length);
    if (data_length < 3)
        return PARSE_STATUS_PACKET_DATA_LEN_INVALID;
    uint8_t gpio_number = *(data + 1);
    uint8_t gpio_rw_flag = *(data + 2);
    uint8_t gpio_level = 0;
    uint8_t data_len = 0;
    //printf("handle_mcu_opt_gpio_level:gpio_number:%d\n", gpio_number);
    //printf("handle_mcu_opt_gpio_level:gpio_rw_flag:%d\n", gpio_rw_flag);
    if (gpio_number >= GPIO_MAP_COUNT)
    {
        data_len = make_mcu_opt_gpio_resp(MCU_OPT_GPIO_NUMBER_INVALID, gpio_number, 0, resp_data);
        goto final;
    }
    if (gpio_rw_flag == MCU_GPIO_WRITE)
    {
        if (data_length < 4)
            return PARSE_STATUS_PACKET_DATA_LEN_INVALID;
        gpio_level = *(data + 3);
        if (gpio_level != MCU_GPIO_HIGH && gpio_level != MCU_GPIO_LOW)
        {
            data_len = make_mcu_opt_gpio_resp(MCU_OPT_GPIO_FAILED, gpio_number, gpio_level, resp_data);
            goto final;
        }
        gpio_apply_level(gpio_number, gpio_level);
    }
    else if (gpio_rw_flag == MCU_GPIO_READ)
    {
        gpio_level = gpio_read_level(gpio_number);
    }
    data_len = make_mcu_opt_gpio_resp(MCU_OPT_GPIO_SUCCESS, gpio_number, gpio_level, resp_data);
final:
    *resp_data_length = data_len;
    return PARSE_STATUS_SUCCESS;
}
int handle_mcu_opt_gpio_set_direction(uint8_t *data, uint8_t data_length, int port_number, uint8_t **resp_data, uint8_t *resp_data_length)
{
    if (data_length < 2)
        return PARSE_STATUS_PACKET_DATA_LEN_INVALID;
    uint8_t gpio_number = *(data + 1);
    uint8_t gpio_direction = *(data + 2);
    uint8_t data_len = 0;
    if (gpio_number >= GPIO_MAP_COUNT)
    {
        data_len = make_mcu_opt_gpio_resp(MCU_OPT_GPIO_NUMBER_INVALID, gpio_number, 0, resp_data);
        goto final;
    }
    if (gpio_direction != MCU_GPIO_INPUT && gpio_direction != MCU_GPIO_OUTPUT)
    {
        data_len = make_mcu_opt_gpio_resp(MCU_OPT_GPIO_FAILED, gpio_number, gpio_direction, resp_data);
        goto final;
    }
    gpio_apply_direction(gpio_number, gpio_direction);
    data_len = make_mcu_opt_gpio_resp(MCU_OPT_GPIO_SUCCESS, gpio_number, gpio_direction, resp_data);
final:
    *resp_data_length = data_len;
    return PARSE_STATUS_SUCCESS;
}
int handle_mcu_opt_SPI_W25Q64_COMMAND(uint8_t *data, uint8_t data_length, int port_number, uint8_t **resp_data, uint8_t *resp_data_length)
{
    // SPI_W25Q64_COMMAND
    /*
    操作SPI FLASH，可以根据偏移读取数据，或者使用FATFS读写SPI EEPROM里面文件
    0x03 [ACTION] [DATA1]
    ACTION:
    0x0 EEPROM_ID
    0x1 READ_RAW_DATA  [OFFSET(4bytes)] [LENGTH(2bytes)]
    0x2 WRITE_RAW_DATA [OFFSET(4bytes)] [LENGTH(2bytes)] [DATA]
    0x3 ERASE_CHIP     NO DATA1
    0x4 ERASE_BLOCK    [OFFSET(4bytes)]
    */
    if (data_length < 2)
        return PARSE_STATUS_PACKET_DATA_LEN_INVALID;

    uint8_t action = data[1];

    if (action == 0x0) // EEPROM_ID
    {
        uint8_t id[3] = {0};
        BSP_W25Qx_Read_ID(id);
        uint8_t *req = make_header();
        uint8_t *hdr = req + MAGIC_CODE_LENGTH;
        *hdr = COMMAND_MCU_OPT | 0x20;
        *(hdr + 1) = 0x5; // len=5: status(1) + action(1) + id(3)
        *(hdr + 2) = 0x00; // status OK
        *(hdr + 3) = action;
        memcpy(hdr + 4, id, 3);
        uint8_t checksum = make_checksum(req, 2 + 1 + 1 + 1 + 1 + 3);
        *(hdr + 7) = checksum;
        *resp_data = req;
        *resp_data_length = 2 + 1 + 1 + 1 + 1 + 3 + 1;
        return PARSE_STATUS_SUCCESS;
    }
    else if (action == 0x1) // READ_RAW_DATA [OFFSET(4)] [LENGTH(2)]
    {
        if (data_length < 7)
            return PARSE_STATUS_PACKET_DATA_LEN_INVALID;

        uint32_t offset = (uint32_t)data[2] | ((uint32_t)data[3] << 8) |
                          ((uint32_t)data[4] << 16) | ((uint32_t)data[5] << 24);
        uint16_t length = (uint16_t)data[6] | ((uint16_t)data[7] << 8);

        if (length == 0 || length > 52)
            return PARSE_STATUS_PACKET_DATA_LEN_INVALID;

        uint8_t *read_buf = malloc(length);
        if (read_buf == NULL)
            return PARSE_STATUS_FAILED;

        uint8_t ret = BSP_W25Qx_Read(read_buf, offset, length);
        if (ret != W25Qx_OK)
        {
            free(read_buf);
            uint8_t data_len = make_mcu_opt_gpio_resp(MCU_OPT_GPIO_FAILED, 0, 0, resp_data);
            *resp_data_length = data_len;
            return PARSE_STATUS_SUCCESS;
        }

        // resp: [MAGIC][CMD|0x20][LEN][STATUS][ACTION][DATA...][CHECKSUM]
        // LEN = 1(status) + 1(action) + length
        uint8_t resp_len_field = 1 + 1 + length;
        uint8_t *req = make_header();
        uint8_t *hdr = req + MAGIC_CODE_LENGTH;
        *hdr = COMMAND_MCU_OPT | 0x20;
        *(hdr + 1) = resp_len_field;
        *(hdr + 2) = 0x00; // status OK
        *(hdr + 3) = action;
        memcpy(hdr + 4, read_buf, length);
        uint8_t checksum = make_checksum(req, 2 + 1 + 1 + resp_len_field);
        *(hdr + 4 + length) = checksum;
        *resp_data = req;
        *resp_data_length = 2 + 1 + 1 + resp_len_field + 1;

        free(read_buf);
        return PARSE_STATUS_SUCCESS;
    }
    else if (action == 0x2) // WRITE_RAW_DATA [OFFSET(4)] [LENGTH(2)] [DATA] - 直接页编程，不自动擦除
    {
        if (data_length < 8)
            return PARSE_STATUS_PACKET_DATA_LEN_INVALID;

        uint32_t offset = (uint32_t)data[2] | ((uint32_t)data[3] << 8) |
                          ((uint32_t)data[4] << 16) | ((uint32_t)data[5] << 24);
        uint16_t length = (uint16_t)data[6] | ((uint16_t)data[7] << 8);

        if (length == 0 || (8 + length) > data_length)
            return PARSE_STATUS_PACKET_DATA_LEN_INVALID;

        // 自动按页拆分写入（调用方需先擦除对应块）
        BSP_W25Qx_Write(data + 8, offset, length);

        uint8_t *req = make_header();
        uint8_t *hdr = req + MAGIC_CODE_LENGTH;
        *hdr = COMMAND_MCU_OPT | 0x20;
        *(hdr + 1) = 0x2; // len=2: status(1) + action(1)
        *(hdr + 2) = 0x00; // status OK
        *(hdr + 3) = action;
        uint8_t checksum = make_checksum(req, 2 + 1 + 1 + 2);
        *(hdr + 4) = checksum;
        *resp_data = req;
        *resp_data_length = 2 + 1 + 1 + 2 + 1;
        return PARSE_STATUS_SUCCESS;
    }
    else if (action == 0x3) // ERASE_CHIP
    {
        uint8_t ret = BSP_W25Qx_Erase_Chip_NB();
        uint8_t status = (ret == W25Qx_OK) ? 0x00 : 0x01;

        uint8_t *req = make_header();
        uint8_t *hdr = req + MAGIC_CODE_LENGTH;
        *hdr = COMMAND_MCU_OPT | 0x20;
        *(hdr + 1) = 0x2;
        *(hdr + 2) = status;
        *(hdr + 3) = action;
        uint8_t checksum = make_checksum(req, 2 + 1 + 1 + 2);
        *(hdr + 4) = checksum;
        *resp_data = req;
        *resp_data_length = 2 + 1 + 1 + 2 + 1;
        return PARSE_STATUS_SUCCESS;
    }
    else if (action == 0x4) // ERASE_BLOCK [OFFSET(4)]
    {
        if (data_length < 6)
            return PARSE_STATUS_PACKET_DATA_LEN_INVALID;

        uint32_t offset = (uint32_t)data[2] | ((uint32_t)data[3] << 8) |
                          ((uint32_t)data[4] << 16) | ((uint32_t)data[5] << 24);
        // 按4KB块对齐
        offset &= ~(0xFFF);

        uint8_t ret = BSP_W25Qx_Erase_Block_NB(offset);
        uint8_t status = (ret == W25Qx_OK) ? 0x00 : 0x01;

        uint8_t *req = make_header();
        uint8_t *hdr = req + MAGIC_CODE_LENGTH;
        *hdr = COMMAND_MCU_OPT | 0x20;
        *(hdr + 1) = 0x2;
        *(hdr + 2) = status;
        *(hdr + 3) = action;
        uint8_t checksum = make_checksum(req, 2 + 1 + 1 + 2);
        *(hdr + 4) = checksum;
        *resp_data = req;
        *resp_data_length = 2 + 1 + 1 + 2 + 1;
        return PARSE_STATUS_SUCCESS;
    }
    else if (action == 0x5) // GET_STATUS - 查询芯片忙/就绪状态
    {
        uint8_t spi_status = BSP_W25Qx_GetStatus();
        uint8_t *req = make_header();
        uint8_t *hdr = req + MAGIC_CODE_LENGTH;
        *hdr = COMMAND_MCU_OPT | 0x20;
        *(hdr + 1) = 0x3; // len=3: status(1) + action(1) + spi_status(1)
        *(hdr + 2) = 0x00; // 命令执行状态 OK
        *(hdr + 3) = action;
        *(hdr + 4) = spi_status; // W25Qx_OK=0x00, W25Qx_BUSY=0x02
        uint8_t checksum = make_checksum(req, 2 + 1 + 1 + 1 + 1 + 1);
        *(hdr + 5) = checksum;
        *resp_data = req;
        *resp_data_length = 2 + 1 + 1 + 1 + 1 + 1 + 1;
        return PARSE_STATUS_SUCCESS;
    }

    return PARSE_STATUS_INVALID_COMMAND;
}
int handle_mcu_opt_DATAFLASH_COMMAND(uint8_t *data, uint8_t data_length, int port_number, uint8_t **resp_data, uint8_t *resp_data_length)
{
    // MCU_OPT_DATAFLASH_COMMAND
    /*
    读写CH582的DATA_FLASH (地址范围 0x0000~0x7FFF, 共32KB)
    0x04 [ACTION] [DATA1]
    ACTION:
    0x1 READ_DATA  [OFFSET(2bytes)] [LENGTH(2bytes)]
    0x2 WRITE_DATA [OFFSET(2bytes)] [LENGTH(2bytes)] [DATA]
    0x3 ERASE      NO DATA1 (擦除全部DataFlash)
    */
    if (data_length < 2)
        return PARSE_STATUS_PACKET_DATA_LEN_INVALID;

    uint8_t action = data[1];

    if (action == 0x1) // READ_DATA [OFFSET(2)] [LENGTH(2)]
    {
        if (data_length < 5)
            return PARSE_STATUS_PACKET_DATA_LEN_INVALID;

        uint16_t offset = (uint16_t)data[2] | ((uint16_t)data[3] << 8);
        uint16_t length = (uint16_t)data[4] | ((uint16_t)data[5] << 8);

        if (length == 0 || (uint32_t)offset + length > 0x8000)
            return PARSE_STATUS_PACKET_DATA_LEN_INVALID;

        // 最大返回数据长度受限于64字节HID包
        if (length > 52)
            length = 52;

        uint8_t *read_buf = malloc(length);
        if (read_buf == NULL)
            return PARSE_STATUS_FAILED;

        EEPROM_READ((uint32_t)offset, read_buf, length);

        // resp: [MAGIC][CMD|0x20][LEN][STATUS][ACTION][DATA...][CHECKSUM]
        uint8_t resp_len_field = 1 + 1 + length;
        uint8_t *req = make_header();
        uint8_t *hdr = req + MAGIC_CODE_LENGTH;
        *hdr = COMMAND_MCU_OPT | 0x20;
        *(hdr + 1) = resp_len_field;
        *(hdr + 2) = 0x00; // status OK
        *(hdr + 3) = action;
        memcpy(hdr + 4, read_buf, length);
        uint8_t checksum = make_checksum(req, 2 + 1 + 1 + resp_len_field);
        *(hdr + 4 + length) = checksum;
        *resp_data = req;
        *resp_data_length = 2 + 1 + 1 + resp_len_field + 1;

        free(read_buf);
        return PARSE_STATUS_SUCCESS;
    }
    else if (action == 0x2) // WRITE_DATA [OFFSET(2)] [LENGTH(2)] [DATA]
    {
        if (data_length < 6)
            return PARSE_STATUS_PACKET_DATA_LEN_INVALID;

        uint16_t offset = (uint16_t)data[2] | ((uint16_t)data[3] << 8);
        uint16_t length = (uint16_t)data[4] | ((uint16_t)data[5] << 8);

        if (length == 0 || (6 + length) > data_length ||
            (uint32_t)offset + length > 0x8000)
            return PARSE_STATUS_PACKET_DATA_LEN_INVALID;

        // 按256字节页对齐，读回-合并-擦除-写回，防止丢失页内其他数据
        uint16_t page_base = offset & ~(0xFF);
        uint16_t page_offset = offset - page_base;
        // 考虑跨页的情况
        uint16_t end_offset = page_offset + length;
        uint16_t total_pages = (end_offset + 255) / 256;

        uint8_t *page_buf = malloc(total_pages * 256);
        if (page_buf == NULL)
            return PARSE_STATUS_FAILED;

        uint32_t ret = 0;
        for (uint16_t i = 0; i < total_pages; i++)
        {
            uint16_t cur_page_base = page_base + i * 256;
            // 读回该页原有数据
            EEPROM_READ((uint32_t)cur_page_base, page_buf + i * 256, 256);
            // 擦除该页
            EEPROM_ERASE((uint32_t)cur_page_base, 256);
        }
        // 合并新数据到缓冲区
        memcpy(page_buf + page_offset, data + 6, length);
        // 写回所有页
        for (uint16_t i = 0; i < total_pages; i++)
        {
            uint16_t cur_page_base = page_base + i * 256;
            ret = EEPROM_WRITE((uint32_t)cur_page_base, page_buf + i * 256, 256);
            if (ret != 0)
                break;
        }
        free(page_buf);

        uint8_t status = (ret == 0) ? 0x00 : 0x01;

        uint8_t *req = make_header();
        uint8_t *hdr = req + MAGIC_CODE_LENGTH;
        *hdr = COMMAND_MCU_OPT | 0x20;
        *(hdr + 1) = 0x2; // len=2: status(1) + action(1)
        *(hdr + 2) = status;
        *(hdr + 3) = action;
        uint8_t checksum = make_checksum(req, 2 + 1 + 1 + 2);
        *(hdr + 4) = checksum;
        *resp_data = req;
        *resp_data_length = 2 + 1 + 1 + 2 + 1;
        return PARSE_STATUS_SUCCESS;
    }
    else if (action == 0x3) // ERASE (擦除全部DataFlash)
    {
        // DataFlash最小擦除单位为256字节(EEPROM_MIN_ER_SIZE), 按4KB块擦除
        uint32_t ret = 0;
        for (uint32_t addr = 0; addr < 0x8000; addr += 4096)
        {
            ret = EEPROM_ERASE(addr, 4096);
            if (ret != 0)
                break;
        }
        uint8_t status = (ret == 0) ? 0x00 : 0x01;

        uint8_t *req = make_header();
        uint8_t *hdr = req + MAGIC_CODE_LENGTH;
        *hdr = COMMAND_MCU_OPT | 0x20;
        *(hdr + 1) = 0x2;
        *(hdr + 2) = status;
        *(hdr + 3) = action;
        uint8_t checksum = make_checksum(req, 2 + 1 + 1 + 2);
        *(hdr + 4) = checksum;
        *resp_data = req;
        *resp_data_length = 2 + 1 + 1 + 2 + 1;
        return PARSE_STATUS_SUCCESS;
    }

    return PARSE_STATUS_INVALID_COMMAND;
}
// Variant变量存储区，支持ID 0xF0~0xFF，共16个变量槽
struct Variant variant_slots[16] = {0};
volatile uint32_t last_variant_recv_time = 0;
volatile uint8_t variant_data_valid = 0;

int handle_mcu_opt_VAR_SET_COMMAND(uint8_t *data, uint8_t data_length, int port_number, uint8_t **resp_data, uint8_t *resp_data_length)
{
    // MCU_OPT_VAR_SET_COMMAND
    /*
    使用variant库的相关函数，维护一个支持ID为0xf1-0xff的变量组合
    0x05 [VAR_ID(1byte)] [PACKED_VARIANT_DATA]
    VAR_ID范围: 0xF1~0xFF (共15个槽)
    PACKED_VARIANT_DATA: 由variant库的pack_packet生成的二进制数据
    写入时: [VAR_ID] [PACKED_VARIANT_DATA]
    读取时: [VAR_ID] (无额外数据)
    */
    if (data_length < 2)
        return PARSE_STATUS_PACKET_DATA_LEN_INVALID;

    uint8_t var_id = data[1];
    if (var_id < 0xF0 || var_id > 0xFF)
    {
        uint8_t *req = make_header();
        uint8_t *hdr = req + MAGIC_CODE_LENGTH;
        *hdr = COMMAND_MCU_OPT | 0x20;
        *(hdr + 1) = 0x2;
        *(hdr + 2) = 0x01; // status FAILED
        *(hdr + 3) = var_id;
        uint8_t checksum = make_checksum(req, 2 + 1 + 1 + 2);
        *(hdr + 4) = checksum;
        *resp_data = req;
        *resp_data_length = 2 + 1 + 1 + 2 + 1;
        return PARSE_STATUS_SUCCESS;
    }

    uint8_t slot_index = var_id - 0xF0;

    if (data_length == 2)
    {
        // 读取操作：无PACKED_VARIANT_DATA，返回当前存储的变量
        unsigned char pack_buf[32] = {0};
        int packed_len = pack_packet(pack_buf, sizeof(pack_buf), &variant_slots[slot_index]);

        if (packed_len < 0)
            packed_len = 0;

        // resp: [MAGIC][CMD|0x20][LEN][STATUS][VAR_ID][PACKED_DATA...][CHECKSUM]
        uint8_t resp_len_field = 1 + 1 + packed_len; // status + var_id + packed_data
        uint8_t *req = make_header();
        uint8_t *hdr = req + MAGIC_CODE_LENGTH;
        *hdr = COMMAND_MCU_OPT | 0x20;
        *(hdr + 1) = resp_len_field;
        *(hdr + 2) = 0x00; // status OK
        *(hdr + 3) = var_id;
        if (packed_len > 0)
            memcpy(hdr + 4, pack_buf, packed_len);
        uint8_t checksum = make_checksum(req, 2 + 1 + 1 + resp_len_field);
        *(hdr + 4 + packed_len) = checksum;
        *resp_data = req;
        *resp_data_length = 2 + 1 + 1 + resp_len_field + 1;
        return PARSE_STATUS_SUCCESS;
    }
    else
    {
        // 写入操作：data[2..]为PACKED_VARIANT_DATA
        struct Variant var;
        int parsed = parse_next_packet(&var, data + 2, data_length - 2);
        if (parsed < 0)
        {
            uint8_t *req = make_header();
            uint8_t *hdr = req + MAGIC_CODE_LENGTH;
            *hdr = COMMAND_MCU_OPT | 0x20;
            *(hdr + 1) = 0x2;
            *(hdr + 2) = 0x01-parsed; // status FAILED
            *(hdr + 3) = var_id;
            uint8_t checksum = make_checksum(req, 2 + 1 + 1 + 2);
            *(hdr + 4) = checksum;
            *resp_data = req;
            *resp_data_length = 2 + 1 + 1 + 2 + 1;
            return PARSE_STATUS_SUCCESS;
        }

        variant_slots[slot_index] = var;
        last_variant_recv_time = g_millis;
        variant_data_valid = 1;

        uint8_t *req = make_header();
        uint8_t *hdr = req + MAGIC_CODE_LENGTH;
        *hdr = COMMAND_MCU_OPT | 0x20;
        *(hdr + 1) = 0x2;
        *(hdr + 2) = 0x00; // status OK
        *(hdr + 3) = var_id;
        uint8_t checksum = make_checksum(req, 2 + 1 + 1 + 2);
        *(hdr + 4) = checksum;
        *resp_data = req;
        *resp_data_length = 2 + 1 + 1 + 2 + 1;
        return PARSE_STATUS_SUCCESS;
    }
}

// FatFS 文件系统命令
#ifdef ENABLE_FATFS
typedef struct {
    char name[FATFS_MAX_FILENAME + 1];
    uint32_t size;
    uint8_t attrib;
} FileEntry;

static FATFS fatfs_work;
static uint8_t fs_mounted = 0;
static FileEntry file_cache[FATFS_MAX_FILES];
static uint8_t file_count = 0;

static int ensure_mounted(void)
{
    if (fs_mounted) return 0;
    FRESULT fr = f_mount(&fatfs_work, "1:", 1);
    if (fr != FR_OK) return -1;
    fs_mounted = 1;
    return 0;
}

static void write_le32(uint8_t *p, uint32_t val)
{
    p[0] = (uint8_t)(val & 0xFF);
    p[1] = (uint8_t)((val >> 8) & 0xFF);
    p[2] = (uint8_t)((val >> 16) & 0xFF);
    p[3] = (uint8_t)((val >> 24) & 0xFF);
}

static void write_le16_local(uint8_t *p, uint16_t val)
{
    p[0] = (uint8_t)(val & 0xFF);
    p[1] = (uint8_t)((val >> 8) & 0xFF);
}

static void fatfs_scan_files(void)
{
    DIR dir;
    FILINFO fno;
    file_count = 0;

    if (f_opendir(&dir, "1:/") != FR_OK) return;

    while (file_count < FATFS_MAX_FILES) {
        if (f_readdir(&dir, &fno) != FR_OK || fno.fname[0] == 0) break;
        if (fno.fattrib & AM_DIR) continue;
        if (strncmp(fno.fname, "display_", 8) != 0) continue;
        if (strlen(fno.fname) > FATFS_MAX_FILENAME) continue;

        strncpy(file_cache[file_count].name, fno.fname, FATFS_MAX_FILENAME);
        file_cache[file_count].name[FATFS_MAX_FILENAME] = '\0';
        file_cache[file_count].size = (uint32_t)fno.fsize;
        file_cache[file_count].attrib = fno.fattrib;
        file_count++;
    }
    f_closedir(&dir);
}

static void fatfs_make_resp(uint8_t action, uint8_t status, const uint8_t *extra, uint8_t extra_len,
                            uint8_t **resp_data, uint8_t *resp_data_length)
{
    // resp: [MAGIC(2)][CMD=0x22][LEN][STATUS][ACTION][EXTRA...][CHECKSUM]
    uint8_t resp_len_field = 1 + 1 + extra_len; // status + action + extra
    uint8_t *req = make_header();
    uint8_t *hdr = req + MAGIC_CODE_LENGTH;
    *hdr = COMMAND_MCU_OPT | 0x20;
    *(hdr + 1) = resp_len_field;
    *(hdr + 2) = status;
    *(hdr + 3) = action;
    if (extra_len > 0 && extra != NULL)
        memcpy(hdr + 4, extra, extra_len);
    uint8_t checksum = make_checksum(req, 2 + 1 + 1 + resp_len_field);
    *(hdr + 4 + extra_len) = checksum;
    *resp_data = req;
    *resp_data_length = 2 + 1 + 1 + resp_len_field + 1;
}

int handle_mcu_opt_FATFS_COMMAND(uint8_t *data, uint8_t data_length, int port_number,
                                 uint8_t **resp_data, uint8_t *resp_data_length)
{
    if (data_length < 2)
        return PARSE_STATUS_PACKET_DATA_LEN_INVALID;

    uint8_t action = data[1];

    if (action == FATFS_ACTION_CHECK_FORMATTED) // 0x01
    {
        FRESULT fr = f_mount(&fatfs_work, "1:", 1);
        if (fr == FR_NO_FILESYSTEM) {
            fs_mounted = 0;
            fatfs_make_resp(action, 0x01, NULL, 0, resp_data, resp_data_length);
            return PARSE_STATUS_SUCCESS;
        }
        if (fr != FR_OK) {
            fatfs_make_resp(action, 0x01, NULL, 0, resp_data, resp_data_length);
            return PARSE_STATUS_SUCCESS;
        }
        fs_mounted = 1;
        DIR dir;
        fr = f_opendir(&dir, "1:/");
        if (fr == FR_OK) {
            f_closedir(&dir);
            fatfs_make_resp(action, 0x00, NULL, 0, resp_data, resp_data_length);
        } else {
            fatfs_make_resp(action, 0x01, NULL, 0, resp_data, resp_data_length);
        }
        return PARSE_STATUS_SUCCESS;
    }
    else if (action == FATFS_ACTION_SCAN_FILES) // 0x02
    {
        if (ensure_mounted() != 0) {
            fatfs_make_resp(action, 0x01, NULL, 0, resp_data, resp_data_length);
            return PARSE_STATUS_SUCCESS;
        }
        fatfs_scan_files();
        uint8_t extra[1] = { file_count };
        fatfs_make_resp(action, 0x00, extra, 1, resp_data, resp_data_length);
        return PARSE_STATUS_SUCCESS;
    }
    else if (action == FATFS_ACTION_GET_FILE_INFO) // 0x03
    {
        if (data_length < 3)
            return PARSE_STATUS_PACKET_DATA_LEN_INVALID;

        uint8_t file_index = data[2];
        if (file_index >= file_count) {
            fatfs_make_resp(action, 0x01, NULL, 0, resp_data, resp_data_length);
            return PARSE_STATUS_SUCCESS;
        }

        // extra: [FILESIZE(4B)][TOTAL_CHUNKS(4B)][FILENAME...\0]
        uint32_t fsize = file_cache[file_index].size;
        uint32_t total_chunks = (fsize + FATFS_READ_CHUNK_SIZE - 1) / FATFS_READ_CHUNK_SIZE;
        uint8_t name_len = (uint8_t)strlen(file_cache[file_index].name);
        uint8_t extra[4 + 4 + FATFS_MAX_FILENAME + 1]; // max possible
        write_le32(extra, fsize);
        write_le32(extra + 4, total_chunks);
        memcpy(extra + 8, file_cache[file_index].name, name_len + 1); // include \0
        uint8_t extra_total = 8 + name_len + 1;

        fatfs_make_resp(action, 0x00, extra, extra_total, resp_data, resp_data_length);
        return PARSE_STATUS_SUCCESS;
    }
    else if (action == FATFS_ACTION_READ_FILE) // 0x04
    {
        // data: [ACTION][FILE_INDEX(1B)][CHUNK_INDEX(4B LE32)]
        if (data_length < 7)
            return PARSE_STATUS_PACKET_DATA_LEN_INVALID;

        uint8_t file_index = data[2];
        uint32_t chunk_index = (uint32_t)data[3] | ((uint32_t)data[4] << 8) |
                               ((uint32_t)data[5] << 16) | ((uint32_t)data[6] << 24);

        if (file_index >= file_count) {
            fatfs_make_resp(action, 0x01, NULL, 0, resp_data, resp_data_length);
            return PARSE_STATUS_SUCCESS;
        }

        // build path
        char path[64];
        snprintf(path, sizeof(path), "1:/%s", file_cache[file_index].name);

        FIL fp;
        FRESULT fr = f_open(&fp, path, FA_READ);
        if (fr != FR_OK) {
            fatfs_make_resp(action, 0x01, NULL, 0, resp_data, resp_data_length);
            return PARSE_STATUS_SUCCESS;
        }

        uint32_t offset = chunk_index * FATFS_READ_CHUNK_SIZE;
        if (offset >= f_size(&fp)) {
            f_close(&fp);
            fatfs_make_resp(action, 0x01, NULL, 0, resp_data, resp_data_length);
            return PARSE_STATUS_SUCCESS;
        }

        f_lseek(&fp, offset);
        uint8_t read_buf[FATFS_READ_CHUNK_SIZE];
        UINT bytes_read = 0;
        fr = f_read(&fp, read_buf, FATFS_READ_CHUNK_SIZE, &bytes_read);
        f_close(&fp);

        if (fr != FR_OK || bytes_read == 0) {
            fatfs_make_resp(action, 0x01, NULL, 0, resp_data, resp_data_length);
            return PARSE_STATUS_SUCCESS;
        }

        // extra: [CHUNK_INDEX(4B LE32)][BYTES_READ(1B)][DATA...]
        uint8_t extra[4 + 1 + FATFS_READ_CHUNK_SIZE];
        write_le32(extra, chunk_index);
        extra[4] = (uint8_t)bytes_read;
        memcpy(extra + 5, read_buf, bytes_read);

        fatfs_make_resp(action, 0x00, extra, 5 + bytes_read, resp_data, resp_data_length);
        return PARSE_STATUS_SUCCESS;
    }
    else if (action == FATFS_ACTION_CREATE_FILE) // 0x05
    {
        // data: [ACTION][FILENAME_LEN(1B)][FILENAME...][FILE_SIZE(4B LE32)]
        if (data_length < 3)
            return PARSE_STATUS_PACKET_DATA_LEN_INVALID;

        uint8_t name_len = data[2];
        if (name_len == 0 || name_len > FATFS_MAX_FILENAME || (3 + name_len + 4) > data_length)
            return PARSE_STATUS_PACKET_DATA_LEN_INVALID;

        char fname[FATFS_MAX_FILENAME + 1];
        memcpy(fname, data + 3, name_len);
        fname[name_len] = '\0';

        // 验证文件名以 display_ 开头
        if (strncmp(fname, "display_", 8) != 0) {
            fatfs_make_resp(action, 0x01, NULL, 0, resp_data, resp_data_length);
            return PARSE_STATUS_SUCCESS;
        }

        uint32_t file_size = (uint32_t)data[3 + name_len] | ((uint32_t)data[4 + name_len] << 8) |
                             ((uint32_t)data[5 + name_len] << 16) | ((uint32_t)data[6 + name_len] << 24);

        if (ensure_mounted() != 0) {
            fatfs_make_resp(action, 0x01, NULL, 0, resp_data, resp_data_length);
            return PARSE_STATUS_SUCCESS;
        }

        char path[64];
        snprintf(path, sizeof(path), "1:/%s", fname);

        FIL fp;
        FRESULT fr = f_open(&fp, path, FA_CREATE_ALWAYS | FA_WRITE);
        if (fr != FR_OK) {
            fatfs_make_resp(action, 0x01, NULL, 0, resp_data, resp_data_length);
            return PARSE_STATUS_SUCCESS;
        }

        if (file_size > 0) {
            // 预分配文件大小
            f_lseek(&fp, file_size - 1);
            uint8_t zero = 0;
            UINT bw;
            f_write(&fp, &zero, 1, &bw);
        }
        f_close(&fp);

        fatfs_scan_files();
        fatfs_make_resp(action, 0x00, NULL, 0, resp_data, resp_data_length);
        return PARSE_STATUS_SUCCESS;
    }
    else if (action == FATFS_ACTION_WRITE_FILE) // 0x06
    {
        // data: [ACTION][FILE_INDEX(1B)][OFFSET(4B LE32)][DATA_LEN(1B)][DATA...]
        if (data_length < 8)
            return PARSE_STATUS_PACKET_DATA_LEN_INVALID;

        uint8_t file_index = data[2];
        uint32_t offset = (uint32_t)data[3] | ((uint32_t)data[4] << 8) |
                          ((uint32_t)data[5] << 16) | ((uint32_t)data[6] << 24);
        uint8_t write_len = data[7];

        if (file_index >= file_count) {
            fatfs_make_resp(action, 0x01, NULL, 0, resp_data, resp_data_length);
            return PARSE_STATUS_SUCCESS;
        }
        if ((8 + write_len) > data_length)
            return PARSE_STATUS_PACKET_DATA_LEN_INVALID;

        char path[64];
        snprintf(path, sizeof(path), "1:/%s", file_cache[file_index].name);

        FIL fp;
        FRESULT fr = f_open(&fp, path, FA_WRITE);
        if (fr != FR_OK) {
            fatfs_make_resp(action, 0x01, NULL, 0, resp_data, resp_data_length);
            return PARSE_STATUS_SUCCESS;
        }

        f_lseek(&fp, offset);
        UINT bw = 0;
        fr = f_write(&fp, data + 8, write_len, &bw);
        f_close(&fp);

        if (fr != FR_OK) {
            fatfs_make_resp(action, 0x01, NULL, 0, resp_data, resp_data_length);
            return PARSE_STATUS_SUCCESS;
        }

        fatfs_scan_files();
        fatfs_make_resp(action, 0x00, NULL, 0, resp_data, resp_data_length);
        return PARSE_STATUS_SUCCESS;
    }
    else if (action == FATFS_ACTION_GET_FS_INFO) // 0x07
    {
        if (ensure_mounted() != 0) {
            fatfs_make_resp(action, 0x01, NULL, 0, resp_data, resp_data_length);
            return PARSE_STATUS_SUCCESS;
        }

        DWORD free_clst = 0;
        FATFS *fatfs_ptr = NULL;
        FRESULT fr = f_getfree("1:", &free_clst, &fatfs_ptr);
        if (fr != FR_OK) {
            fatfs_make_resp(action, 0x01, NULL, 0, resp_data, resp_data_length);
            return PARSE_STATUS_SUCCESS;
        }

        // total = (n_fatent - 2) * csize * sector_size
        // free = free_clst * csize * sector_size
        DWORD sector_size = (DWORD)fatfs_ptr->ssize;
        uint32_t total_bytes = (uint32_t)((fatfs_ptr->n_fatent - 2) * fatfs_ptr->csize * sector_size);
        uint32_t free_bytes = (uint32_t)(free_clst * fatfs_ptr->csize * sector_size);

        // extra: [TOTAL(4B LE32)][FREE(4B LE32)]
        uint8_t extra[8];
        write_le32(extra, total_bytes);
        write_le32(extra + 4, free_bytes);

        fatfs_make_resp(action, 0x00, extra, 8, resp_data, resp_data_length);
        return PARSE_STATUS_SUCCESS;
    }
    else if (action == FATFS_ACTION_FORMAT) // 0x08
    {
        // 先卸载已挂载的文件系统
        f_mount(NULL, "1:", 0);
        fs_mounted = 0;

        // f_mkfs需要一个工作缓冲区，至少4096字节(SPI FLASH扇区大小)
        uint8_t *mkfs_buf = malloc(4096);
        if (mkfs_buf == NULL) {
            fatfs_make_resp(action, 0x01, NULL, 0, resp_data, resp_data_length);
            return PARSE_STATUS_SUCCESS;
        }

        MKFS_PARM mkfs_opt;
        memset(&mkfs_opt, 0, sizeof(mkfs_opt));
        mkfs_opt.fmt = FM_FAT | FM_SFD;
        mkfs_opt.n_fat = 1;
        mkfs_opt.align = 1;   // SPI FLASH扇区对齐(单位:扇区, 1=4096字节)

        FRESULT fr = f_mkfs("1:", &mkfs_opt, mkfs_buf, 4096);
        free(mkfs_buf);

        if (fr != FR_OK) {
            uint8_t err_extra[1] = { (uint8_t)fr };
            fatfs_make_resp(action, 0x01, err_extra, 1, resp_data, resp_data_length);
            return PARSE_STATUS_SUCCESS;
        }

        // 格式化成功，清空文件缓存
        file_count = 0;

        fatfs_make_resp(action, 0x00, NULL, 0, resp_data, resp_data_length);
        return PARSE_STATUS_SUCCESS;
    }

    return PARSE_STATUS_INVALID_COMMAND;
}
#endif /* ENABLE_FATFS */

int handle_mcu_opt(uint8_t *data, uint8_t data_length, int port_number, uint8_t **resp_data, uint8_t *resp_data_length)
{
    //printf("handle_mcu_opt:data_length:%d\n", data_length);
    if (data_length < 1)
        return PARSE_STATUS_PACKET_DATA_LEN_INVALID;
    uint8_t mcu_opt_command = *(data);
    if (mcu_opt_command == MCU_OPT_COMMAND_GPIO_LEVEL)
    {
        return handle_mcu_opt_gpio_level(data, data_length, port_number, resp_data, resp_data_length);
    }
    if (mcu_opt_command == MCU_OPT_GPIO_SET_DIRECTION)
    {
        return handle_mcu_opt_gpio_set_direction(data, data_length, port_number, resp_data, resp_data_length);
    }
    if (mcu_opt_command == MCU_OPT_DATAFLASH_COMMAND)
    {
        return handle_mcu_opt_DATAFLASH_COMMAND(data, data_length, port_number, resp_data, resp_data_length);
    }
    if (mcu_opt_command == MCU_OPT_SPI_W25Q64_COMMAND)
    {
        return handle_mcu_opt_SPI_W25Q64_COMMAND(data, data_length, port_number, resp_data, resp_data_length);
    }
    if (mcu_opt_command == MCU_OPT_VAR_SET_COMMAND)
    {
        return handle_mcu_opt_VAR_SET_COMMAND(data, data_length, port_number, resp_data, resp_data_length);
    }
#ifdef ENABLE_FATFS
    if (mcu_opt_command == MCU_OPT_FATFS_COMMAND)
    {
        return handle_mcu_opt_FATFS_COMMAND(data, data_length, port_number, resp_data, resp_data_length);
    }
#endif
    return PARSE_STATUS_INVALID_COMMAND;
}
int handle_send_data(uint8_t *data, uint8_t data_length, int port_number, uint8_t **resp_data, uint8_t *resp_data_length)
{
    uint8_t recv_port_number = *(data);
    uint8_t spi_command = *(data + 1);
    uint8_t data_len =0;
    if(spi_command==0x0){
        uint8_t id[4]={0xFF,0xFF,0xFF,0XFF};
        BSP_W25Qx_Read_ID(id);
        data_len=make_recv_data_resp(recv_port_number, id,4, resp_data);
        *resp_data_length = data_len;
        return PARSE_STATUS_SUCCESS;
    }
    data_len = make_simple_code_resp(COMMAND_SEND_DATA, 0x2, resp_data);
    *resp_data_length = data_len;
    //print_hex("handle_send_data:port", data, 1);
    //print_hex("handle_send_data:data", data + 1, data_length - 1);
    //uint8_t data_len = make_simple_code_resp(COMMAND_SEND_DATA, 0x1, resp_data);
    //*resp_data_length = data_len;
    //printf("already set recv_data\n");
    /*
    uint8_t recv_data_len = make_recv_data_resp(recv_port_number, data + 1, data_length - 1, &recv_data_buffer);
    if(recv_port_number==0){
        memset(pEP1_IN_DataBuf,0,64);
        memcpy(pEP1_IN_DataBuf,recv_data_buffer,recv_data_len);
        DevEP1_IN_Deal(64);
        free(recv_data_buffer);
        recv_data_buffer=NULL;
    }else if(recv_port_number==1){
        memset(pU2EP1_IN_DataBuf,0,64);
        memcpy(pU2EP1_IN_DataBuf,recv_data_buffer,recv_data_len);
        U2DevEP1_IN_Deal(64);
        free(recv_data_buffer);
        recv_data_buffer=NULL;
    }
    if(recv_data_buffer != NULL) {
      free(recv_data_buffer);
      recv_data_buffer = NULL;
    }
        */
    return PARSE_STATUS_SUCCESS;
}

int parse_data(uint8_t *data, uint8_t data_length, int port_number, uint8_t **resp_data, uint8_t *resp_data_length)
{
    int ret = PARSE_STATUS_INVALID_COMMAND;
    if (data_length < 5)
    {
        ret = PARSE_STATUS_PACKET_FORMAT_ERROR;
        goto error;
    }
    if (*(data) != MAGIC_CODE_1 || *(data + 1) != MAGIC_CODE_2)
    {
        ret = PARSE_STATUS_PACKET_FORMAT_ERROR;
        goto error;
    }
    uint8_t command_type = *(data + MAGIC_CODE_LENGTH);
    uint8_t command_data_len = *(data + MAGIC_CODE_LENGTH + 1);
    if ((command_data_len + MAGIC_CODE_LENGTH + 1 + 1 + 1) > data_length)
    {
        ret = PARSE_STATUS_PACKET_DATA_LEN_INVALID;
        goto error;
    }
    if (verify_checksum(data, (command_data_len + MAGIC_CODE_LENGTH + 1 + 1 + 1)) == FAILED)
    {
        ret = PARSE_STATUS_CHECKSUM_ERROR;
        goto error;
    }
    uint8_t *command_data = NULL;
    if (command_data_len > 0)
    {
        command_data = malloc(sizeof(uint8_t) * command_data_len);
        memcpy(command_data, data + MAGIC_CODE_LENGTH + 1 + 1, command_data_len);
    }
    if (command_type == COMMAND_MCU_VER)
    {
        ret = handle_mcu_ver(command_data, command_data_len, port_number, resp_data, resp_data_length);
    }
    if (command_type == COMMAND_MCU_OPT)
    {
        ret = handle_mcu_opt(command_data, command_data_len, port_number, resp_data, resp_data_length);
    }
    if (command_type == COMMAND_PORT_INFO)
    {
        ret = handle_port_info(command_data, command_data_len, port_number, resp_data, resp_data_length);
    }
    if (command_type == COMMAND_SEND_DATA)
    {
        ret = handle_send_data(command_data, command_data_len, port_number, resp_data, resp_data_length);
    }
    if (ret == PARSE_STATUS_SUCCESS && resp_data != NULL){
        if(command_data!=NULL)
            free(command_data);
        command_data=NULL;
        return ret;
    }
error:
    *resp_data_length = make_error_resp(ret, resp_data);
    if(command_data!=NULL)
        free(command_data);
    command_data=NULL;
    return ret;
}
