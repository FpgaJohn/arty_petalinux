
-----
Title
-----

Exercising the GPIO, FIFO, and DMA peripherals of the ArtyZ7-20

-------------------
Directory Structure
-------------------

design_1_wrapper.xsa  -  Exported from Vivado 2024.1
design_1.tcl          -  Written from Vivado using:
                         write_bd_tcl -force design_1.tcl
my_state.vi   - Custom IP required by block design


--------------------
Things to read about
--------------------
What are kernel bootargs?
 - console=ttyPS0,115200 earlycon root=/dev/mmcblk0p2 ro rootwait

Init-manager selects sysvinit, systemd  is another option

rootfs config, under Image Features -> Package management, I see string inputs
  - package-feed-uris
  - package-feed-archs

Page posts solution for libgpiod-tools
https://adaptivesupport.amd.com/s/question/0D54U00007A9gzZSAR/petalinux-20221-added-libgpiod-to-the-rootfs-with-petalinuxconfig-c-rootfs-when-i-build-the-sdk-the-library-libgpiodso-isnt-included-in-the-sysroot?language=en_US

Yocto layer
All customizations ultimately flow into:
  project-spec/meta-user/

----------------------
Source PetaLinux Tools
----------------------

```
. /tools/Xilinx/PetaLinux/2024.1/settings.sh
```

------------------------
Create PetaLinux Project
------------------------

```
petalinux-create project --template zynq --name arty_demo
```

-------------------------
Import XSA HW Description
-------------------------

```
cd arty_demo
petalinux-config --get-hw-description ../design_1_wrapper.xsa
```

* Changes are stored in:
  project-spec/configs/config

* Set the following:
  Image Packaging Configuration
    - Root filesystem type
      EXT4
    - Copy final images to tftpboot
    - Device node of SD device
      /dev/mmcblk0p2
*  Fpga Manager
    - [unchecked] Fpga Manager
*  DTG Settings
    - Kernel Bootargs
      - Auto generated bootargs (NO EDIT!)
       (should contain root=/dev/mmcblk0p2 only after you reload)

-------------
Kernel Config
-------------

```
petalinux-config -c kernel
```

* Changes are stored in:
  project-spec/meta-user/recipes-kernel/linux/linux-xlnx/user_*.cfg

* Set the following:
  Device Drivers
    - Userspace I/O Drivers
      <*> Userspace I/O platform driver with generic IRQ handling

---------------
Set Device Tree
---------------

Add nodes for all devices to bind to uio in:
  - project-spec/meta-user/recipes-bsp/device-tree/files/system-user.dtsi
```
/include/ "system-conf.dtsi"
  / {
      chosen {
          bootargs = "console=ttyPS0,115200 earlycon earlyprintk root=/dev/mmcblk0p2 ro rootwait uio_pdrv_genirq.of_id=generic-uio";
      };
  };

  &axi_gpio_0 {
      compatible = "generic-uio";
  };

  &axi_gpio_1 {
      compatible = "generic-uio";
  };

  &axi_gpio_2 {
      compatible = "generic-uio";
  };

  &axi_gpio_control {
      compatible = "generic-uio";
  };

  &axi_gpio_values {
      compatible = "generic-uio";
  };

  &axi_fifo_mm_s_1 {
      compatible = "generic-uio";
  };

  &axi_dma_0 {
      compatible = "generic-uio";
  };

  &axi_dma_1 {
      compatible = "generic-uio";
  };
```

```
petalinux-build -c device-tree
```

----------------
Configure rootfs
----------------

```
petalinux-config -c rootfs
```

* Changes are stored in: project-spec/configs/rootfs_config
* Add to project-spec/meta-user/conf/petalinuxbsp.conf
  IMAGE_INSTALL:append = " libgpiod libgpiod-tools "

* Set the following:
  Image Features
    package management <= No public feed available, have to host your own
    empty-root-password
    serial-autologin-root

  * Filesystem Packages
    - admin
      - sudo
        [] sudo
    - base
      - init-ifupdown
        [] init-ifupdown
      - iproute2
        [] iproute2
        [] iprou2-nfstat (network interface statistics)
        [] iprou2-lnstat
        [] iprou2-rtacct
        [] iprou2-ifstat
        [] iprou2-bash-completion
      - kmod
        [] kmod
        [] libkmod
        [] kmod-bash-completion
      - netbase
        [] netbase
      - shell
        - bash
          [] bash
      - tar
        [] tar
    - libs
      - libgpiod
        [] libgpiod
        [] libgpiod-dev
    - net
      - tcpdump
        [] tcpdump
  - user packages
    [] gpio-demo
    [] peekpoke

-------------------------
Build via petalinux-build
-------------------------

```
petalinux-build
```

-----------------------------
Build SDK via petalinux-build
-----------------------------

```
petalinux-build --sdk
```

If it crashes, clean
```
petalinux-build --sdk -x mrproper
```

#petalinux-config -c u-boot
#project-spec/meta-user/recipes-bsp/u-boot/config.cfg

-------------
Package
-------------

* Package for boot

```
petalinux-package --boot --fsbl images/linux/zynq_fsbl.elf --fpga ./components/plnx_workspace/device-tree/device-tree/design_1_wrapper.bit  --u-boot images/linux/u-boot.elf --force
```

