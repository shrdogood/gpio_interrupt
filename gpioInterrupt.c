#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include "database.h"
#include "dis_dfe8219_api.h"
#include "gpioInterrupt.h"
#include "commonLog.h"

uint8_t group_list[MAX_INT_CNT];
uint8_t group_bit_list[MAX_INT_CNT];

/**
 * @brief 初始化 GPIO 中断模块的调试日志
 */
void gpio_int_debug_init(uint8_t enable)
{
    setModuleTraceEn(GPIOINTSERVICE, enable);
}

/**
 * @brief 通用 GPIO 中断初始化接口
 */
uint8_t gpio_int_init(GpioIntCtx *ctx)
{
    if (!ctx || ctx->int_cnt == 0 || ctx->int_cnt > MAX_INT_CNT) {
        DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Error: Invalid GPIO interrupt context\n");
        return DIS_COMMON_ERR_INV_PARAM;
    }

    for (uint8_t i = 0; i < ctx->int_cnt; ++i) {
        const GpioIntPinCfg *cfg = &ctx->pin_cfg[i];

        // 1. 打开 /dev/uioX
        char dev_path[32];
        snprintf(dev_path, sizeof(dev_path), "/dev/uio%u", cfg->uio_index);
        ctx->fd[i] = open(dev_path, O_RDWR);
        if (ctx->fd[i] < 0) {
            DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Error: Failed to open %s\n", dev_path);
            return DIS_COMMON_ERR_API_FAIL;
        }

        // 2. 设置GPIO复用
        gpio_setPinmux(cfg->group_id, cfg->group_bit, 1);

        // 3. 打开gpiochip并请求line
        char chipname[16];
        snprintf(chipname, sizeof(chipname), "gpiochip%u", cfg->group_id);
        struct gpiod_chip *chip = gpiod_chip_open_by_name(chipname);
        if (!chip) {
            DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Error: Failed to open %s\n", chipname);
            close(ctx->fd[i]);
            return DIS_COMMON_ERR_API_FAIL;
        }

        ctx->line[i] = gpiod_chip_get_line(chip, cfg->group_bit);
        if (!ctx->line[i]) {
            DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Error: Failed to get group_bit %u from %s\n", cfg->group_bit, chipname);
            close(ctx->fd[i]);
            return DIS_COMMON_ERR_API_FAIL;
        }

        if (gpiod_line_request_input(ctx->line[i], cfg->consumer) != 0) {
            DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Error: Failed to request input for group_bit %u\n", cfg->group_bit);
            close(ctx->fd[i]);
            return DIS_COMMON_ERR_API_FAIL;
        }
    }

    return DIS_COMMON_ERR_OK;
}


uint8_t gpio_int_init_channel(GpioIntCtx *ctx, uint8_t channel_idx)
{
    if (!ctx || channel_idx >= ctx->int_cnt) {
        DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Error: Invalid channel index %u (max: %u)\n", 
                         channel_idx, ctx->int_cnt);
        return DIS_COMMON_ERR_INV_PARAM;
    }

    const GpioIntPinCfg *cfg = &ctx->pin_cfg[channel_idx];

    // 1. 打开 /dev/uioX
    char dev_path[32];
    snprintf(dev_path, sizeof(dev_path), "/dev/uio%u", cfg->uio_index);
    ctx->fd[channel_idx] = open(dev_path, O_RDWR);
    if (ctx->fd[channel_idx] < 0) {
        DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Error: Failed to open %s for channel %u\n", 
                         dev_path, channel_idx);
        return DIS_COMMON_ERR_API_FAIL;
    }

    // 2. 设置GPIO复用
    gpio_setPinmux(cfg->group_id, cfg->group_bit, 1);

    // 3. 打开gpiochip并请求line
    char chipname[16];
    snprintf(chipname, sizeof(chipname), "gpiochip%u", cfg->group_id);
    struct gpiod_chip *chip = gpiod_chip_open_by_name(chipname);
    if (!chip) {
        DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Error: Failed to open %s for channel %u\n", 
                         chipname, channel_idx);
        close(ctx->fd[channel_idx]);
        ctx->fd[channel_idx] = -1;
        return DIS_COMMON_ERR_API_FAIL;
    }

    ctx->line[channel_idx] = gpiod_chip_get_line(chip, cfg->group_bit);
    if (!ctx->line[channel_idx]) {
        DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Error: Failed to get group_bit %u from %s for channel %u\n", 
                         cfg->group_bit, chipname, channel_idx);
        close(ctx->fd[channel_idx]);
        ctx->fd[channel_idx] = -1;
        return DIS_COMMON_ERR_API_FAIL;
    }

    if (gpiod_line_request_input(ctx->line[channel_idx], cfg->consumer) != 0) {
        DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Error: Failed to request input for group_bit %u, channel %u\n", 
                         cfg->group_bit, channel_idx);
        close(ctx->fd[channel_idx]);
        ctx->fd[channel_idx] = -1;
        return DIS_COMMON_ERR_API_FAIL;
    }

    return DIS_COMMON_ERR_OK;
}

