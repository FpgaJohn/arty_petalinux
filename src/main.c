#include <stdio.h>
#include <stdint.h>

#include "gpio.h"
#include "fifos.h"
#include "dma.h"

/*
 * uio0: dma_0         @ 0x40400000
 * uio1: dma_1         @ 0x40410000
 * uio2: fifo_mm_s_0   @ 0x43c00000
 * uio3: fifo_mm_s_1   @ 0x43c10000
 * uio4: gpio_control  @ 0x41220000
 * uio5: gpio_values   @ 0x41230000
 */

static int read_int(const char *prompt, int *out)
{
    printf("%s", prompt);
    fflush(stdout);
    return scanf("%d", out);
}

static void menu_gpio(void)
{
    gpio_dev_t gpio;
    if (gpio_open(&gpio, 0x41220000, 0x41230000) < 0) return;

    int choice;
    do {
        printf("\n--- GPIO Test ---\n");
        printf("  (1) Reset SUM\n");
        printf("  (2) Add\n");
        printf("  (3) Return\n");

        if (read_int("Choice: ", &choice) != 1) { choice = 0; }

        uint32_t sum, carry;

        switch (choice) {
        case 1:
            gpio_reset_sum(&gpio);
            gpio_read_values(&gpio, &sum, &carry);
            printf("  sum=0x%08X (%u)  carry=0x%08X (%u)\n",
                   sum, sum, carry, carry);
            break;

        case 2: {
            int value;
            if (read_int("Value to add: ", &value) != 1) break;
            gpio_add(&gpio, (uint32_t)value);
            gpio_read_values(&gpio, &sum, &carry);
            printf("  sum=0x%08X (%u)  carry=0x%08X (%u)\n",
                   sum, sum, carry, carry);
            break;
        }

        case 3:
            break;

        default:
            printf("Invalid choice.\n");
        }
    } while (choice != 3);

    gpio_close(&gpio);
}

int main(void)
{
    printf("c_fifo_hw\n");

    int choice;
    do {
        printf("\n=== Main Menu ===\n");
        printf("  (1) Run GPIO Test\n");
        printf("  (2) Run FIFO Test\n");
        printf("  (3) Run DMA Test\n");
        printf("  (4) Exit\n");

        if (read_int("Choice: ", &choice) != 1) { choice = 0; }

        switch (choice) {
        case 1: menu_gpio();              break;
        case 2: fifo_main(0x43c00000);    break;
        case 3: dma_main(0x40400000);     break;
        case 4: printf("Bye.\n");         break;
        default: printf("Invalid choice.\n");
        }
    } while (choice != 4);

    return 0;
}