```
petalinux-package boot --fsbl --fpga --u-boot --force
```

generates:
 - BOOT.BIN
 - bootgen.bif


* Generate Image

```
petalinux-package wic --bootfiles "BOOT.BIN image.ub system.dtb boot.scr" --rootfs-file ./images/linux/rootfs.tar.gz
```

-------------
Miscellaneous
-------------
petalinux-build -c device-tree
petalinux-util -p . find-xsa-bitstream


----------
Validation
----------
 ┌────────────┬──────────────────────────────────────────────────────────────┐
 │    File    │                           Contents                           │
 ├────────────┼──────────────────────────────────────────────────────────────┤
 │ BOOT.BIN   │ FSBL + FPGA bitstream + U-Boot                               │
 ├────────────┼──────────────────────────────────────────────────────────────┤
 │ image.ub   │ FIT image: Linux kernel + system.dtb (+ sometimes initramfs) │
 ├────────────┼──────────────────────────────────────────────────────────────┤
 │ system.dtb │ Also exists as a standalone file in images/linux/            │
 └────────────┴──────────────────────────────────────────────────────────────┘

BOOT.BIN --> U-Boot --> image.ub (extracts kernel and DTB)

```
dumpimage -l images/linux/image.ub
```

Extract the DTB from the FIT image (-T flat_dt = device tree type, -p 1 = second fdt node)
```
dumpimage -T flat_dt -p 1 -o extracted.dtb images/linux/image.u-boot
```

decompile
```
dtc -I dtb -O dts -o extracted.dts extracted.dtb
```

* Check if uio is listed under compatible

1 - Check for DMA proxy in
cat project-spec/meta-user/recipes-bsp/device-tree/files/system-user.dtsi

2 - Check and decompile the built DTB
dtc -I dtb -O dts -o final.dts images/linux/system.dtb

grep dma@ final.dts
grep -A5 dma@.* final.dts
  - dma@4040_0000 - axi_dma_0
  - dma@4041_0000 - axi_dma_1

grep -A5 gpio@.*\ { final.dts
grep -A20 gpio@4122.*\ { final.dts
  - gpio@4120_0000 axi_gpio_0
  - gpio@4121_0000 axi_gpio_1
  - gpio@4122_0000 axi_gpio_control
  - gpio@4123_0000 axi_gpio_values

grep -A5 axi_fifo_mm_@ final.dts
  - axi_fifo_mm_s@43c0_0000 - axi_fifo_mm_s_0
  - axi_fifo_mm_s@43c2_0000 - axi_fifo_mm_s_1


-----------
Live System
-----------
for d in /sys/class/uio/uio*; do
    echo "$(basename $d): $(cat $d/name) @ $(cat $d/maps/map0/addr)"
done

```
uio0: dma @ 0x40400000
uio1: dma @ 0x40410000
uio2: axi_fifo_mm_s @ 0x43c00000
uio3: axi_fifo_mm_s @ 0x43c10000
uio4: gpio_ctl @ 0x41220000
uio5: gpio_val @ 0x41230000
```

------------
Vitis 2024.1
------------
1 - Platform Project with xsa

linux on ps7_cortexa9
Sysroot Directory:
  /home/john/work/arty_petalinux/arty_demo/sdk/sysroots/cortexa9t2hf-neon-xilinx-linux-gnueabi
Linux Rootfs:
  /home/john/work/arty_petalinux/arty_demo/images/linux/rootfs.tar.D54U00007A9gzZSAR

2 - C Application
  Sysroot path:
  Root FS: rootfs.ext4
  Kernel Image: uImage

3 - Debug TCF Agent
  192.168.1.199

  Debug Configuration
  Application
    Remote File path
    /home/petalinux/c_hw_test.elf
    Working Directory
    /home/petalinux

==================================================================================================
==================================================================================================
OLD NOTES / ARCHIVE
-------------------
----------------------------------
Create Kernel Module for DMA Proxy
----------------------------------
petalinux-create modules --name dma-proxy --enable
 - project-spec/meta-user-recipes-modules/dma-proxy

Edit project-spec/meta-user/recipes-modules/dma-proxy/dma-proxy.bb,
  add file://dma-proxy.h

Copy example proxy files:
  cp ../dma-proxy/files/dma-proxy.c ./project-spec/meta-user/recipes-modules/dma-proxy/files
  cp ../dma-proxy/files/dma-proxy.h ./project-spec/meta-user/recipes-modules/dma-proxy/files

----------------------
Add DMA to device tree
----------------------
Edit project-spec/meta-user/recipes-bsp/device-tree/files/system-user.dtsi
 - append
<< 'EOF'
&amba_pl {
    dma_proxy: dma_proxy@0 {
        compatible = "xlnx,dma_proxy";
        dmas = <&axi_dma_0 0>, <&axi_dma_0 1>;
        dma-names = "dma_proxy_tx", "dma_proxy_rx";
    };
};

/ {
    axi_fifo_0: fifo@43c00000 {
        compatible = "generic-uio";
        reg = <0x43c00000 0x1000>;
    };
};

EOF




