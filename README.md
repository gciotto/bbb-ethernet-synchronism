# Synchronism pulses via Ethernet with two BBBs

A project to send and receive synchronism triggers via Ethernet using two BeagleBones Black and linux kernel modules.

This project consists of the transmission of synchronism triggers via a standard Ethernet network using two BeagleBone Black boards. Linux kernel modules were written for both sides of the application (server and client) and they handled both the reception and transmission of UDP packets and processing of IRQ interruptions.

Folder `kernel-modules` contains the kernel devices with two implementation approaches, with or without kernel threads. 

The folder `pru` presents an equivalent solution, but instead using userspace applications and BeagleBone's PRU unit.
