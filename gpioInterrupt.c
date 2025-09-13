#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <stdbool.h>
#include "dis_dfe8219_dataBase.h"
#include "dis_dfe8219_api.h"
#include "gpioInterrupt.h"
#include "dis_dfe8219_log.h"

/* Global GPIO interrupt context for system-wide use */
GpioIntCtx g_gpio_system_ctx;
static bool g_gpio_system_initialized = false;

/* GPIO interrupt monitoring thread variables */
static pthread_t g_gpio_monitor_thread;
static bool g_gpio_monitor_running = false;
static int g_gpio_epoll_fd = -1;

/* GPIO interrupt callback function array */
static gpio_interrupt_callback_t g_gpio_callbacks[MAX_INT_CNT] = {NULL};

/* Channel running status and mutex protection */
static bool g_channel_is_running[MAX_INT_CNT] = {false};
static pthread_mutex_t g_channel_mutex[MAX_INT_CNT];
static bool g_mutex_initialized = false;

/* ========== Callback Thread Support ========== */

/**
 * @brief Structure to pass data to callback thread
 */
typedef struct {
    uint8_t channel;
    int gpio_value;
    gpio_interrupt_callback_t callback;
    pthread_mutex_t *mutex;
} CallbackThreadData;

/* ========== Private Helper Functions ========== */

/* Forward declarations for static functions */
static uint8_t gpio_int_start_monitor_thread(void);
static uint8_t gpio_int_stop_monitor_thread(void);

/**
 * @brief Initialize channel mutexes
 * @return uint8_t DIS_COMMON_ERR_OK on success, error code on failure
 */
static uint8_t init_channel_mutexes(void)
{
    if (g_mutex_initialized) {
        return DIS_COMMON_ERR_OK;
    }
    
    for (uint8_t i = 0; i < MAX_INT_CNT; i++) {
        if (pthread_mutex_init(&g_channel_mutex[i], NULL) != 0) {
            /* Cleanup previously initialized mutexes */
            for (uint8_t j = 0; j < i; j++) {
                pthread_mutex_destroy(&g_channel_mutex[j]);
            }
            return DIS_COMMON_ERR_API_FAIL;
        }
        g_channel_is_running[i] = false;
    }
    
    g_mutex_initialized = true;
    return DIS_COMMON_ERR_OK;
}

/**
 * @brief Cleanup channel mutexes
 */
static void cleanup_channel_mutexes(void)
{
    if (!g_mutex_initialized) {
        return;
    }
    
    for (uint8_t i = 0; i < MAX_INT_CNT; i++) {
        pthread_mutex_destroy(&g_channel_mutex[i]);
        g_channel_is_running[i] = false;
    }
    
    g_mutex_initialized = false;
}

/**
 * @brief Thread function to execute GPIO callback
 * @param arg Pointer to CallbackThreadData structure
 * @return void* Thread return value
 */
static void* gpio_callback_thread_func(void *arg)
{
    CallbackThreadData *data = (CallbackThreadData *)arg;
    
    if (data && data->callback && data->mutex) {
        /* Execute the callback function */
        data->callback(data->channel, data->gpio_value);
        
        /* Clear the running flag after callback execution */
        pthread_mutex_lock(data->mutex);
        g_channel_is_running[data->channel] = false;
        pthread_mutex_unlock(data->mutex);
    }
    
    /* Free the allocated data */
    free(data);
    return NULL;
}

/**
 * @brief GPIO interrupt service routine
 * @param channel GPIO interrupt channel number
 * @param gpio_ctx Pointer to GPIO interrupt context
 * 
 * This function reads GPIO value and calls registered callbacks.
 */
