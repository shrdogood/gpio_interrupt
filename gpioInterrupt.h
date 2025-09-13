#ifndef _GPIOINTERRUPT_H_
#define _GPIOINTERRUPT_H_

#include <gpiod.h>
#include <stdint.h>
#include <sys/epoll.h>
#include "dis_dfe8219_board.h"
#include "gpio_pinmux.h"

/* Maximum number of GPIO interrupt channels supported */
#define MAX_INT_CNT 8

/* ========== Data Structures ========== */

/**
 * @brief GPIO interrupt callback function type
 * @param channel GPIO interrupt channel number
 * @param gpio_value Current GPIO value (0 or 1)
 * 
 * This function type is used for service-specific interrupt handlers.
 * Services can register their interrupt handlers using this function type. 
 */
typedef void (*gpio_interrupt_callback_t)(uint8_t channel, int gpio_value);

/**
 * @brief GPIO interrupt pin configuration
 */
typedef struct {
    uint8_t  group_id;       /* GPIO group number (corresponds to gpiochipX) */
    uint8_t  group_bit;      /* GPIO bit number within the group */
    uint8_t  uio_index;      /* UIO device index for /dev/uio<uio_index> */
    char     consumer[16];   /* gpiod consumer identifier string */
} GpioIntPinCfg;

/**
 * @brief GPIO interrupt context structure
 */
typedef struct {
    uint8_t             int_cnt;                    /* Number of interrupt channels */
    uint8_t             enable_list[MAX_INT_CNT];   /* Enable list: 1=init, 0=skip */
    GpioIntPinCfg       pin_cfg[MAX_INT_CNT];      /* Pin configuration array */
    int                 fd[MAX_INT_CNT];           /* UIO file descriptors */
    struct gpiod_line   *line[MAX_INT_CNT];        /* gpiod line handles */
} GpioIntCtx;

extern GpioIntCtx g_gpio_system_ctx;

/**
 * @brief Initialize GPIO interrupt module debug logging
 * @param enable Enable debug logging (1=enable, 0=disable)
 */
void gpio_int_debug_init(uint8_t enable);

/**
 * @brief Initialize complete GPIO interrupt system
 * @return uint8_t DIS_COMMON_ERR_OK on success, error code on failure
 * 
 * This function initializes all enabled GPIO interrupt channels from database
 * configuration and prepares them for use by different services.
 */
uint8_t gpio_int_system_init(void);

/**
 * @brief Register GPIO interrupt callback function for specific channel
 * @param channel GPIO interrupt channel number
 * @param callback Callback function pointer (NULL to unregister)
 * @return uint8_t DIS_COMMON_ERR_OK on success, error code on failure
 * 
 * This function allows services to register their specific interrupt handlers
 * for GPIO channels. When an interrupt occurs on the specified channel,
 * the registered callback will be called with the current GPIO value (0 or 1).
 */
uint8_t gpio_int_register_callback(uint8_t channel, gpio_interrupt_callback_t callback);

/**
 * @brief Deinitialize complete GPIO interrupt system
 * @return uint8_t DIS_COMMON_ERR_OK on success, error code on failure
 * 
 * This function stops the monitoring thread, cleans up all resources,
 * and deinitializes the GPIO interrupt system.
 */
uint8_t gpio_int_system_deinit(void);

#endif