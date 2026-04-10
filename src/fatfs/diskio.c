/*-----------------------------------------------------------------------*/
/* Low level disk I/O module SKELETON for FatFs     (C)ChaN, 2025        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include "ff.h"			/* Basic definitions of FatFs */
#include "diskio.h"		/* Declarations FatFs MAI */

/* Example: Declarations of the platform and disk functions in the project */
//#include "platform.h"
//#include "storage.h"

/* Example: Mapping of physical drive number for each drive */
//#define DEV_FLASH	0	/* Map FTL to physical drive 0 */
//#define DEV_MMC		1	/* Map MMC/SD card to physical drive 1 */
//#define DEV_USB		2	/* Map USB MSD to physical drive 2 */
#define SD_CARD 0
#define SPI_FLASH 1
#define Q64_BLOCK_SIZE 1024
#include "../w25qxx/w25qxx.h"
#include "../printf.h"
/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
	BYTE pdrv		/* Physical drive nmuber to identify the drive */
)
{
	printf("disk_status\n");
	DSTATUS stat;
	//int result;
	uint8_t id[3]={0x0,0x0,0x0};
	switch (pdrv) {
	case SD_CARD :

		break;

	case SPI_FLASH :

		BSP_W25Qx_Read_ID(id);

		if(id[0]==0xef && id[2]>=0x16)
			stat=0;
		else
			stat= STA_NOINIT;

		return stat;
	}
	return STA_NOINIT;
}



/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
	BYTE pdrv				/* Physical drive nmuber to identify the drive */
)
{
	//DSTATUS stat;
	//int result;
	printf("disk_initialize\n");
	switch (pdrv) {
	case SD_CARD :

		break;

	case SPI_FLASH :
		CH58X_SPI_INIT_W25Qx();

		return disk_status(SPI_FLASH);
	}
	return STA_NOINIT;
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	LBA_t sector,	/* Start sector in LBA */
	UINT count		/* Number of sectors to read */
)
{
	DRESULT res;
	//int result;
	int wc=count*4096/Q64_BLOCK_SIZE;
	int i=0;
	int ret =0;
	switch (pdrv) {
	case SD_CARD :
		break;

	case SPI_FLASH :
		printf("disk_read,sector:%x,count:%x\n",sector,count);
		while(i<wc){
			
			BSP_W25Qx_Read(buff+(i*Q64_BLOCK_SIZE),sector*4096+i*Q64_BLOCK_SIZE,Q64_BLOCK_SIZE);
			i++;
		}
		//BSP_W25Qx_Read(buff,sector*4096,count*4096);
		res = RES_OK;
		return res;
	}

	return RES_PARERR;
}



/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0

DRESULT disk_write (
	BYTE pdrv,			/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	LBA_t sector,		/* Start sector in LBA */
	UINT count			/* Number of sectors to write */
)
{
	DRESULT res;
	//int result;
	int wc=count*4096/Q64_BLOCK_SIZE;
	int i=0;
	switch (pdrv) {
	case SD_CARD :

		break;

	case SPI_FLASH :
		if(BSP_W25Qx_Erase_Block(sector*4096)==W25Qx_OK){
			while(i<wc){
				if(BSP_W25Qx_Write((uint8_t *)buff+(i*Q64_BLOCK_SIZE),sector*4096+i*Q64_BLOCK_SIZE,Q64_BLOCK_SIZE)==W25Qx_OK){
					res=RES_OK;
				}else{
					printf("failed\n");
					res=RES_PARERR;
				}
				i++;
			}
		}else{
			res=RES_PARERR;
		}
		return res;
	}

	return RES_PARERR;
}

#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
	DRESULT res;
	//int result;
	printf("disk_ioctl\n");
	switch (pdrv) {
	case SD_CARD :

		// Process of the command for the RAM drive

		break;

	case SPI_FLASH :

		// Process of the command for the MMC/SD card
		switch(cmd){
			case GET_SECTOR_COUNT:
				*(DWORD *)buff = 2048;
				break;
			case GET_SECTOR_SIZE:
				*(WORD *)buff = 4096;
				break;
			case GET_BLOCK_SIZE:
				*(WORD *)buff = 1;
				break;
		}
		res=RES_OK;
		return res;
	}

	return RES_PARERR;
}

