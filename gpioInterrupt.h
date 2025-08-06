#ifndef _GPIOINTERRUPT_H_
#define _GPIOINTERRUPT_H_

#include <gpiod.h>
#include <stdint.h>
#include <sys/epoll.h>
#include "gpio_pinmux.h"

#define MAX_INT_CNT 8

/**
 * @brief 单个 GPIO 中断通道的配置信息
 */
typedef struct {
    uint8_t  group_id;       // GPIO 组编号 (对应 gpiochipX)
    uint8_t  group_bit;      // GPIO 组内位编号
    uint8_t  uio_index;      // /dev/uio<uio_index> 设备索引
    char     consumer[16];   // gpiod consumer 标识符
} GpioIntPinCfg;

/**
 * @brief GPIO 中断上下文结构体
 */
typedef struct {
    uint8_t             int_cnt;                    // 实际中断通道数
    GpioIntPinCfg       pin_cfg[MAX_INT_CNT];      // 静态配置信息
    int                 fd[MAX_INT_CNT];           // UIO 文件描述符
    struct gpiod_line   *line[MAX_INT_CNT];        // gpiod line 句柄
} GpioIntCtx;

/**
 * @brief 旧版兼容结构体 (PAP服务仍在使用)
 */
typedef struct {
    struct gpiod_line *line[MAX_INT_CNT];
    uint8_t IntCount;
} user_gpio_t;

/**
 * @brief 通用 GPIO 中断初始化接口
 * @param ctx 预先配置好 pin_cfg 和 int_cnt 的上下文指针
 * @return uint8_t DIS_COMMON_ERR_OK 成功，其它错误码表示失败
 * 
 * 使用示例：
 *     GpioIntCtx ctx = { 
 *         .int_cnt = 2,
 *         .pin_cfg = {
 *             {4, 7, 2, "pwr_drop"},      // group4.bit7 -> uio2
 *             {4, 8, 3, "temp_alert"}     // group4.bit8 -> uio3
 *         }
 *     };
 *     ret = gpio_int_init(&ctx);
 */
uint8_t gpio_int_init(GpioIntCtx *ctx);

/**
 * @brief 根据通道索引初始化单个 GPIO 中断通道
 * @param ctx 已配置的上下文指针
 * @param channel_idx 要初始化的通道索引
 * @return uint8_t DIS_COMMON_ERR_OK 成功，其它错误码表示失败
 */
uint8_t gpio_int_init_channel(GpioIntCtx *ctx, uint8_t channel_idx);

/**
 * @brief 从数据库配置初始化 GPIO 中断上下文
 * @param ctx        输出的上下文指针
 * @param db_region  数据库区域标识 (如 GPIOINTERRUPT)
 * @param base_path  配置路径前缀 (如 "/GPIOINT" 或 "/papSvc")
 * @return uint8_t   DIS_COMMON_ERR_OK 成功，其它错误码表示失败
 */
uint8_t gpio_int_ctx_from_db(GpioIntCtx *ctx, uint32_t db_region, 
                             const char *base_path);

/**
 * @brief 启用指定通道的中断
 * @param ctx 上下文指针
 * @param idx 通道索引
 * @return uint8_t DIS_COMMON_ERR_OK 成功，其它错误码表示失败
 */
uint8_t gpio_int_enable_irq(GpioIntCtx *ctx, uint8_t idx);

/**
 * @brief 释放 GPIO 中断资源
 * @param ctx 上下文指针
 * @return uint8_t DIS_COMMON_ERR_OK 成功，其它错误码表示失败
 */
uint8_t gpio_int_deinit(GpioIntCtx *ctx);

/**
 * @brief 打印 GPIO 中断配置信息
 * @param ctx 上下文指针
 */
void gpio_int_print_info(const GpioIntCtx *ctx);

/**
 * @brief 初始化 GPIO 中断模块的调试日志
 * @param enable 是否启用调试日志 (1=启用, 0=禁用)
 */
void gpio_int_debug_init(uint8_t enable);

// ============ PAP 服务专用接口 ============

/**
 * @brief PAP 服务专用的 GPIO 中断初始化
 * @param fd   uio中断设备的文件描述符指针，指向fd数组
 * @param gpio_data PAP服务使用的GPIO数据结构
 * @return uint8_t DIS_COMMON_ERR_OK 成功，其它错误码表示失败
 */
uint8_t gpio_interrupt_init_Pap(int *fd, user_gpio_t *gpio_data);

#endif