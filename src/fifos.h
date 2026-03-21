#ifndef FIFO_H
#define FIFO_H

#include <stdint.h>
#include "util.h"

/* AXI Stream FIFO register offsets — C_DATA_INTERFACE_TYPE=0 (AXI-Lite, PG080) */
#define FIFO_ISR    0x00  /* Interrupt Status Register (W1C) */
#define FIFO_IER    0x04  /* Interrupt Enable Register */
#define FIFO_TDFR   0x08  /* Transmit Data FIFO Reset */
#define FIFO_TDFV   0x0C  /* Transmit Data FIFO Vacancy */
#define FIFO_TDFD   0x10  /* Transmit Data FIFO 32-bit write port */
#define FIFO_TLR    0x14  /* Transmit Length Register — commits packet */
#define FIFO_RDFR   0x18  /* Receive Data FIFO Reset */
#define FIFO_RDFO   0x1C  /* Receive Data FIFO Occupancy */
#define FIFO_RDFD   0x20  /* Receive Data FIFO 32-bit read port */
#define FIFO_RLR    0x24  /* Receive Length Register (bit 31 = LLAST) */
#define FIFO_SRR    0x28  /* AXI4-Stream Reset (write 0xA5) */
#define FIFO_TDR    0x2C  /* Transmit Destination Register */
#define FIFO_RDR    0x30  /* Receive Destination Register */

#define MAP_SIZE    0x10000

typedef struct {
    int               fd;
    volatile uint32_t *ctl;
} fifo_dev_t;

int      fifo_open(fifo_dev_t *dev, uint32_t addr);
void     fifo_close(fifo_dev_t *dev);
void     fifo_reset(fifo_dev_t *dev);
uint32_t fifo_tx_vacancy(fifo_dev_t *dev);
uint32_t fifo_rx_occupancy(fifo_dev_t *dev);
int      fifo_write(fifo_dev_t *dev, uint32_t *data, uint32_t num_words);
int      fifo_read(fifo_dev_t *dev, uint32_t *buf, uint32_t max_words);
int      fifo_wait_rx(fifo_dev_t *dev, uint32_t timeout_ms);
void     fifo_print_status(fifo_dev_t *dev, const char *label);
int      fifo_main(uint32_t addr);

#endif /* FIFO_H */
