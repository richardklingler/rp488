#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/time.h"

#define GPIB_DIO_BASE 0u
#define GPIB_DIO_MASK 0xFFu
#define GPIB_DAV_PIN 8u
#define GPIB_NRFD_PIN 9u
#define GPIB_NDAC_PIN 10u
#define GPIB_EOI_PIN 11u
#define GPIB_ATN_PIN 12u
#define GPIB_SRQ_PIN 13u
#define GPIB_IFC_PIN 14u
#define GPIB_REN_PIN 15u
#define GPIB_TE_PIN 16u
#define GPIB_PE_PIN 17u
#define GPIB_DC_PIN 18u

#define GPIB_CTRL_MASK ((1u << GPIB_DAV_PIN) | (1u << GPIB_NRFD_PIN) | \
                        (1u << GPIB_NDAC_PIN) | (1u << GPIB_EOI_PIN) | \
                        (1u << GPIB_ATN_PIN) | (1u << GPIB_SRQ_PIN) | \
                        (1u << GPIB_IFC_PIN) | (1u << GPIB_REN_PIN) | \
                        (1u << GPIB_TE_PIN) | (1u << GPIB_PE_PIN) | \
                        (1u << GPIB_DC_PIN))

static void gpib_gpio_init(void)
{
    gpio_init_mask(GPIB_DIO_MASK | GPIB_CTRL_MASK);

    for (uint gpio = GPIB_DIO_BASE; gpio < GPIB_DIO_BASE + 8; ++gpio) {
        gpio_set_dir(gpio, GPIO_OUT);
        gpio_put(gpio, 1);
    }

    gpio_set_dir(GPIB_DAV_PIN, GPIO_OUT);
    gpio_put(GPIB_DAV_PIN, 1);
    gpio_set_dir(GPIB_NRFD_PIN, GPIO_OUT);
    gpio_put(GPIB_NRFD_PIN, 1);
    gpio_set_dir(GPIB_NDAC_PIN, GPIO_OUT);
    gpio_put(GPIB_NDAC_PIN, 1);
    gpio_set_dir(GPIB_EOI_PIN, GPIO_OUT);
    gpio_put(GPIB_EOI_PIN, 1);
    gpio_set_dir(GPIB_ATN_PIN, GPIO_OUT);
    gpio_put(GPIB_ATN_PIN, 1);
    gpio_set_dir(GPIB_IFC_PIN, GPIO_OUT);
    gpio_put(GPIB_IFC_PIN, 1);
    gpio_set_dir(GPIB_REN_PIN, GPIO_OUT);
    gpio_put(GPIB_REN_PIN, 1);
    gpio_set_dir(GPIB_TE_PIN, GPIO_OUT);
    gpio_put(GPIB_TE_PIN, 1);
    gpio_set_dir(GPIB_PE_PIN, GPIO_OUT);
    gpio_put(GPIB_PE_PIN, 1);
    gpio_set_dir(GPIB_DC_PIN, GPIO_OUT);
    gpio_put(GPIB_DC_PIN, 1);

    gpio_set_dir(GPIB_SRQ_PIN, GPIO_IN);
    gpio_pull_down(GPIB_SRQ_PIN);
}

static void gpib_bus_idle(void)
{
    gpio_put(GPIB_ATN_PIN, 1);
    gpio_put(GPIB_REN_PIN, 1);
    gpio_put(GPIB_IFC_PIN, 0);
    sleep_us(100);
    gpio_put(GPIB_IFC_PIN, 1);
    gpio_put(GPIB_DC_PIN, 1);
    gpio_put(GPIB_TE_PIN, 1);
    gpio_put(GPIB_PE_PIN, 1);
}

static inline uint8_t gpib_invert_byte(uint8_t v)
{
    return (uint8_t)(~v);
}

static void gpib_write_byte(uint8_t byte)
{
    for (uint gpio = GPIB_DIO_BASE; gpio < GPIB_DIO_BASE + 8; ++gpio) {
        gpio_put(gpio, (gpib_invert_byte(byte) >> (gpio - GPIB_DIO_BASE)) & 1u);
    }
}

static uint8_t gpib_read_byte(void)
{
    uint8_t value = 0u;
    for (uint gpio = GPIB_DIO_BASE; gpio < GPIB_DIO_BASE + 8; ++gpio) {
        value |= ((gpio_get(gpio) & 1u) << (gpio - GPIB_DIO_BASE));
    }
    return gpib_invert_byte(value);
}

int main(void)
{
    stdio_init_all();

    gpib_gpio_init();
    gpib_bus_idle();

    while (true) {
        if (stdio_usb_connected()) {
            int ch = getchar_timeout_us(0);
            if (ch != PICO_ERROR_TIMEOUT) {
                putchar_raw((char)ch);
            }
        }

        sleep_ms(1);
    }

    return 0;
}