/**
 * @brief 从数据库配置初始化 GPIO 中断上下文
 */
uint8_t gpio_int_ctx_from_db(GpioIntCtx *ctx, uint32_t db_region, 
                             const char *base_path)
{
    if (!ctx || !base_path) {
        return DIS_COMMON_ERR_INV_PARAM;
    }

    // 初始化数据库
    uint32_t ret = dataBaseInitWithRegion(DFE8219, db_region);
    if (NO_ERROR != ret) {
        DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Error: Database init failed for region %u\n", db_region);
        return DIS_COMMON_ERR_API_FAIL;
    }

    // 读取中断数量
    char path[64];
    snprintf(path, sizeof(path), "%s/IntCount", base_path);
    ret = dataBaseGetU8(DFE8219, db_region, path, &ctx->int_cnt, 1);
    if (NO_ERROR != ret) {
        DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Error: Failed to read %s\n", path);
        return DIS_COMMON_ERR_API_FAIL;
    }

    if (ctx->int_cnt > MAX_INT_CNT) {
        DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Error: IntCount %u exceeds MAX_INT_CNT %u\n", ctx->int_cnt, MAX_INT_CNT);
        return DIS_COMMON_ERR_INV_PARAM;
    }

    // 读取每个通道的配置
    for (uint8_t i = 0; i < ctx->int_cnt; ++i) {
        // 读取 pin_cfg (group_id, group_bit, uio_index)
        snprintf(path, sizeof(path), "%s/ch%u/pin_cfg", base_path, i);
        ret = dataBaseGetU8(DFE8219, db_region, path, (uint8_t*)&ctx->pin_cfg[i], 3);
        if (NO_ERROR != ret) {
            DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Error: Failed to read %s\n", path);
            return DIS_COMMON_ERR_API_FAIL;
        }
        
        // 读取 consumer
        snprintf(path, sizeof(path), "%s/ch%u/consumer", base_path, i);
        ret = dataBaseGet(DFE8219, db_region, path, ctx->pin_cfg[i].consumer);
        if (NO_ERROR != ret) {
            DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Error: Failed to read %s\n", path);
            return DIS_COMMON_ERR_API_FAIL;
        }
 
        DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 2, "GPIO Int[%u]: group%u.bit%u -> uio%u (%s)\n", 
               i, ctx->pin_cfg[i].group_id, ctx->pin_cfg[i].group_bit, 
               ctx->pin_cfg[i].uio_index, ctx->pin_cfg[i].consumer);
    }

    return DIS_COMMON_ERR_OK;
}

/**
 * @brief 启用指定通道的中断
 */
