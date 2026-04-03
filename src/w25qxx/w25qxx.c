#include "w25qxx.h"
#include "../printf.h"
uint8_t CH58X_SPI_INIT_W25Qx(){
    #ifdef CH58X_SPI_REMAP
    GPIOPinRemap(ENABLE,RB_PIN_SPI0);
    GPIOB_ResetBits(SCS_PIN | SCK_PIN | MOSI_PIN);
    GPIOB_ModeCfg(SCS_PIN | SCK_PIN | MOSI_PIN, GPIO_ModeOut_PP_5mA);
    #else
    //GPIOA_SetBits(MISO_PIN);
    //GPIOA_ModeCfg(MISO_PIN,GPIO_ModeIN_PU);
    GPIOA_SetBits(SCS_PIN);
    GPIOA_ModeCfg(SCS_PIN | SCK_PIN | MOSI_PIN, GPIO_ModeOut_PP_5mA);
    #endif
    SPI0_MasterDefInit();
    SPI0_DataMode(Mode3_HighBitINFront);
    //SPI0_CLKCfg(64);
    return BSP_W25Qx_Init();
    //return 0;
}

uint8_t BSP_W25Qx_Init(void)
{
    BSP_W25Qx_Reset();
    return BSP_W25Qx_GetStatus();
}

void BSP_W25Qx_Reset(void)
{
    uint8_t cmd[2] = {RESET_ENABLE_CMD, RESET_MEMORY_CMD};

    W25Qx_Enable();
    /* Send the reset command */
    SPI0_MasterTrans(cmd, 2);
    W25Qx_Disable();
}

uint8_t BSP_W25Qx_GetStatus(void)
{
    uint8_t cmd[] = {READ_STATUS_REG1_CMD};
    uint8_t status;

    W25Qx_Enable();
    /* Send the read status command */
    SPI0_MasterTrans(cmd, 1);
    /* Reception of the data */
    SPI0_MasterRecv(&status, 1);
    W25Qx_Disable();

    /* Check the value of the register */
    if ((status & W25Q128FV_FSR_BUSY) != 0)
    {
        GPIOB_ResetBits(GPIO_PIN_1);
        return W25Qx_BUSY;
    }
    else
    {
        GPIOB_SetBits(GPIO_PIN_1);
        return W25Qx_OK;
    }
}

uint8_t BSP_W25Qx_WriteEnable(void)
{
    uint8_t cmd[] = {WRITE_ENABLE_CMD};
    uint32_t tickstart = SYS_GetSysTickCnt();

    /*Select the FLASH: Chip Select low */
    W25Qx_Enable();
    /* Send the read ID command */
    SPI0_MasterTrans(cmd, 1);
    /*Deselect the FLASH: Chip Select high */
    W25Qx_Disable();

    /* Wait the end of Flash writing */
    while (BSP_W25Qx_GetStatus() == W25Qx_BUSY)
        ;
    {
        /* Check for the Timeout */
        if ((SYS_GetSysTickCnt() - tickstart) > W25Qx_TIMEOUT_VALUE)
        {
            return W25Qx_TIMEOUT;
        }
    }

    return W25Qx_OK;
}

void BSP_W25Qx_Read_ID(uint8_t *ID)
{
    
    uint8_t cmd[4] = {READ_ID_CMD, 0x00, 0x00, 0x00};

    W25Qx_Enable();
    /* Send the read ID command */
    SPI0_MasterTrans(cmd, 4);
    /* Reception of the data */
    SPI0_MasterRecv(ID, 2);
    W25Qx_Disable();
    /*
    uint8_t ID1,ID2;
    GPIOA_ResetBits(GPIO_Pin_12);
    SPI0_MasterSendByte(0x90);
    SPI0_MasterSendByte(0x00);
    SPI0_MasterSendByte(0x00);
    SPI0_MasterSendByte(0x00);
    for(int i=0;i<5;i++){
        printf("SPI0_MasterRecvByte()=%x\n",SPI0_MasterRecvByte());
    }
    ID1=SPI0_MasterRecvByte();
    ID2=SPI0_MasterRecvByte();
    printf("%x,%x\n",ID1,ID2);
    GPIOA_SetBits(GPIO_Pin_12);
    *(ID)=ID1;
    *(ID+1)=ID2;
    */
}

