# QEMU EDU PCI Device Driver

A Linux character device driver for the QEMU `edu` educational PCI device (`1234:11e8`).

This driver specifically implements the **factorial computation** feature of the `edu` device. It creates a character device interface that allows user-space applications to write an integer to the device and read back the computed factorial, utilizing hardware interrupts to wait for the computation to finish.

## Hardware Reference

* **Vendor ID:** `0x1234`
* **Device ID:** `0x11E8`
* **Factorial Register (MMIO):** `0x08`
* **Status Register (MMIO):** `0x20` (Driver sets `0x80` to raise an interrupt upon completion)
  
https://www.qemu.org/docs/master/specs/edu.html