static void gpio_interrupt_handler(uint8_t channel, GpioIntCtx *gpio_ctx)
{    
    if (channel >= MAX_INT_CNT) {
        DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Invalid GPIO interrupt channel: %u\n", channel);
        return;
    }
    
    /* Read current GPIO value */
    int gpio_value = gpiod_line_get_value(gpio_ctx->line[channel]);
    if (gpio_value < 0) {
        DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Failed to read GPIO value for channel %u\n", channel);
        return;
    }
 
    /* Call registered callback if available */
    if (g_gpio_callbacks[channel] != NULL && !g_channel_is_running[channel]) {
        /* Set running flag */
        pthread_mutex_lock(&g_channel_mutex[channel]);
        g_channel_is_running[channel] = true;
        pthread_mutex_unlock(&g_channel_mutex[channel]);
        
        /* Create a new thread to execute the callback */
        CallbackThreadData *data = (CallbackThreadData *)malloc(sizeof(CallbackThreadData));
        if (!data) {
            /* Reset running flag if memory allocation fails */
            pthread_mutex_lock(&g_channel_mutex[channel]);
            g_channel_is_running[channel] = false;
            pthread_mutex_unlock(&g_channel_mutex[channel]);
            DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Failed to allocate memory for callback thread data\n");
            return;
        }
        data->channel = channel;
        data->gpio_value = gpio_value;
        data->callback = g_gpio_callbacks[channel];
        data->mutex = &g_channel_mutex[channel];

        pthread_t thread;
        if (pthread_create(&thread, NULL, gpio_callback_thread_func, data) != 0) {
            /* Reset running flag if thread creation fails */
            pthread_mutex_lock(&g_channel_mutex[channel]);
            g_channel_is_running[channel] = false;
            pthread_mutex_unlock(&g_channel_mutex[channel]);
            DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Failed to create callback thread for channel %u\n", channel);
            free(data);
        } else {
            pthread_detach(thread);
        }
    } 
}

/**
 * @brief Initialize UIO device for a channel
 * @param cfg Pin configuration
 * @param fd_ptr Pointer to file descriptor to set
 * @return uint8_t DIS_COMMON_ERR_OK on success, error code on failure
 */
static uint8_t init_uio_device(const GpioIntPinCfg *cfg, int *fd_ptr)
{
    char dev_path[32];
    snprintf(dev_path, sizeof(dev_path), "/dev/uio%u", cfg->uio_index);
    
    *fd_ptr = open(dev_path, O_RDWR);
    if (*fd_ptr < 0) {
        return DIS_COMMON_ERR_API_FAIL;
    }
    
    return DIS_COMMON_ERR_OK;
}

/**
 * @brief Initialize GPIO line for a channel
 * @param cfg Pin configuration  
 * @param line_ptr Pointer to gpiod line to set
 * @return uint8_t DIS_COMMON_ERR_OK on success, error code on failure
 */
static uint8_t init_gpio_line(const GpioIntPinCfg *cfg, struct gpiod_line **line_ptr)
{
    char chipname[16];
    snprintf(chipname, sizeof(chipname), "gpiochip%u", cfg->group_id);
    
    struct gpiod_chip *chip = gpiod_chip_open_by_name(chipname);
    if (!chip) {
        return DIS_COMMON_ERR_API_FAIL;
    }
    
    *line_ptr = gpiod_chip_get_line(chip, cfg->group_bit);
    if (!*line_ptr) {
        return DIS_COMMON_ERR_API_FAIL;
    }
    
    if (gpiod_line_request_input(*line_ptr, cfg->consumer) != 0) {
        return DIS_COMMON_ERR_API_FAIL;
    }
    
    return DIS_COMMON_ERR_OK;
}

/**
 * @brief Initialize a single GPIO interrupt channel
 * @param ctx Context pointer
 * @param channel_idx Channel index
 * @return uint8_t DIS_COMMON_ERR_OK on success, error code on failure
 */
static uint8_t init_single_channel(GpioIntCtx *ctx, uint8_t channel_idx)
{
    const GpioIntPinCfg *cfg = &ctx->pin_cfg[channel_idx];
    uint8_t ret;
    
    /* Initialize UIO device */
    ret = init_uio_device(cfg, &ctx->fd[channel_idx]);
    if (ret != DIS_COMMON_ERR_OK) {
        return ret;
    }
    
    /* Set GPIO pinmux */
    gpio_setPinmux(cfg->group_id, cfg->group_bit, 1);
    
    /* Initialize GPIO line */
    ret = init_gpio_line(cfg, &ctx->line[channel_idx]);
    if (ret != DIS_COMMON_ERR_OK) {
        close(ctx->fd[channel_idx]);
        ctx->fd[channel_idx] = -1;
        return ret;
    }
    
    return DIS_COMMON_ERR_OK;
}

/**
 * @brief GPIO interrupt monitoring thread function
 * @param arg Thread argument (unused)
 * @return void* Thread return value
 */
