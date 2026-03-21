#include "util.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <dirent.h>


uint32_t reg_read(uio_dev_t *dev, uint32_t offset)
{
    return dev->regs[offset / 4];
}

void reg_write(uio_dev_t *dev, uint32_t offset, uint32_t val)
{
    dev->regs[offset / 4] = val;
}


int uio_open(uio_dev_t *dev, const char *path, uint32_t map_size)
{
    dev->fd = open(path, O_RDWR);
    if (dev->fd < 0) { perror("open"); return -1; }

    dev->regs = (volatile uint32_t *)mmap(NULL, map_size,
                    PROT_READ | PROT_WRITE, MAP_SHARED, dev->fd, 0);
    if (dev->regs == MAP_FAILED) { perror("mmap"); close(dev->fd); return -1; }
    return 0;
}

void uio_close(uio_dev_t *dev, uint32_t map_size)
{
    munmap((void *)dev->regs, map_size);
    close(dev->fd);
}

// Returns the /dev/uioN path for a given base address
int uio_find_by_addr(uint32_t target_addr, char *out_path, size_t out_len)
{
    struct dirent *entry;
    DIR *dp = opendir("/sys/class/uio");
    if (!dp) return -1;

    while ((entry = readdir(dp)) != NULL)
    {
        if (strncmp(entry->d_name, "uio", 3) != 0) continue;

        char addr_path[64];
        snprintf(addr_path, sizeof(addr_path),
                 "/sys/class/uio/%s/maps/map0/addr", entry->d_name);

        FILE *f = fopen(addr_path, "r");
        if (!f) continue;

        uint32_t addr;
        fscanf(f, "0x%x", &addr);
        fclose(f);

        if (addr == target_addr)
        {
            snprintf(out_path, out_len, "/dev/%s", entry->d_name);
            closedir(dp);
            return 0;
        }
    }

    closedir(dp);
    return -1;
}
