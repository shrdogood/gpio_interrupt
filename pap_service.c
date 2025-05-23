#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <stdint-uintn.h>
#include "dis_dfe8219_api.h"
#include "paProtection.h"
#include "carrierResourceHandler.h"
#include "pap_service.h"

#define MAP_COUNT (sizeof(gpioMapTable)/sizeof(gpioMapTable[0]))
#define SYSFS_GPIO_DIR "/sys/class/gpio"

extern void* tdd_module;
dis_dfe8219_fbList_t fb_list;
uint32_t tddCh = 0;


// 简单封装：导出 GPIO，设置方向为 in，设为 both 边沿触发
static int gpio_export_and_config(int bank, int pin) {
    char path[64], buf[8];
    int fd, len;

    // 导出
    fd = open(SYSFS_GPIO_DIR "/export", O_WRONLY);
    if (fd < 0 && errno != EBUSY) return -1;
    if (fd >= 0) {
        len = snprintf(buf, sizeof(buf), "%d\n", bank*32 + pin);
        write(fd, buf, len);
        close(fd);
    }

    // 设置方向
    snprintf(path, sizeof(path), SYSFS_GPIO_DIR "/gpio%d/direction", bank*32 + pin);
    fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    write(fd, "in\n", 3);
    close(fd);

    // 设置边沿触发
    snprintf(path, sizeof(path), SYSFS_GPIO_DIR "/gpio%d/edge", bank*32 + pin);
    fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    write(fd, "both\n", 5);
    close(fd);

    return 0;
}

// 初始化中断，将所有关注的 GPIO 导出并配置
void init_function(void) {
    for (int i = 0; i < MAP_COUNT; i++) {
        if (gpio_export_and_config(gpio_banks[i], gpio_pins[i]) < 0) {
            fprintf(stderr, "Failed to config GPIO %d_%d\n", gpio_banks[i], gpio_pins[i]);
        }
    }
    // 这里可以创建一个单独线程或使用 epoll/poll 监控所有 gpioX/value 文件的可读事件
    // 假设下文 GPIO_IRQHandler 会在检测到中断时被调用
}

// 占位：将实际的 DPD、CLGC 初始化逻辑填在这里
void config_function(void) {
    // e.g. dis_dfe8219_configDPD(...);
    //     dis_dfe8219_configCLGC(...);
}

// 实际中断处理例程
void GPIO_IRQHandler(int bank, int pin) {
    // 1) 在映射表中查找
    const GpioTxMap *entry = NULL;
    for (int i = 0; i < MAP_COUNT; i++) {
        if (gpioMapTable[i].bank == bank && gpioMapTable[i].pin == pin) {
            entry = &gpioMapTable[i];
            break;
        }
    }
    if (!entry || entry->txCount == 0) return;  // 找不到或不控制任何 TX

    // 2) 读一次 value 判断是上升沿（'1'）还是下降沿（'0'）
    int gpio_num = bank*32 + pin;
    char path[64], val;
    int fd;
    snprintf(path, sizeof(path), SYSFS_GPIO_DIR "/gpio%d/value", gpio_num);
    fd = open(path, O_RDONLY);
    if (fd < 0) return;
    read(fd, &val, 1);
    close(fd);

    // val == '0' 表示低电平，'1' 表示高电平
    // 这里假设先触发中断，接着读取到的是触发时的电平
    if (val == '1') {
        // 上升沿：对所有 txNums 依次关闭 PA
        for (int k = 0; k < entry->txCount; k++) {
            PA_SW_Close(entry->txNums[k]);
        }
    } else {
        // 下降沿到低电平 -> 二次确认后开启 PA
        usleep(1000);
        fd = open(path, O_RDONLY);
        if (fd < 0) return;
        read(fd, &val, 1);
        close(fd);
        if (val == '0') {
            for (int k = 0; k < entry->txCount; k++) {
                PA_SW_Open(entry->txNums[k]);
            }
        }
    }

    // 清除中断标志（具体操作视底层驱动而定，可能是写 IRQ_CLEAR 寄存器）
    // clear_irq(bank, pin);
}


uint8_t processFbListSaveAndZero(dis_dfe8219_fbList_t* fbList, float* savedGains) {
    uint8_t count = 0;

    for (uint8_t i = 0; i < fbList->validFbCnt; ++i) {
        uint8_t fb = fbList->fbIdList[i];
        
        float gainValue_dB = 0;
        // 获取当前 gain
        if (dis_dfe8219_getTxDucGain(fb, &gainValue_dB) != 0) {
            perror("Error getting gain for fb %u\n", fb);
            return EXIT_FAILURE;
        }

        // 保存到数组
        savedGains[count++] = gainValue_dB;

        // 设置为 0
        if (dis_dfe8219_setTxDucGain(fb, 0) != 0) {
            perror("Error setting gain for fb %u\n", fb);
            return EXIT_FAILURE;
        }
    }

    return 0;
}

uint8_t processFbListRestore(dis_dfe8219_fbList_t* fbList, const float* savedGains) {
    uint8_t count = 0;

    for (uint8_t i = 0; i < fbList->validFbCnt; ++i) {
        uint8_t fb = fbList->fbIdList[i];
        float originalGain = savedGains[count++];

        // 恢复原增益
        if (dis_dfe8219_setTxDucGain(fb, originalGain) != 0) {
            perror("Error setting gain for fb %u\n", fb);
            return EXIT_FAILURE;
        }
    }

    return 0;
}

void PA_SW_Open(int TxNum){
    // ①配置PAP为可恢复模式
    setPapRecover(1);
    // ②配置TDD
    dis_dfe8219_swPaOn(s_paChMappingtdd[TxNum]);
    // ③dig_gain0配置
    processFbListRestore(&fb_list, savedGains[TxNum]); 
    // ④配置DPD和CLGC
    configCallback(); //
    // ⑤配置PAP为不可恢复模式
    setPapRecover(0);
}

void PA_SW_Close(int TxNum){
    //①配置TDD
    dis_dfe8219_swPaOff(s_paChMappingtdd[TxNum]);  //需要pach
    //②配置DPD和CLGC
    configCallback();
    //③dig_gain0配置0
    dis_dfe_getTddChIdByGlobalTx(TxNum, &tddCh);
    dis_dfe_getFbListByTddChId(tddCh, &fb_list);  //需要tddch
    processFbListSaveAndZero(&fb_list, savedGains[TxNum]);
}

int main(int argc, char *argv[]) {
    // 初始化
    init_function();

    // 这里可以使用一个简单的 poll 监控所有 gpioX/value
    struct pollfd fds[MAP_COUNT];
    char buf;
    char path[64];
    for (int i = 0; i < MAP_COUNT; i++) {
        int gpio_num = gpio_banks[i]*32 + gpio_pins[i];
        snprintf(path, sizeof(path), SYSFS_GPIO_DIR "/gpio%d/value", gpio_num);
        fds[i].fd = open(path, O_RDONLY | O_NONBLOCK);
        fds[i].events = POLLPRI | POLLERR;
        // 先读一次清掉初始状态
        read(fds[i].fd, &buf, 1);
    }

    while (1) {
        int ret = poll(fds, MAP_COUNT, -1);
        if (ret > 0) {
            for (int i = 0; i < MAP_COUNT; i++) {
                if (fds[i].revents & POLLPRI) {
                    // 清除标志，再调用处理
                    lseek(fds[i].fd, 0, SEEK_SET);
                    read(fds[i].fd, &buf, 1);
                    GPIO_IRQHandler(gpio_banks[i], gpio_pins[i]);
                }
            }
        }
    }

    return 0;
}