static void* gpio_interrupt_monitor_thread(void *arg)
{
    (void)arg; 
    
    struct epoll_event events[MAX_INT_CNT];
    int icount;
    
    while (g_gpio_monitor_running) {
        /* Wait for interrupt events */
        int n = epoll_wait(g_gpio_epoll_fd, events, MAX_INT_CNT, -1);
        
        if (n < 0) {
            if (g_gpio_monitor_running) {
                DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "epoll_wait failed in GPIO monitor thread\n");
            }
            break;
        }
        
        /* Process interrupt events */
        for (int i = 0; i < n; i++) {
            uint8_t channel = events[i].data.u32;
            
            /* Read interrupt count to clear the interrupt */
            int fd = g_gpio_system_ctx.fd[channel];
            if (read(fd, &icount, sizeof(icount)) > 0) {
                /* Call interrupt handler */
                gpio_interrupt_handler(channel, &g_gpio_system_ctx);
                
                /* Re-enable interrupt */
                int irq_on = 1;
                if (write(fd, &irq_on, sizeof(irq_on)) != sizeof(irq_on)) {
                    DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Failed to re-enable IRQ for channel %u\n", channel);
                }
            }
        }
    }
    
    DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 2, "GPIO interrupt monitor thread stopped\n");
    return NULL;
}

/* ========== Public API Functions ========== */

void gpio_int_debug_init(uint8_t enable)
{
    setModuleTraceEn(GPIOINTSERVICE, enable);
}

uint8_t gpio_int_ctx_from_db(GpioIntCtx *ctx, uint32_t db_region)
{
    if (!ctx) {
        return DIS_COMMON_ERR_INV_PARAM;
    }
    
    /* Initialize database */
    uint32_t ret = dis_dfe8219_dataBaseInitWithRegion(DFE8219, db_region);
    if (ret != NO_ERROR) {
        return DIS_COMMON_ERR_API_FAIL;
    }
    
    /* Read interrupt count */
    char path[64];
    snprintf(path, sizeof(path), "/GPIOINT/IntCount");
    ret = dis_dfe8219_dataBaseGetU8(DFE8219, db_region, path, &ctx->int_cnt, 1);
    if (ret != NO_ERROR) {
        return DIS_COMMON_ERR_API_FAIL;
    }
    
    if (ctx->int_cnt > MAX_INT_CNT) {
        return DIS_COMMON_ERR_INV_PARAM;
    }
    
    /* Read enable list */
    snprintf(path, sizeof(path), "/GPIOINT/enable_list");
    ret = dis_dfe8219_dataBaseGetU8(DFE8219, db_region, path, ctx->enable_list, ctx->int_cnt);
    if (ret != NO_ERROR) {
        return DIS_COMMON_ERR_API_FAIL;
    }
    
    /* Read channel configurations */
    for (uint8_t i = 0; i < ctx->int_cnt; ++i) {
        if (ctx->enable_list[i] == 0) {
            continue; /* Skip disabled channels */
        }
        
        /* Read pin configuration */
        snprintf(path, sizeof(path), "/GPIOINT/ch%u/pin_cfg", i);
        ret = dis_dfe8219_dataBaseGetU8(DFE8219, db_region, path, (uint8_t*)&ctx->pin_cfg[i], 3);
        if (ret != NO_ERROR) {
            return DIS_COMMON_ERR_API_FAIL;
        }
        
        /* Read consumer string */
        snprintf(path, sizeof(path), "/GPIOINT/ch%u/consumer", i);
        ret = dis_dfe8219_dataBaseGet(DFE8219, db_region, path, ctx->pin_cfg[i].consumer);
        if (ret != NO_ERROR) {
            return DIS_COMMON_ERR_API_FAIL;
        }
    }
    
    return DIS_COMMON_ERR_OK;
}

uint8_t gpio_int_init(GpioIntCtx *ctx)
{
    if (!ctx || ctx->int_cnt == 0 || ctx->int_cnt > MAX_INT_CNT) {
        return DIS_COMMON_ERR_INV_PARAM;
    }
    
    for (uint8_t i = 0; i < ctx->int_cnt; ++i) {
        if (ctx->enable_list[i] == 0) {
            /* Mark as uninitialized */
            ctx->fd[i] = -1;
            ctx->line[i] = NULL;
            continue;
        }
        
        uint8_t ret = init_single_channel(ctx, i);
        if (ret != DIS_COMMON_ERR_OK) {
            return ret;
        }
    }
    
    return DIS_COMMON_ERR_OK;
}

