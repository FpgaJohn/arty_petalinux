#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    int fd;
    volatile uint32_t *regs;
} uio_dev_t;


uint32_t reg_read(uio_dev_t *dev, uint32_t offset);
void reg_write(uio_dev_t *dev, uint32_t offset, uint32_t val);
int uio_open(uio_dev_t *dev, const char *path, uint32_t map_size);
void uio_close(uio_dev_t *dev, uint32_t map_size);

int uio_find_by_addr(uint32_t target_addr, char *out_path, size_t out_len);

#endif
