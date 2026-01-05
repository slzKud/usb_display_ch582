#include <stdio.h>
#include "printf.h"
#include "fatfs/ff.h"
#include "fatfs/diskio.h"

FATFS fsobject;
BYTE work[FF_MAX_SS]; 
FIL fp ;
const char write_buf[] = "Hello FatFS!!";
char read_buf[255] = "";
UINT bw;
UINT br;

int fatfs_test(){
    int  res ;
    printf("start_fatfs_test\n");
    res = f_mount(&fsobject,  "1:",  1);
    printf("mount over.res=%d\n",res);
    //res = f_mount(NULL,  "1:",  1);

    if(res == FR_NO_FILESYSTEM)
	{
        printf("mkfs...\n");
        res = f_mkfs("1:",0,work,sizeof(work));
        if(res==FR_OK){
            printf("mkfs...ok\n");
            res = f_mount(NULL,  "1:",  1);
            res = f_mount(&fsobject,  "1:",  1);
        }else{
            return 1;
        }
    }else if(res ==RES_OK){
        printf("mount fatfs ok\n");
    }else{
        return 2;
    }
    res=f_open(&fp , "1:hello" , FA_OPEN_ALWAYS|FA_READ |FA_WRITE);
    if(res==RES_OK){
        printf("open file ok\n");
        res=f_write(&fp,write_buf,sizeof(write_buf),&bw);
        if(res==RES_OK){
            printf("write file ok\n");
            f_lseek(&fp,0);
            res = f_read(&fp ,read_buf,f_size(&fp),&br);
            if(res!=RES_OK){
                printf("read file error\n");
                f_close(&fp);
                f_mount(NULL,  "1:",  1);
                return 5;
            }
            printf("read file ok,file:%s\n",read_buf);
            f_close(&fp);
            f_mount(NULL,  "1:",  1);
        }
    }else{
        return 3;
    }
    return 0;

}

void fatfs_test_str(char *str){
    sprintf(str,"r:%s",read_buf);
}