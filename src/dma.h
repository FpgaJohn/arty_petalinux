#ifndef DMA_H
#define DMA_H

#include <stdint.h>
#include "util.h"

/* AXI DMA register offsets — simple DMA mode (PG021) */
#define DMA_MM2S_DMACR    0x00  /* MM2S control */
#define DMA_MM2S_DMASR    0x04  /* MM2S status */
#define DMA_MM2S_SA       0x18  /* MM2S source address */
#define DMA_MM2S_LENGTH   0x28  /* MM2S length — writing triggers transfer */
#define DMA_S2MM_DMACR    0x30  /* S2MM control */
#define DMA_S2MM_DMASR    0x34  /* S2MM status */
#define DMA_S2MM_DA       0x48  /* S2MM destination address */
#define DMA_S2MM_LENGTH   0x58  /* S2MM length — writing arms receiver */

#define DMA_CR_RS         (1u << 0)   /* Run/Stop */
#define DMA_CR_RESET      (1u << 2)   /* Channel reset (self-clearing) */
#define DMA_SR_HALTED     (1u << 0)   /* Channel halted (stopped or error) */
#define DMA_SR_IDLE       (1u << 1)   /* Transfer complete / waiting */

#define DMA_MAP_SIZE      0x10000
#define DMA_BUF_WORDS     16          /* test transfer size in 32-bit words */

typedef struct {
    int               fd;
    volatile uint32_t *regs;
} dma_dev_t;

int  dma_open(dma_dev_t *dev, uint32_t addr);
void dma_close(dma_dev_t *dev);
void dma_reset(dma_dev_t *dev);
void dma_print_status(dma_dev_t *dev, const char *label);
int  dma_main(uint32_t dma_addr);

#endif
