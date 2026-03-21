#include "dma.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
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

/* Translate a locked virtual address to its DDR physical address. */
static uint32_t virt_to_phys(void *vaddr)
{
    long page_size = sysconf(_SC_PAGE_SIZE);
    int fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd < 0) { perror("pagemap open"); return 0; }

    uint64_t entry;
    off_t off = ((uintptr_t)vaddr / (uintptr_t)page_size) * (off_t)sizeof(entry);
    if (pread(fd, &entry, sizeof(entry), off) != (ssize_t)sizeof(entry)) {
        perror("pagemap pread");
        close(fd);
        return 0;
    }
    close(fd);

    if (!(entry & (1ULL << 63))) {
        fprintf(stderr, "virt_to_phys: page not present in RAM\n");
        return 0;
    }

    uint64_t pfn = entry & ((1ULL << 55) - 1);
    return (uint32_t)(pfn * (uint64_t)page_size +
                      ((uintptr_t)vaddr & (uintptr_t)(page_size - 1)));
}

/* Map a physical DDR address as non-cacheable through /dev/mem (O_SYNC).
 * On ARM Linux, O_SYNC causes the kernel to apply pgprot_noncached, so all
 * reads/writes bypass L1 and L2 caches and go directly to DDR. This sidesteps
 * the coherency problem with HP0, which the DMA also accesses non-cached. */
static volatile uint32_t *phys_mmap(int mem_fd, uint32_t phys, size_t len,
                                     void **base, size_t *map_size)
{
    long page_size = sysconf(_SC_PAGE_SIZE);
    off_t  aligned = (off_t)(phys & ~(uint32_t)(page_size - 1));
    size_t offset  = (size_t)(phys - (uint32_t)aligned);
    *map_size = (offset + len + (size_t)page_size - 1) & ~((size_t)page_size - 1);
    *base = mmap(NULL, *map_size, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, aligned);
    if (*base == MAP_FAILED) { *base = NULL; return NULL; }
    return (volatile uint32_t *)((char *)*base + offset);
}

int dma_open(dma_dev_t *dev, uint32_t addr)
{
    char path[32];

    if (uio_find_by_addr(addr, path, sizeof(path)) < 0) {
        fprintf(stderr, "Cannot find DMA UIO at 0x%08X\n", addr);
        return -1;
    }
    dbg_printf("DMA @ 0x%08X -> %s\n", addr, path);

    dev->fd = open(path, O_RDWR);
    if (dev->fd < 0) { perror("open"); return -1; }

    dev->regs = (volatile uint32_t *)mmap(NULL, DMA_MAP_SIZE,
                    PROT_READ | PROT_WRITE, MAP_SHARED, dev->fd, 0);
    if (dev->regs == MAP_FAILED) { perror("mmap"); return -1; }

    dbg_printf("[DBG] dma_open: regs @ %p\n", (void *)dev->regs);
    return 0;
}

void dma_close(dma_dev_t *dev)
{
    munmap((void *)dev->regs, DMA_MAP_SIZE);
    close(dev->fd);
}

void dma_reset(dma_dev_t *dev)
{
    dev->regs[DMA_MM2S_DMACR / 4] = DMA_CR_RESET;
    for (int i = 0; i < 1000 && (dev->regs[DMA_MM2S_DMACR / 4] & DMA_CR_RESET); i++)
        usleep(10);

    dev->regs[DMA_S2MM_DMACR / 4] = DMA_CR_RESET;
    for (int i = 0; i < 1000 && (dev->regs[DMA_S2MM_DMACR / 4] & DMA_CR_RESET); i++)
        usleep(10);

    dbg_printf("[DBG] dma_reset: done\n");
}

void dma_print_status(dma_dev_t *dev, const char *label)
{
    printf("--- %s ---\n", label);
    printf("  MM2S SR: 0x%08X\n", dev->regs[DMA_MM2S_DMASR / 4]);
    printf("  S2MM SR: 0x%08X\n", dev->regs[DMA_S2MM_DMASR / 4]);
    fflush(stdout);
}

static int wait_idle(volatile uint32_t *sr_reg, uint32_t timeout_ms,
                     const char *name)
{
    for (uint32_t ms = 0; ms < timeout_ms; ms++) {
        uint32_t sr = *sr_reg;
        if (sr & DMA_SR_IDLE)   return 0;
        if (sr & DMA_SR_HALTED) {
            fprintf(stderr, "%s halted: SR=0x%08X\n", name, sr);
            return -1;
        }
        usleep(1000);
    }
    fprintf(stderr, "%s timeout: SR=0x%08X\n", name, *sr_reg);
    return -1;
}

