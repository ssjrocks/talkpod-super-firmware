# GD32F3x0 firmware library (vendored)

GigaDevice's CMSIS headers and standard peripheral library for the GD32F3x0 family, copied from
[Jamie793/A36Plus_Radio](https://github.com/Jamie793/A36Plus_Radio) (`Libraries/GD32`).
Copyright GigaDevice Semiconductor Inc., BSD 3-Clause licence (see each file's header); the CMSIS
core headers are Copyright ARM Limited.

This radio's MCU is a GD32F3x0-family part (AHB GPIO at 0x48000000, 64 KB flash, 16 KB RAM,
no FPU), built here as `GD32F350`. The startup code is our own (`startup/startup_a36.S`).
