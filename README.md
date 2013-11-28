Avalon Gen2 Hasher
------------------

This project contains a circuit board, firmware and cgminer/bfgminer patch to get the new Avalon Gen2 ASICs (A3255Q48) hashing.

**Board features:**

  - Very efficient (~90%) DC/DC converter to power the ASICs
  - Adjustable voltage (0.85 - 1.25 V) to choose between efficiency and overclocking
  - USB-Interface to connect to a host running cgminer or bfgminer
  - I²C interface to chain multiple boards together
  - 100% flat surface on the bottom side to mount a usual heatsink
  - On-board temperature management
  - Fan connector with rotation speed measurement and PWM-control
  - Indicator LED to report USB-activity
  - 4-layer PCB for optimal heat/current transportation
  - Two board designs available: one for 10 and another for 16 ASICs

**cgminer/bfgminer patch features:**

  - Reporting temperature, real ASIC frequency and fan-speed back to the host
  - Control of ASIC frequency in a big range from 62.5 to 5000 MHz in smallest possible steps
  - Control of a desired temperature

**Get support:**

* [Forum thread]
*  \# [avalon2] on [freenode IRC]


  [Forum thread]: https://bitcointalk.org/index.php?topic=323175.0
  [avalon2]: irc://chat.au.freenode.net:6667/avalon2
  [freenode IRC]: http://freenode.net