uint8_t gpio_int_enable_irq(GpioIntCtx *ctx, uint8_t idx)
{
    if (!ctx || idx >= ctx->int_cnt) {
        return DIS_COMMON_ERR_INV_PARAM;
    }
    
    /* Check if channel is enabled */
    if (ctx->enable_list[idx] == 0) {
        return DIS_COMMON_ERR_INV_PARAM;
    }
    
    /* Check if file descriptor is valid */
    if (ctx->fd[idx] < 0) {
        return DIS_COMMON_ERR_API_FAIL;
    }
    
    // /* Clear any pending interrupt */
    // int count = 0;
    // read(ctx->fd[idx], &count, sizeof(count));
    
    /* Enable interrupt */
    int irq_on = 1;
    if (write(ctx->fd[idx], &irq_on, sizeof(irq_on)) != sizeof(irq_on)) {
        return DIS_COMMON_ERR_API_FAIL;
    }
    
    return DIS_COMMON_ERR_OK;
}

uint8_t gpio_int_deinit(GpioIntCtx *ctx)
{
    if (!ctx) {
        return DIS_COMMON_ERR_INV_PARAM;
    }
    
    for (uint8_t i = 0; i < ctx->int_cnt; ++i) {
        if (ctx->enable_list[i] == 0) {
            continue; /* Skip disabled channels */
        }
        
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

void gpio_int_print_info(const GpioIntCtx *ctx)
{
    if (!ctx) {
        return;
    }
    
    DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 2, "GPIO Interrupt Configuration: %u channels\n", ctx->int_cnt);
    
    for (uint8_t i = 0; i < ctx->int_cnt; i++) {
        if (ctx->enable_list[i] == 1) {
            const GpioIntPinCfg *cfg = &ctx->pin_cfg[i];
            DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 2, "  Ch[%u]: group%u.bit%u -> uio%u (%s)\n", 
                           i, cfg->group_id, cfg->group_bit, cfg->uio_index, cfg->consumer);
        }
    }
}

uint8_t gpio_int_system_init(void)
{
    uint8_t ret;
    
    if (g_gpio_system_initialized) {
        DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 1, "GPIO interrupt system already initialized\n");
        return DIS_COMMON_ERR_OK;
    }
    
    /* Initialize channel mutexes */
    ret = init_channel_mutexes();
    if (ret != DIS_COMMON_ERR_OK) {
        DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Failed to initialize channel mutexes\n");
        return ret;
    }
    
    /* Load GPIO interrupt configuration from database */
    ret = gpio_int_ctx_from_db(&g_gpio_system_ctx, GPIOINTERRUPT);
    if (ret != DIS_COMMON_ERR_OK) {
        DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Failed to load GPIO interrupt config from database\n");
        return ret;
    }
    
    /* Initialize all enabled GPIO interrupt channels */
    ret = gpio_int_init(&g_gpio_system_ctx);
    if (ret != DIS_COMMON_ERR_OK) {
        DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Failed to initialize GPIO interrupt channels\n");
        return ret;
    }
    
    /* Print configuration information */
    gpio_int_print_info(&g_gpio_system_ctx);
    
    g_gpio_system_initialized = true;
    
    /* Start GPIO interrupt monitoring thread */
    ret = gpio_int_start_monitor_thread();
    if (ret != DIS_COMMON_ERR_OK) {
        DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Failed to start GPIO interrupt monitor thread\n");
        g_gpio_system_initialized = false;
        return ret;
    }
    
    DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 2, "GPIO interrupt system initialized successfully\n");
    
    return DIS_COMMON_ERR_OK;
}

