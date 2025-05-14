#ifndef __GPIOD_INTERRUPT_H__
#define __GPIOD_INTERRUPT_H__

#include <gpiod.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <paProtection.h>

/**
 * @brief Edge-triggered interrupts on  GPIO line.
 * @param chip_number The GPIO chip number.
 * @param line_number GPIO line number.
 * @param event_mode "rising" or "falling" or "both"
 * @return 0 if the operation succeeds, -1 on failure.
 */
int gpiod_interrupt(char *chip_number=NULL, char *line_number=NULL, char *event_mode);

void gpio_interrupt_handler(void);

void start_pa_process(void);
#endif