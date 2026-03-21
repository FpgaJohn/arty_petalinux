# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

PetaLinux 2024.1 embedded Linux project targeting the Digilent Arty Z7-20 board (Xilinx Zynq-7000 / XC7Z020 SoC). Produces a bootable SD card image with a Linux system that talks to custom FPGA logic via AXI DMA, AXI GPIO, and AXI FIFO peripherals.

### Repository layout
- `design_1_wrapper.xsa` — hardware description exported from Vivado 2024.1
- `design_1.tcl` — Vivado block-design source (`write_bd_tcl -force`) — authoritative for peripheral addresses and HP-port wiring
- `arty_demo/` — the PetaLinux project
- `arty_demo/vitis/` — Vitis 2024.1 workspace (`plat/` platform project, `c_hw_test/` C app, `c_hw_test_system/` system project) used for cross-compiling and debugging the target-side test program via TCF
- `src/` — top-level copy of the Linux userspace test app (`main.c`, `gpio.c`, `fifos.c`, `dma.c`, `util.c`). These are kept in sync with `arty_demo/vitis/c_hw_test/src/`; when editing, update both (or symlink) — `main.c` has diverged slightly between the two trees in the past

## Build Environment

Source PetaLinux tools before any `petalinux-*` command:

```bash
. /tools/Xilinx/PetaLinux/2024.1/settings.sh
```

## Common Commands

```bash
# Full build
petalinux-build

# Build individual components
petalinux-build -c device-tree
petalinux-build -c u-boot
petalinux-build -c kernel

# Configuration menus
petalinux-config                          # system-level
petalinux-config -c rootfs
petalinux-config -c kernel
petalinux-config -c device-tree

# Import updated hardware from Vivado
petalinux-config --get-hw-description ../design_1_wrapper.xsa

# Package BOOT.BIN
petalinux-package --boot \
  --fsbl images/linux/zynq_fsbl.elf \
  --fpga ./components/plnx_workspace/device-tree/device-tree/design_1_wrapper.bit \
  --u-boot images/linux/u-boot.elf --force

# Package WIC (SD card image)
petalinux-package wic \
  --bootfiles "BOOT.BIN image.ub system.dtb boot.scr" \
  --rootfs-file ./images/linux/rootfs.tar.gz

# Build SDK
petalinux-build --sdk          # clean first with -x mrproper if it crashes

# Validate device tree after build
dtc -I dtb -O dts -o final.dts images/linux/system.dtb

# Confirm XSA/bitstream path
petalinux-util -p . find-xsa-bitstream
```

## Architecture

### Boot Sequence
1. FSBL (`zynq_fsbl.elf`) — runs from internal SRAM, programs FPGA bitstream
2. U-Boot — loaded from QSPI flash
3. Linux kernel (`linux-xlnx`) — loaded from QSPI or SD card
4. Rootfs on `/dev/mmcblk0p2` (EXT4, SD card)

Kernel bootargs: `console=ttyPS0,115200 earlycon root=/dev/mmcblk0p2 ro rootwait uio_pdrv_genirq.of_id=generic-uio`

### FPGA Peripherals (AXI-mapped)

Addresses are authoritative in `design_1.tcl` (`assign_bd_address` lines). The two DMA controllers share HP0 (`S_AXI_HP0`) into DDR; HP1–HP3 are unused.

| Peripheral        | Address    | UIO-bound | Notes |
|-------------------|------------|-----------|-------|
| axi_dma_0         | 0x40400000 | yes       | MM2S/S2MM via HP0; no SG |
| axi_dma_1         | 0x40410000 | yes       | MM2S/S2MM via HP0; no SG |
| axi_gpio_0        | 0x41200000 | yes       | RGB LED + 2-bit switches |
| axi_gpio_1        | 0x41210000 | yes       | 4 buttons + 4 LEDs |
| axi_gpio_control  | 0x41220000 | yes       | drives `my_state_0` control/value — reference as `&axi_gpio_control` in DTSI |
| axi_gpio_values   | 0x41230000 | yes       | reads `sum`/`carry` from `my_state_0` — reference as `&axi_gpio_values` in DTSI |
| axi_fifo_mm_s_0   | 0x43C00000 | yes       | AXI-Stream FIFO, TX looped back to RX |
| axi_fifo_mm_s_1   | 0x43C10000 | yes       | AXI-Stream FIFO, TX looped back to RX |

