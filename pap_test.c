#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "dis_dfe8219_api.h"
#include "paProtection.h"
#include <gpiod.h>  

#define TX0 0 

// 定义一个函数指针类型
typedef void (*CallbackFunction)(void);

//检测GPIO电平状态函数 1：高电平 0：低电平
int check_io_state(unsigned int chip_number=0, unsigned int line_number=0){
    struct gpiod_chip *chip = NULL;
    struct gpiod_line *line = NULL;
    chip = gpiod_chip_open_by_number(chip_number);
    line = gpiod_chip_get_line(chip, line_number);
    return gpiod_line_get_value(line);
}

//中断处理函数 如果TX0对应的中断触发了，执行关PA步骤
void GPIO_IRQHandler(void){
    //①配置TDD
    tdd_module_e tdd_module = TDD_MODULE_PA;
    dis_dfe8219_tddEnable(tdd_module, TX0, 0);
    //②配置DPD和CLGC
    configCallback();
    //③dig_gain0配置0
    float txDucGainValue_dB = 0;
    dis_dfe8219_filterBranchId_e fb = fb0;
    dis_dfe8219_getTxDucGain(fb, &txDucGainValue_dB);
    dis_dfe8219_setTxDucGain(fb, 0);
}

int main(int argc, char *argv[]) {
    // 定义函数指针，并将其指向不同的函数
    CallbackFunction initCallback = ;
    CallbackFunction configCallback = ;
    int io_state = 0;
    //初始化中断
    initCallback();

    while(1){        
        io_state = check_io_state(4, 7);
        if(io_state == 0){
            //执行开PA步骤
            io_state = check_io_state(4, 7);
            if(io_state == 0){
                // ①配置PAP为可恢复模式
                setPapRecover(1);
                // ②配置TDD
                dis_dfe8219_tddEnable(tdd_module, TX0, 1);
                // ③dig_gain0配置
                dis_dfe8219_setTxDucGain(fb, txDucGainValue_dB);
                // ④配置DPD和CLGC
                configCallback();
                // ⑤配置PAP为不可恢复模式
                setPapRecover(0);
            }
            else if(io_state == -1){
                perror("Read current gpio value error!");
                return EXIT_FAILURE;
        }
        }
        else if(io_state == -1){
            perror("Read current gpio value error!");
            return EXIT_FAILURE;
        }
    }
    


    return 0;
}