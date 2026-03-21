#ifndef GPIO_H
#define GPIO_H

#include <stdint.h>
#include "util.h"

typedef struct {
    uio_dev_t ctl;
    uio_dev_t val;
} gpio_dev_t;

int  gpio_open(gpio_dev_t *dev, uint32_t ctl_addr, uint32_t val_addr);
void gpio_close(gpio_dev_t *dev);
void gpio_reset_sum(gpio_dev_t *dev);
void gpio_add(gpio_dev_t *dev, uint32_t value);
void gpio_read_values(gpio_dev_t *dev, uint32_t *sum, uint32_t *carry);
int  gpio_main(uint32_t ctl_addr, uint32_t val_addr);

#endif