uint8_t BSP_W25Qx_Read(uint8_t *pData, uint32_t ReadAddr, uint32_t Size)
{
    uint8_t cmd[4];

    /* Configure the command */
    cmd[0] = READ_CMD;
    cmd[1] = (uint8_t)(ReadAddr >> 16);
    cmd[2] = (uint8_t)(ReadAddr >> 8);
    cmd[3] = (uint8_t)(ReadAddr);

    W25Qx_Enable();
    /* Send the read ID command */
    SPI0_MasterTrans(cmd, 4);
    DelayMs(1);
    /* Reception of the data */
    SPI0_MasterRecv(pData, Size);
    W25Qx_Disable();
    return W25Qx_OK;
}

uint8_t BSP_W25Qx_Write(uint8_t *pData, uint32_t WriteAddr, uint32_t Size)
{
    uint8_t cmd[4];
    uint32_t end_addr, current_size, current_addr;
    uint32_t tickstart = SYS_GetSysTickCnt();
    /* Calculation of the size between the write address and the end of the page */
    current_addr = 0;

    while (current_addr <= WriteAddr)
    {
        current_addr += W25Q128FV_PAGE_SIZE;
    }
    current_size = current_addr - WriteAddr;

    /* Check if the size of the data is less than the remaining place in the page */
    if (current_size > Size)
    {
        current_size = Size;
    }

    /* Initialize the adress variables */
    current_addr = WriteAddr;
    end_addr = WriteAddr + Size;

    /* Perform the write page by page */
    do
    {
        /* Configure the command */
        cmd[0] = PAGE_PROG_CMD;
        cmd[1] = (uint8_t)(current_addr >> 16);
        cmd[2] = (uint8_t)(current_addr >> 8);
        cmd[3] = (uint8_t)(current_addr);

        /* Enable write operations */
        BSP_W25Qx_WriteEnable();

        W25Qx_Enable();
        /* Send the command */
        SPI0_MasterTrans(cmd, 4);
        DelayMs(1);
        /* Transmission of the data */
        SPI0_MasterTrans(pData, current_size);
        W25Qx_Disable();
        /* Wait the end of Flash writing */
        while (BSP_W25Qx_GetStatus() == W25Qx_BUSY)
            ;
        {
            /* Check for the Timeout */
            if ((SYS_GetSysTickCnt() - tickstart) > W25Qx_TIMEOUT_VALUE)
            {
                return W25Qx_TIMEOUT;
            }
        }

        /* Update the address and size variables for next page programming */
        current_addr += current_size;
        pData += current_size;
        current_size = ((current_addr + W25Q128FV_PAGE_SIZE) > end_addr) ? (end_addr - current_addr) : W25Q128FV_PAGE_SIZE;
    } while (current_addr < end_addr);

    return W25Qx_OK;
}

uint8_t BSP_W25Qx_Erase_Block(uint32_t Address)
{
    uint8_t cmd[4];
    uint32_t tickstart = SYS_GetSysTickCnt();
    cmd[0] = SECTOR_ERASE_CMD;
    cmd[1] = (uint8_t)(Address >> 16);
    cmd[2] = (uint8_t)(Address >> 8);
    cmd[3] = (uint8_t)(Address);

    /* Enable write operations */
    BSP_W25Qx_WriteEnable();

    /*Select the FLASH: Chip Select low */
    W25Qx_Enable();
    /* Send the read ID command */
    SPI0_MasterTrans(cmd, 4);
    /*Deselect the FLASH: Chip Select high */
    W25Qx_Disable();

    /* Wait the end of Flash writing */
    while (BSP_W25Qx_GetStatus() == W25Qx_BUSY)
        ;
    {
        /* Check for the Timeout */
        if ((SYS_GetSysTickCnt() - tickstart) > W25Q128FV_SECTOR_ERASE_MAX_TIME)
        {
            return W25Qx_TIMEOUT;
        }
    }
    return W25Qx_OK;
}

/**********************************************************************************
 * ????: ????
 */
uint8_t BSP_W25Qx_Erase_Chip(void)
{
    uint8_t cmd[4];
    uint32_t tickstart = SYS_GetSysTickCnt();
    cmd[0] = CHIP_ERASE_CMD;

    /* Enable write operations */
    BSP_W25Qx_WriteEnable();

    /*Select the FLASH: Chip Select low */
    W25Qx_Enable();
    /* Send the read ID command */
    SPI0_MasterTrans(cmd, 1);
    /*Deselect the FLASH: Chip Select high */
    W25Qx_Disable();

    /* Wait the end of Flash writing */
    while (BSP_W25Qx_GetStatus() != W25Qx_BUSY)
        ;
    {
        /* Check for the Timeout */
        if ((SYS_GetSysTickCnt() - tickstart) > W25Q128FV_BULK_ERASE_MAX_TIME)
        {
            return W25Qx_TIMEOUT;
        }
    }
    return W25Qx_OK;
}