static uint8_t gpio_int_start_monitor_thread(void)
{
    if (g_gpio_monitor_running) {
        DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 1, "GPIO monitor thread already running\n");
        return DIS_COMMON_ERR_OK;
    }
        
    /* Create epoll instance */
    g_gpio_epoll_fd = epoll_create1(0);
    if (g_gpio_epoll_fd < 0) {
        DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Failed to create epoll instance\n");
        return DIS_COMMON_ERR_API_FAIL;
    }
    
    struct epoll_event ev;
    ev.events = EPOLLIN;
    
    /* Add all enabled GPIO interrupt channels to epoll */
    for (uint8_t i = 0; i < g_gpio_system_ctx.int_cnt; i++) {
        if (g_gpio_system_ctx.enable_list[i] == 1) {
            ev.data.u32 = i; /* Store channel number */
            if (epoll_ctl(g_gpio_epoll_fd, EPOLL_CTL_ADD, g_gpio_system_ctx.fd[i], &ev) != 0) {
                DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Failed to add channel %u to epoll\n", i);
                close(g_gpio_epoll_fd);
                g_gpio_epoll_fd = -1;
                return DIS_COMMON_ERR_API_FAIL;
            }
            
            /* Enable interrupt for this channel */
            uint8_t ret = gpio_int_enable_irq(&g_gpio_system_ctx, i);
            if (ret != DIS_COMMON_ERR_OK) {
                DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Failed to enable IRQ for channel %u\n", i);
                close(g_gpio_epoll_fd);
                g_gpio_epoll_fd = -1;
                return ret;
            }
            
            DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 2, "Added channel %u (%s) to interrupt monitoring\n", 
                           i, g_gpio_system_ctx.pin_cfg[i].consumer);
        }
    }
    
    /* Start monitoring thread */
    g_gpio_monitor_running = true;
    if (pthread_create(&g_gpio_monitor_thread, NULL, gpio_interrupt_monitor_thread, NULL) != 0) {
        DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Failed to create GPIO monitor thread\n");
        g_gpio_monitor_running = false;
        close(g_gpio_epoll_fd);
        g_gpio_epoll_fd = -1;
        return DIS_COMMON_ERR_API_FAIL;
    }
    
    return DIS_COMMON_ERR_OK;
}

static uint8_t gpio_int_stop_monitor_thread(void)
{
    if (!g_gpio_monitor_running) {
        DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 1, "GPIO monitor thread not running\n");
        return DIS_COMMON_ERR_OK;
    }
    
    /* Signal thread to stop */
    g_gpio_monitor_running = false;
    
    /* Wait for thread to finish */
    if (pthread_join(g_gpio_monitor_thread, NULL) != 0) {
        DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Failed to join GPIO monitor thread\n");
    }
    
    /* Close epoll file descriptor */
    if (g_gpio_epoll_fd >= 0) {
        close(g_gpio_epoll_fd);
        g_gpio_epoll_fd = -1;
    }
    
    DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 2, "GPIO interrupt monitor thread stopped\n");
    return DIS_COMMON_ERR_OK;
}

uint8_t gpio_int_register_callback(uint8_t channel, gpio_interrupt_callback_t callback)
{
    /* Check if GPIO system is initialized */
    if (!g_gpio_system_initialized) {
        DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "GPIO system not initialized\n");
        return DIS_COMMON_ERR_API_FAIL;
    }
    
    /* Check if the channel is valid and enabled */
    if (channel >= g_gpio_system_ctx.int_cnt) {
        DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Channel %u exceeds configured channel count (%u)\n", 
                         channel, g_gpio_system_ctx.int_cnt);
        return DIS_COMMON_ERR_INV_PARAM;
    }
    
    if (g_gpio_system_ctx.enable_list[channel] == 0) {
        DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 0, "Channel %u is not enabled in configuration\n", channel);
        return DIS_COMMON_ERR_INV_PARAM;
    }
    
    /* Register the callback */
    g_gpio_callbacks[channel] = callback;
    
    if (callback != NULL) {
        DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 2, "Registered callback for channel %u (%s)\n", 
                         channel, g_gpio_system_ctx.pin_cfg[channel].consumer);
    }
    
    return DIS_COMMON_ERR_OK;
}

uint8_t gpio_int_system_deinit(void)
{
    if (!g_gpio_system_initialized) {
        DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 1, "GPIO interrupt system not initialized\n");
        return DIS_COMMON_ERR_OK;
    }
    
    /* Stop monitoring thread */
    gpio_int_stop_monitor_thread();
    
    /* Deinitialize GPIO context */
    gpio_int_deinit(&g_gpio_system_ctx);
    
    /* Clear all callbacks */
    for (uint8_t i = 0; i < MAX_INT_CNT; i++) {
        g_gpio_callbacks[i] = NULL;
    }
    
    /* Cleanup channel mutexes */
    cleanup_channel_mutexes();
    
    g_gpio_system_initialized = false;
    
    DEBUG_LOG_SAMPLE(GPIOINTSERVICE, 2, "GPIO interrupt system deinitialized\n");
    return DIS_COMMON_ERR_OK;
}