These must appear in the compiled device tree (`system.dtb`). Use `dtc` to decompile and verify after a build.

### UIO Binding Pattern
Three parts must all be in place for a peripheral to appear under `/sys/class/uio/`:
1. **Kernel config** — `CONFIG_UIO_PDRV_GENIRQ=y` (set in `recipes-kernel/linux/linux-xlnx/user_<timestamp>.cfg`)
2. **Bootarg** — `uio_pdrv_genirq.of_id=generic-uio` (set in `system-user.dtsi` chosen node)
3. **Device tree node** — `compatible = "generic-uio";` on each peripheral node in `system-user.dtsi`

DTG preserves the Vivado BD instance name as the DTS label (confirmed in `components/plnx_workspace/device-tree/device-tree/pl.dtsi`). Reference peripherals by their BD names in `system-user.dtsi` (e.g. `&axi_gpio_control`, not `&axi_gpio_2`) — guessing positional aliases will fail the DTC with "Label or path ... not found".

UIO enumeration order at runtime is not stable across kernel versions / DTB changes. Runtime code must resolve a peripheral by its base address (see `uio_find_by_addr()` in `src/util.c`) rather than hard-coding `/dev/uioN`.

### DMA cache coherency (important)

`S_AXI_HP0` is not coherent with CPU caches. `src/dma.c` handles this by:
1. Allocating anchor buffers with `posix_memalign` + `mlock` to lock physical pages
2. Translating to physical via `/proc/self/pagemap`
3. Re-mapping those same physical pages through `/dev/mem` opened with `O_SYNC` — on ARM Linux this returns a `pgprot_noncached` mapping, so CPU reads/writes bypass L1/L2 and go straight to DDR

Do **not** switch this to a plain cached mapping or `msync`/`cacheflush` loop without confirming the DMA direction matches. The uncached mapping is the coherency strategy, not a debugging aid.

### Output Artifacts
| File | Contents |
|---|---|
| `BOOT.BIN` | FSBL + FPGA bitstream + U-Boot |
| `image.ub` | FIT image: Linux kernel + system.dtb |
| `system.dtb` | Standalone compiled device tree |

Inspect a FIT image:
```bash
dumpimage -l images/linux/image.ub
# Extract and decompile the DTB from the FIT image:
dumpimage -T flat_dt -p 1 -o extracted.dtb images/linux/image.ub
dtc -I dtb -O dts -o extracted.dts extracted.dtb
```

### Key Customization Layer
All project-specific Yocto customization is in `arty_demo/project-spec/meta-user/`:
- `conf/petalinuxbsp.conf` — appends packages (libgpiod, libgpiod-tools) to rootfs
- `recipes-bsp/device-tree/files/system-user.dtsi` — custom device tree overlay
- `recipes-bsp/u-boot/files/bsp.cfg` / `platform-top.h` — U-Boot board config
- `recipes-kernel/linux/linux-xlnx/bsp.cfg` — static kernel config fragments
- `recipes-kernel/linux/linux-xlnx/user_<timestamp>.cfg` — kernel config fragments saved by `petalinux-config -c kernel`

Rootfs package selection is controlled via `arty_demo/project-spec/configs/rootfs_config`. Key packages: `libgpiod`, `gpio-demo`, `peekpoke`, `sudo`, `bash`, `kmod`, `tcpdump`, `openssh-server`. Init system is **sysvinit** (not systemd).

### Vitis cross-compile / debug
Target-side test program (`src/*.c`) is built through the Vitis workspace at `arty_demo/vitis/`, using the PetaLinux-generated sysroot at `arty_demo/sdk/sysroots/cortexa9t2hf-neon-xilinx-linux-gnueabi`. Remote debug uses the TCF agent on the board (typical IP `192.168.1.199`) with remote path `/home/petalinux/c_hw_test.elf`.

### Live System Validation
On a running board, enumerate UIO devices with their mapped addresses:

```bash
for d in /sys/class/uio/uio*; do
    echo "$(basename $d): $(cat $d/name) @ $(cat $d/maps/map0/addr)"
done
```
