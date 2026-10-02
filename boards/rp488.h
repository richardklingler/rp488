#ifndef _BOARDS_RP488_H
#define _BOARDS_RP488_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)

// RP2354A = QFN-60, 30 GPIOs  → 1
// RP2354B = QFN-80, 48 GPIOs  → 0
#define PICO_RP2350A 1

// In-package flash: 2 MB, W25Q-compatible, default boot2 works
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1
#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif
pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (2 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

// 12 MHz crystal, as in the reference design
#define PICO_XOSC_STARTUP_DELAY_MULTIPLIER 64

// No default UART/LED: stdio over USB, LEDs defined in main.c
#endif