uint8_t gpio_int_enable_irq(GpioIntCtx *ctx, uint8_t idx)
{
    if (!ctx || idx >= ctx->int_cnt) {
        return DIS_COMMON_ERR_INV_PARAM;
    }

    int count = 0;
    read(ctx->fd[idx], &count, sizeof(count));  //清除可读信号

    int irq_on = 1;
    if (write(ctx->fd[idx], &irq_on, sizeof(irq_on)) != sizeof(irq_on)) {
        DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Error: Failed to enable IRQ for channel %u\n", idx);
        return DIS_COMMON_ERR_API_FAIL;
    }

    return DIS_COMMON_ERR_OK;
}

/**
 * @brief 释放 GPIO 中断资源
 */
uint8_t gpio_int_deinit(GpioIntCtx *ctx)
{
    if (!ctx) {
        return DIS_COMMON_ERR_INV_PARAM;
    }

    for (uint8_t i = 0; i < ctx->int_cnt; ++i) {
        if (ctx->line[i]) {
            gpiod_line_release(ctx->line[i]);
            ctx->line[i] = NULL;
        }
        if (ctx->fd[i] >= 0) {
            close(ctx->fd[i]);
            ctx->fd[i] = -1;
        }
    }

    ctx->int_cnt = 0;
    return DIS_COMMON_ERR_OK;
}

/**
 * @brief 打印 GPIO 中断配置信息
 */
void gpio_int_print_info(const GpioIntCtx *ctx)
{
    if (!ctx) {
        DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 1, "GPIO Interrupt Context: NULL\n");
        return;
    }

    DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 2, "GPIO Interrupt Configuration:\n");
    DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 2, "Total Interrupts: %u\n", ctx->int_cnt);

    for (uint8_t i = 0; i < ctx->int_cnt; i++) {
        const GpioIntPinCfg *cfg = &ctx->pin_cfg[i];
        DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 2, "  Channel[%u]: group%u.bit%u -> /dev/uio%u (fd:%d) [%s]\n", 
               i, cfg->group_id, cfg->group_bit, cfg->uio_index, 
               ctx->fd[i], cfg->consumer);
    }
}

// ============ PAP 服务专用接口实现 ============

uint8_t gpio_interrupt_init_Pap(int *fd, user_gpio_t *gpio_data)
{
    uint32_t ret = dataBaseInitWithRegion(DFE8219, PAPSERVICEMAP);
    if (NO_ERROR != ret)
    {
        perror("db init for PAPSERVICEMAP is failed.\n");
        return DIS_COMMON_ERR_API_FAIL;
    }

    ret = dataBaseGetU8(DFE8219, PAPSERVICEMAP, "/papSvc/Count", &(gpio_data->IntCount), 1);
    if (NO_ERROR != ret)
    {
        perror("did not found /papSvc/Count. db init for PAPSERVICEMAP is failed.\n");
        return DIS_COMMON_ERR_API_FAIL;
    }

    for (uint8_t index = 0; index < gpio_data->IntCount; index++)
    {
        char str[32] = "";
        uint8_t temp[3];

        snprintf(str, sizeof(str), "/dev/uio%d", index+2);
        fd[index] = open(str, O_RDWR); 
        memset(str, 0, sizeof(str));

        snprintf(str, sizeof(str), "/papSvc/gpioInt%d/val", index);
        dataBaseGetU8(DFE8219, PAPSERVICEMAP, str, temp, 3);
        memset(str, 0, sizeof(str));
        
        group_list[index] = temp[1];
        group_bit_list[index] = temp[2];

        gpio_setPinmux(group_list[index], group_bit_list[index], 1); //设置GPIO复用为GPIO模式
        
        snprintf(str, sizeof(str), "gpiochip%d", group_list[index]);
        struct gpiod_chip *gpio_group = gpiod_chip_open_by_name(str);  //返回gpiochip
        assert(gpio_group);
        memset(str, 0, sizeof(str));

        gpio_data->line[index] = gpiod_chip_get_line(gpio_group, group_bit_list[index]);  //返回gpioline
        assert(gpio_data->line[index]);

        assert(gpiod_line_request_input(gpio_data->line[index], "uio_demo") == 0);  //设置为输入模式 方便读取电平        
    }

    return DIS_COMMON_ERR_OK;
}