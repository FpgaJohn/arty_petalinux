#include "gpio.h"
#include "util.h"

#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

/*
 * gpio_ctl (0x41220000):
 *   Channel 1 (GPIO_DATA  0x00): control [1:0] — 0=noop, 1=add, 2=reset
 *   Channel 2 (GPIO2_DATA 0x08): value   [31:0]
 * gpio_val (0x41230000):
 *   Channel 1 (GPIO_DATA  0x00): sum   [31:0]
 *   Channel 2 (GPIO2_DATA 0x08): carry [31:0]
 */

#define GPIO_DATA    0x00
#define GPIO2_DATA   0x08
#define MAP_SIZE     0x10000

int gpio_open(gpio_dev_t *dev, uint32_t ctl_addr, uint32_t val_addr)
{
    char path[64];

    if (uio_find_by_addr(ctl_addr, path, sizeof(path)) < 0) {
        fprintf(stderr, "Cannot find GPIO_CTL UIO at 0x%08X\n", ctl_addr);
        return -1;
    }
    if (uio_open(&dev->ctl, path, MAP_SIZE) < 0) return -1;

    if (uio_find_by_addr(val_addr, path, sizeof(path)) < 0) {
        fprintf(stderr, "Cannot find GPIO_VAL UIO at 0x%08X\n", val_addr);
        uio_close(&dev->ctl, MAP_SIZE);
        return -1;
    }
    if (uio_open(&dev->val, path, MAP_SIZE) < 0) {
        uio_close(&dev->ctl, MAP_SIZE);
        return -1;
    }
    return 0;
}

void gpio_close(gpio_dev_t *dev)
{
    uio_close(&dev->ctl, MAP_SIZE);
    uio_close(&dev->val, MAP_SIZE);
}

/* Write 2 to CTL ch1 (reset pulse), then deassert to 0. */
void gpio_reset_sum(gpio_dev_t *dev)
{
    reg_write(&dev->ctl, GPIO_DATA, 2);
    reg_write(&dev->ctl, GPIO_DATA, 0);
}

/* Write value to CTL ch2, pulse CTL ch1 = 1 (add), then deassert both. */
void gpio_add(gpio_dev_t *dev, uint32_t value)
{
    reg_write(&dev->ctl, GPIO2_DATA, value);
    reg_write(&dev->ctl, GPIO_DATA,  1);
    reg_write(&dev->ctl, GPIO_DATA,  0);
    reg_write(&dev->ctl, GPIO2_DATA, 0);
}

void gpio_read_values(gpio_dev_t *dev, uint32_t *sum, uint32_t *carry)
{
    *sum   = reg_read(&dev->val, GPIO_DATA);
    *carry = reg_read(&dev->val, GPIO2_DATA);
}

int gpio_main(uint32_t ctl_addr, uint32_t val_addr)
{
    gpio_dev_t gpio;
    if (gpio_open(&gpio, ctl_addr, val_addr) < 0) return -1;

    uint32_t sum, carry;

    printf("Step 1: add 0x64\n");
    gpio_add(&gpio, 0x64);
    gpio_read_values(&gpio, &sum, &carry);
    printf("  sum=0x%08X (%u)  carry=0x%08X (%u)\n", sum, sum, carry, carry);

    printf("Step 2: add 0x1\n");
    gpio_add(&gpio, 0x1);
    gpio_read_values(&gpio, &sum, &carry);
    printf("  sum=0x%08X (%u)  carry=0x%08X (%u)\n", sum, sum, carry, carry);

    printf("Step 3: reset\n");
    gpio_reset_sum(&gpio);
    gpio_read_values(&gpio, &sum, &carry);
    printf("  sum=0x%08X (%u)  carry=0x%08X (%u)\n", sum, sum, carry, carry);

    gpio_close(&gpio);
    return 0;
}
