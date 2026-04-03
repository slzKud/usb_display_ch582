#include "u8g2_dataflash_cb.h"
#include "CH58x_common.h"
uint32_t dataflash_read_cb(void *user_ptr, uint32_t offset,
                        uint8_t *buffer, uint32_t count)
{
    /* user_ptr could be a file handle or SD card context */
    EEPROM_READ(offset,buffer,count);
    return count;
}