int dma_main(uint32_t dma_addr)
{
    dma_dev_t dma;
    uint32_t *anchor_tx  = NULL;   /* virtual alloc — forces physical page creation */
    uint32_t *anchor_rx  = NULL;
    void     *tx_base    = NULL;
    void     *rx_base    = NULL;
    size_t    tx_msize   = 0;
    size_t    rx_msize   = 0;
    int       mem_fd     = -1;
    int       ret        = 0;

    printf("- dma_open\n");
    if (dma_open(&dma, dma_addr) < 0) return -1;

    printf("- dma_reset\n");
    dma_reset(&dma);
    dma_print_status(&dma, "after reset");

    const uint32_t num_words = DMA_BUF_WORDS;
    const uint32_t len_bytes = num_words * sizeof(uint32_t);

    /* Allocate and touch virtual buffers to force physical page allocation,
     * then lock them so the pages can't be swapped before virt_to_phys. */
    if (posix_memalign((void **)&anchor_tx, 64, len_bytes) != 0 ||
        posix_memalign((void **)&anchor_rx, 64, len_bytes) != 0) {
        perror("posix_memalign");
        ret = -1; goto cleanup;
    }
    memset(anchor_tx, 0, len_bytes);   /* touch → triggers page fault / COW */
    memset(anchor_rx, 0, len_bytes);

    if (mlock(anchor_tx, len_bytes) < 0 || mlock(anchor_rx, len_bytes) < 0) {
        perror("mlock");
        ret = -1; goto cleanup;
    }

    uint32_t tx_phys = virt_to_phys(anchor_tx);
    uint32_t rx_phys = virt_to_phys(anchor_rx);
    if (!tx_phys || !rx_phys) { ret = -1; goto cleanup; }

    dbg_printf("[DBG] tx_phys=0x%08X rx_phys=0x%08X len=%u\n",
               tx_phys, rx_phys, len_bytes);

    /* Open /dev/mem with O_SYNC so mmap returns a pgprot_noncached mapping.
     * Reads and writes bypass the CPU cache entirely — no coherency issue. */
    mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0) { perror("/dev/mem"); ret = -1; goto cleanup; }

    volatile uint32_t *tx_uncached = phys_mmap(mem_fd, tx_phys, len_bytes,
                                                &tx_base, &tx_msize);
    volatile uint32_t *rx_uncached = phys_mmap(mem_fd, rx_phys, len_bytes,
                                                &rx_base, &rx_msize);
    if (!tx_uncached || !rx_uncached) {
        perror("/dev/mem mmap");
        ret = -1; goto cleanup;
    }

    /* Write TX pattern directly to DDR through the non-cacheable mapping */
    printf("\nWriting %u 32-bit words via DMA loopback...\n", num_words);
    for (uint32_t i = 0; i < num_words; i++) {
        tx_uncached[i] = 0xA0000000 | i;
        printf("  tx[%u] = 0x%08X\n", i, 0xA0000000 | i);
    }

    /* Arm S2MM first — set destination and length before MM2S sends data */
    dma.regs[DMA_S2MM_DMACR / 4] = DMA_CR_RS;
    dma.regs[DMA_S2MM_DA     / 4] = rx_phys;
    dma.regs[DMA_S2MM_LENGTH / 4] = len_bytes;

    /* Trigger MM2S transfer */
    dma.regs[DMA_MM2S_DMACR / 4] = DMA_CR_RS;
    dma.regs[DMA_MM2S_SA     / 4] = tx_phys;
    dma.regs[DMA_MM2S_LENGTH / 4] = len_bytes;

    printf("\nWaiting for DMA to complete...\n");
    if (wait_idle(&dma.regs[DMA_MM2S_DMASR / 4], 1000, "MM2S") < 0) { ret = -1; goto cleanup; }
    if (wait_idle(&dma.regs[DMA_S2MM_DMASR / 4], 1000, "S2MM") < 0) { ret = -1; goto cleanup; }

    dma_print_status(&dma, "after transfer");

    /* Read RX data directly from DDR through the non-cacheable mapping */
    printf("\nReceived %u 32-bit words:\n", num_words);
    int pass = 1;
    for (uint32_t i = 0; i < num_words; i++) {
        uint32_t rx_val = rx_uncached[i];
        int match = (rx_val == (0xA0000000 | i));
        printf("  rx[%u] = 0x%08X %s\n", i, rx_val, match ? "OK" : "MISMATCH");
        if (!match) pass = 0;
    }

    printf("\nResult: %s\n", pass ? "PASS" : "FAIL");

cleanup:
    if (tx_base)  munmap(tx_base, tx_msize);
    if (rx_base)  munmap(rx_base, rx_msize);
    if (mem_fd >= 0) close(mem_fd);
    free(anchor_tx);
    free(anchor_rx);
    dma_close(&dma);
    return ret;
}
