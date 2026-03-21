#include "fifos.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

static void dbg_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
}

int fifo_open(fifo_dev_t *dev, uint32_t addr)
{
    char path[32];

    if (uio_find_by_addr(addr, path, sizeof(path)) < 0) {
        fprintf(stderr, "Cannot find UIO at 0x%08X\n", addr);
        return -1;
    }
    dbg_printf("FIFO @ 0x%08X -> %s\n", addr, path);

    dev->fd = open(path, O_RDWR);
    if (dev->fd < 0) { perror("open"); return -1; }

    dev->ctl = (volatile uint32_t *)mmap(NULL, MAP_SIZE,
                   PROT_READ | PROT_WRITE, MAP_SHARED, dev->fd, 0);
    if (dev->ctl == MAP_FAILED) { perror("mmap"); return -1; }

    dbg_printf("[DBG] fifo_open: mapped @ %p\n", (void *)dev->ctl);
    return 0;
}

void fifo_close(fifo_dev_t *dev)
{
    munmap((void *)dev->ctl, MAP_SIZE);
    close(dev->fd);
}

void fifo_reset(fifo_dev_t *dev)
{
    /* SRR resets the entire core — wait before any further AXI transactions */
    dev->ctl[FIFO_SRR / 4] = 0xA5;
    usleep(1000);
    dev->ctl[FIFO_ISR / 4] = 0xFFFFFFFF;  /* W1C: clear all pending interrupts */
}

uint32_t fifo_tx_vacancy(fifo_dev_t *dev)
{
    return dev->ctl[FIFO_TDFV / 4];
}

uint32_t fifo_rx_occupancy(fifo_dev_t *dev)
{
    return dev->ctl[FIFO_RDFO / 4];
}

void fifo_print_status(fifo_dev_t *dev, const char *label)
{
    printf("--- %s ---\n", label);
    printf("  ISR:          0x%08X\n", dev->ctl[FIFO_ISR  / 4]);
    printf("  TX vacancy:   %u\n",     dev->ctl[FIFO_TDFV / 4]);
    printf("  RX occupancy: %u\n",     dev->ctl[FIFO_RDFO / 4]);
    fflush(stdout);
}

int fifo_write(fifo_dev_t *dev, uint32_t *data, uint32_t num_words)
{
    uint32_t vacancy = dev->ctl[FIFO_TDFV / 4];
    if (vacancy < num_words) {
        fprintf(stderr, "TX FIFO full: vacancy=%u need=%u\n", vacancy, num_words);
        return -1;
    }

    for (uint32_t i = 0; i < num_words; i++) {
        dbg_printf("[DBG] fifo_write: word %u = 0x%08X\n", i, data[i]);
        dev->ctl[FIFO_TDFD / 4] = data[i];
    }

    dev->ctl[FIFO_TLR / 4] = num_words * 4;
    dbg_printf("[DBG] fifo_write: TLR=%u bytes, packet committed\n", num_words * 4);
    return 0;
}

/* Reads one packet from the RX FIFO using RLR to determine byte count.
 * Returns number of words read, or 0 if FIFO empty, -1 on error. */
int fifo_read(fifo_dev_t *dev, uint32_t *buf, uint32_t max_words)
{
    uint32_t occupancy = dev->ctl[FIFO_RDFO / 4];
    if (occupancy == 0) return 0;

    uint32_t rlr = dev->ctl[FIFO_RLR / 4] & 0x7FFFFFFF;  /* mask LLAST bit */
    uint32_t num_words = rlr / 4;
    if (num_words > max_words) {
        fprintf(stderr, "fifo_read: packet too large (%u words > buf %u)\n",
                num_words, max_words);
        num_words = max_words;
    }

    for (uint32_t i = 0; i < num_words; i++) {
        buf[i] = dev->ctl[FIFO_RDFD / 4];
        dbg_printf("[DBG] fifo_read: word %u = 0x%08X\n", i, buf[i]);
    }
    return (int)num_words;
}

/* Poll RDFO until data arrives or timeout_ms elapses. */
int fifo_wait_rx(fifo_dev_t *dev, uint32_t timeout_ms)
{
    for (uint32_t elapsed = 0; elapsed < timeout_ms; elapsed++) {
        if (dev->ctl[FIFO_RDFO / 4] != 0) return 0;
        usleep(1000);
    }
    return -1;
}

int fifo_main(uint32_t addr)
{
    fifo_dev_t fifo;

    printf("- fifo_open\n");
    if (fifo_open(&fifo, addr) < 0) return -1;

    printf("- fifo_reset\n");
    fifo_reset(&fifo);
    fifo_print_status(&fifo, "after reset");

    uint32_t tx_data[] = {
        0xDEADBEEF,
        0xCAFEBABE,
        0x12345678,
        0xAABBCCDD
    };
    uint32_t num_words = sizeof(tx_data) / sizeof(tx_data[0]);

    printf("\nWriting %u 32-bit words...\n", num_words);
    for (uint32_t i = 0; i < num_words; i++)
        printf("  tx[%u] = 0x%08X\n", i, tx_data[i]);

    if (fifo_write(&fifo, tx_data, num_words) < 0) goto cleanup;

    printf("\nWaiting for RX data...\n");
    if (fifo_wait_rx(&fifo, 100) < 0) {
        printf("Timeout waiting for RX data!\n");
        goto cleanup;
    }

    fifo_print_status(&fifo, "after write");

    uint32_t rx_buf[16] = {0};
    int nread = fifo_read(&fifo, rx_buf, 16);
    if (nread <= 0) { printf("Read failed\n"); goto cleanup; }

    printf("\nReceived %d 32-bit words:\n", nread);
    int pass = 1;
    for (int i = 0; i < nread; i++) {
        int match = (i < (int)num_words) && (rx_buf[i] == tx_data[i]);
        printf("  rx[%d] = 0x%08X %s\n", i, rx_buf[i], match ? "OK" : "MISMATCH");
        if (!match) pass = 0;
    }
    if (nread != (int)num_words) {
        printf("  word count mismatch: got %d expected %u\n", nread, num_words);
        pass = 0;
    }

    printf("\nResult: %s\n", pass ? "PASS" : "FAIL");

cleanup:
    fifo_close(&fifo);
    return 0;
}
