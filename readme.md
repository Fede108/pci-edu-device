# QEMU EDU PCI Device Drivers

- **PCI character device driver**: Este driver implementa la funcionalidad de **cálculo de factorial** del dispositivo `edu`. Crea una interfaz de dispositivo de caracteres que permite a aplicaciones en espacio de usuario escribir un entero en el dispositivo y leer el factorial calculado como resultado, utilizando interrupciones para esperar a que el cálculo finalice.

- **RAM-backed block device driver**



## Hardware Reference

* **Vendor ID:** `0x1234`
* **Device ID:** `0x11E8`
* **Factorial Register (MMIO):** `0x08`
* **Status Register (MMIO):** `0x20`
  
https://www.qemu.org/docs/master/specs/edu.html
