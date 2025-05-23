#ifndef _PAP_SERVICE_
#define _PAP_SERVICE_

#include "dis_dfe8219_api.h"

// 一条映射记录：某个 gpio(bank,pin) → N 个 txNum
typedef struct {
    int bank, pin;
    int txCount;        // 下面 txNums 有效长度
    int txNums[8];      // 最多支持 8 个 tx 模块
} GpioTxMap;

static uint8_t s_paChMappingtdd[MAX_ANT_COUNT] = {0, 1, 2, 3, 4, 5, 6, 7};

// GPIO 列表，及其对应的 TxNum
static const int gpio_pins[]   = {  7,   8,   9,  10,  11,  13 };
static const int gpio_banks[]  = {  4,   4,   4,   4,   4,   4 };  // 比如 gpio4_7 表示 bank=4, pin=7

// 随机示例：gpio4_7 控制 tx 0,1； gpio4_8 控制 tx 2；gpio4_9 控制 tx 3,4,5；……
static const GpioTxMap gpioMapTable[] = {
    { 4,  7, 2, { 0, 1 } },
    { 4,  8, 1, { 2 }    },
    { 4,  9, 3, { 3,4,5} },
    { 4, 10, 1, { 6 }    },
    { 4, 11, 2, { 1,7 }  },
    { 4, 13, 0, { }      }  // 不控制任何 TX，也可以省略
};

static float savedGains[MAX_TX_MCB_CNT][MAX_CARRIER_PER_BRANCH];

/**
 * @brief 遍历 fbList 中的 fbId，获取并保存原始 gain 值，然后将其设置为 0
 * @param fbList   输入的 fb 列表
 * @param savedGains[out] 用于存储读取到的 gain 值
 * @return 实际处理的 fb 个数
 */
uint8_t processFbListSaveAndZero(dis_dfe8219_fbList_t* fbList, float* savedGains);
/**
 * @brief 将之前保存的 gain 值重新设置回 fb
 * @param fbList      输入的 fb 列表
 * @param savedGains  存储的 gain 值数组
 * @return 0 成功，非 0 失败
 */
uint8_t processFbListRestore(dis_dfe8219_fbList_t* fbList, const float* savedGains);
void PA_SW_Open(int TxNum);
void PA_SW_Close(int TxNum);





#endif