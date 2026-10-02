#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

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
#define LED_ACTIVITY_PIN 19u
#define LED_ERROR_PIN 20u
#define LED_ACTIVITY_PULSE_US 100000u

#define GPIB_CTRL_MASK ((1u << GPIB_DAV_PIN) | (1u << GPIB_NRFD_PIN) | \
                        (1u << GPIB_NDAC_PIN) | (1u << GPIB_EOI_PIN) | \
                        (1u << GPIB_ATN_PIN) | (1u << GPIB_SRQ_PIN) | \
                        (1u << GPIB_IFC_PIN) | (1u << GPIB_REN_PIN) | \
                        (1u << GPIB_TE_PIN) | (1u << GPIB_PE_PIN) | \
                        (1u << GPIB_DC_PIN))
#define GPIB_TIMEOUT_MS 3000u
#define USB_LINE_SIZE 256u
#define GPIB_DEFAULT_ADDRESS 8u
#define GPIB_MAX_BINARY_LENGTH (16u * 1024u * 1024u)

static uint8_t gpib_address = GPIB_DEFAULT_ADDRESS;
static uint8_t gpib_eos = 0u;
static bool gpib_eoi_enabled = true;
static bool gpib_auto_read = false;
static uint32_t gpib_read_timeout_ms = GPIB_TIMEOUT_MS;
static const char *gpib_phase = "idle";
static const char *gpib_timeout_reason = "none";
static uint32_t gpib_timeout_gpio_state;
static uint64_t activity_led_deadline_us;

static void activity_led_pulse(void)
{
    activity_led_deadline_us = time_us_64() + LED_ACTIVITY_PULSE_US;
    gpio_put(LED_ACTIVITY_PIN, 1u);
}

static void error_led_set(bool on)
{
    gpio_put(LED_ERROR_PIN, on ? 1u : 0u);
}

static void report_adapter_error(const char *message)
{
    error_led_set(true);
    puts(message);
}

static void gpib_note_timeout(const char *reason)
{
    gpib_timeout_reason = reason;
    gpib_timeout_gpio_state = gpio_get_all();
    error_led_set(true);
}

static void gpib_output(uint pin, bool high)
{
    gpio_put(pin, high ? 1u : 0u);
    gpio_set_dir(pin, GPIO_OUT);
}

static bool gpib_wait_level(uint pin, bool high, uint32_t timeout_ms)
{
    uint64_t deadline = time_us_64() + (uint64_t)timeout_ms * 1000u;
    while ((gpio_get(pin) != 0u) != high) {
        if (time_us_64() >= deadline) {
            return false;
        }
        tight_loop_contents();
    }
    return true;
}

static void gpib_set_talker(void)
{
    gpio_put_masked(GPIB_DIO_MASK, GPIB_DIO_MASK);
    gpio_put(GPIB_DAV_PIN, 1u);
    gpio_put(GPIB_EOI_PIN, 1u);
    gpio_put(GPIB_NRFD_PIN, 0u);
    gpio_put(GPIB_NDAC_PIN, 0u);

    gpio_set_dir(GPIB_NRFD_PIN, GPIO_IN);
    gpio_set_dir(GPIB_NDAC_PIN, GPIO_IN);
    gpio_put(GPIB_TE_PIN, 1u);

    for (uint gpio = GPIB_DIO_BASE; gpio < GPIB_DIO_BASE + 8u; ++gpio) {
        gpio_set_dir(gpio, GPIO_OUT);
    }
    gpio_set_dir(GPIB_DAV_PIN, GPIO_OUT);
    gpio_set_dir(GPIB_EOI_PIN, GPIO_OUT);
}

static void gpib_set_listener(void)
{
    gpio_put(GPIB_DAV_PIN, 1u);
    gpio_put(GPIB_EOI_PIN, 1u);
    gpio_put_masked(GPIB_DIO_MASK, GPIB_DIO_MASK);
    for (uint gpio = GPIB_DIO_BASE; gpio < GPIB_DIO_BASE + 8u; ++gpio) {
        gpio_set_dir(gpio, GPIO_IN);
    }
    gpio_set_dir(GPIB_DAV_PIN, GPIO_IN);
    gpio_set_dir(GPIB_EOI_PIN, GPIO_IN);

    gpib_output(GPIB_NRFD_PIN, false);
    gpib_output(GPIB_NDAC_PIN, false);
    gpio_put(GPIB_TE_PIN, 0u);
}

static void gpib_gpio_init(void)
{
    gpio_init_mask(GPIB_DIO_MASK | GPIB_CTRL_MASK |
                   (1u << LED_ACTIVITY_PIN) | (1u << LED_ERROR_PIN));

    gpib_output(GPIB_DC_PIN, false);
    gpib_output(GPIB_PE_PIN, true);
    gpib_output(GPIB_ATN_PIN, true);
    gpib_output(GPIB_IFC_PIN, true);
    gpib_output(GPIB_REN_PIN, true);
    gpib_output(GPIB_TE_PIN, false);
    gpio_set_dir(GPIB_SRQ_PIN, GPIO_IN);
    gpio_set_dir(LED_ACTIVITY_PIN, GPIO_OUT);
    gpio_put(LED_ACTIVITY_PIN, 0u);
    gpio_set_dir(LED_ERROR_PIN, GPIO_OUT);
    gpio_put(LED_ERROR_PIN, 0u);
    gpio_set_dir(GPIB_DAV_PIN, GPIO_IN);
    gpio_set_dir(GPIB_EOI_PIN, GPIO_IN);
    for (uint gpio = GPIB_DIO_BASE; gpio < GPIB_DIO_BASE + 8u; ++gpio) {
        gpio_set_dir(gpio, GPIO_IN);
    }

    gpib_set_listener();
    gpio_put(GPIB_IFC_PIN, 0u);
    sleep_us(100u);
    gpio_put(GPIB_IFC_PIN, 1u);
}

static bool gpib_write_byte(uint8_t byte, bool eoi)
{
    if (!gpib_wait_level(GPIB_NRFD_PIN, true, GPIB_TIMEOUT_MS)) {
        gpib_note_timeout("NRFD did not release");
        return false;
    }

    gpio_put_masked(GPIB_DIO_MASK, (uint8_t)~byte);
    gpio_put(GPIB_EOI_PIN, eoi ? 0u : 1u);
    busy_wait_us(2u);
    gpio_put(GPIB_DAV_PIN, 0u);

    if (!gpib_wait_level(GPIB_NDAC_PIN, true, GPIB_TIMEOUT_MS)) {
        gpio_put(GPIB_DAV_PIN, 1u);
        gpio_put(GPIB_EOI_PIN, 1u);
        gpib_note_timeout("NDAC did not release after DAV assertion");
        return false;
    }

    gpio_put(GPIB_DAV_PIN, 1u);
    gpio_put(GPIB_EOI_PIN, 1u);
    if (!gpib_wait_level(GPIB_NDAC_PIN, false, GPIB_TIMEOUT_MS)) {
        gpib_note_timeout("NDAC did not assert after DAV release");
        return false;
    }
    activity_led_pulse();
    return true;
}

static bool gpib_read_byte(uint8_t *byte, bool *eoi)
{
    gpio_put(GPIB_NRFD_PIN, 1u);
    if (!gpib_wait_level(GPIB_DAV_PIN, false, gpib_read_timeout_ms)) {
        gpio_put(GPIB_NRFD_PIN, 0u);
        gpib_note_timeout("DAV did not assert while listening");
        return false;
    }

    gpio_put(GPIB_NRFD_PIN, 0u);
    *byte = (uint8_t)~gpio_get_all();
    *eoi = gpio_get(GPIB_EOI_PIN) == 0u;
    gpio_put(GPIB_NDAC_PIN, 1u);

    if (!gpib_wait_level(GPIB_DAV_PIN, true, gpib_read_timeout_ms)) {
        gpio_put(GPIB_NDAC_PIN, 0u);
        gpib_note_timeout("DAV did not release after accepting byte");
        return false;
    }

    gpio_put(GPIB_NDAC_PIN, 0u);
    activity_led_pulse();
    return true;
}

static bool gpib_send_commands(const uint8_t *commands, size_t count)
{
    gpio_put(GPIB_ATN_PIN, 0u);
    gpib_set_talker();
    for (size_t i = 0; i < count; ++i) {
        gpib_phase = "GPIB command bytes";
        if (!gpib_write_byte(commands[i], false)) {
            gpio_put(GPIB_ATN_PIN, 1u);
            gpib_set_listener();
            return false;
        }
    }
    gpio_put(GPIB_ATN_PIN, 1u);
    return true;
}

static bool gpib_address_for_write(void)
{
    const uint8_t commands[] = {0x3Fu, (uint8_t)(0x20u + gpib_address), 0x40u};
    return gpib_send_commands(commands, sizeof(commands));
}

static bool gpib_address_for_read(void)
{
    const uint8_t commands[] = {0x3Fu, (uint8_t)(0x40u + gpib_address), 0x20u};
    if (!gpib_send_commands(commands, sizeof(commands))) {
        return false;
    }
    gpib_set_listener();
    return true;
}

static bool gpib_read_stream(bool stop_on_eoi, int terminator)
{
    if (!gpib_address_for_read()) {
        return false;
    }

    gpib_phase = "instrument response";
    for (;;) {
        uint8_t byte;
        bool eoi;
        if (!gpib_read_byte(&byte, &eoi)) {
            return false;
        }
        putchar_raw(byte);
        if ((stop_on_eoi && eoi) || (!stop_on_eoi && byte == (uint8_t)terminator)) {
            error_led_set(false);
            return true;
        }
    }
}

static bool gpib_binary_write(uint32_t length)
{
    if (!gpib_address_for_write()) {
        return false;
    }

    uint64_t deadline = time_us_64() + (uint64_t)gpib_read_timeout_ms * 1000u;
    gpib_phase = "binary data";
    for (uint32_t i = 0; i < length; ++i) {
        int ch;
        do {
            ch = getchar_timeout_us(10000u);
            if (ch == PICO_ERROR_TIMEOUT && time_us_64() >= deadline) {
                gpib_set_listener();
                return false;
            }
        } while (ch == PICO_ERROR_TIMEOUT);

        if (!gpib_write_byte((uint8_t)ch, i + 1u == length)) {
            gpib_set_listener();
            return false;
        }
    }

    gpib_set_listener();
    error_led_set(false);
    return true;
}

static bool gpib_read_ieee_block(uint32_t maximum_length)
{
    uint8_t header[11];
    uint8_t byte;
    bool eoi;

    if (!gpib_address_for_read()) {
        gpib_set_listener();
        report_adapter_error("ERR: GPIB timeout before IEEE block");
        return false;
    }
    if (!gpib_read_byte(&byte, &eoi)) {
        gpib_set_listener();
        report_adapter_error("ERR: GPIB timeout before IEEE block");
        return false;
    }
    if (byte != '#') {
        gpib_set_listener();
        report_adapter_error("ERR: expected IEEE definite-length block");
        return false;
    }
    header[0] = byte;

    if (!gpib_read_byte(&byte, &eoi)) {
        gpib_set_listener();
        report_adapter_error("ERR: GPIB timeout in IEEE block header");
        return false;
    }
    if (byte < '1' || byte > '9') {
        gpib_set_listener();
        report_adapter_error("ERR: expected IEEE definite-length block");
        return false;
    }
    header[1] = byte;
    uint8_t length_digits = (uint8_t)(byte - '0');
    uint32_t payload_length = 0u;

    for (uint8_t i = 0; i < length_digits; ++i) {
        if (!gpib_read_byte(&byte, &eoi)) {
            gpib_set_listener();
            report_adapter_error("ERR: GPIB timeout in IEEE block length");
            return false;
        }
        if (byte < '0' || byte > '9') {
            gpib_set_listener();
            report_adapter_error("ERR: invalid IEEE block length");
            return false;
        }
        header[2u + i] = byte;
        uint32_t digit = (uint32_t)(byte - '0');
        if (payload_length > (UINT32_MAX - digit) / 10u) {
            gpib_set_listener();
            report_adapter_error("ERR: IEEE block length overflow");
            return false;
        }
        payload_length = payload_length * 10u + digit;
    }

    if (payload_length > maximum_length) {
        gpib_set_listener();
        report_adapter_error("ERR: IEEE block exceeds requested maximum");
        return false;
    }

    for (uint8_t i = 0; i < (uint8_t)(2u + length_digits); ++i) {
        putchar_raw(header[i]);
    }

    for (uint32_t i = 0; i < payload_length; ++i) {
        if (!gpib_read_byte(&byte, &eoi)) {
            gpib_set_listener();
            return false;
        }
        putchar_raw(byte);
    }

    if (!eoi) {
        for (uint8_t trailing = 0; trailing < 8u && !eoi; ++trailing) {
            if (!gpib_read_byte(&byte, &eoi)) {
                gpib_set_listener();
                return false;
            }
        }
        if (!eoi) {
            gpib_set_listener();
            return false;
        }
    }

    gpib_set_listener();
    error_led_set(false);
    return true;
}

static bool gpib_write_text(const char *text)
{
    size_t length = strlen(text);
    size_t eos_length = gpib_eos == 0u ? 2u : (gpib_eos == 3u ? 0u : 1u);
    size_t total = length + eos_length;
    if (total == 0u || !gpib_address_for_write()) {
        gpib_set_listener();
        return total == 0u;
    }

    gpib_phase = "SCPI data";
    for (size_t i = 0; i < total; ++i) {
        uint8_t byte;
        if (i < length) {
            byte = (uint8_t)text[i];
        } else if (gpib_eos == 0u) {
            byte = i == length ? '\r' : '\n';
        } else {
            byte = gpib_eos == 1u ? '\r' : '\n';
        }
        if (!gpib_write_byte(byte, gpib_eoi_enabled && i + 1u == total)) {
            gpib_set_listener();
            return false;
        }
    }

    gpib_set_listener();
    error_led_set(false);
    return true;
}

static bool gpib_serial_poll(uint8_t address, uint8_t *status)
{
    const uint8_t commands[] = {0x3Fu, 0x18u, (uint8_t)(0x40u + address), 0x20u};
    if (!gpib_send_commands(commands, sizeof(commands))) {
        return false;
    }

    gpib_set_listener();
    bool eoi;
    if (!gpib_read_byte(status, &eoi)) {
        return false;
    }

    const uint8_t finish[] = {0x19u, 0x5Fu};
    if (!gpib_send_commands(finish, sizeof(finish))) {
        return false;
    }
    error_led_set(false);
    return true;
}

static bool parse_uint(const char *text, uint32_t maximum, uint32_t *value)
{
    char *end;
    unsigned long parsed = strtoul(text, &end, 10);
    if (text == end || *end != '\0' || parsed > maximum) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

static void report_bus_error(void)
{
    error_led_set(true);
    printf("ERR: GPIB timeout during %s: %s (GPIO=0x%08lx)\r\n",
           gpib_phase, gpib_timeout_reason,
           (unsigned long)gpib_timeout_gpio_state);
    gpib_set_listener();
}

static void handle_adapter_command(char *line)
{
    char *command = strtok(line, " \t");
    char *argument = strtok(NULL, " \t");
    uint32_t value;

    if (command == NULL) {
        return;
    }
    if (strcmp(command, "++ver") == 0) {
        puts("BusLink RP2350 GPIB adapter 0.1");
    } else if (strcmp(command, "++addr") == 0) {
        if (argument == NULL) {
            printf("%u\r\n", gpib_address);
        } else if (parse_uint(argument, 30u, &value)) {
            gpib_address = (uint8_t)value;
        } else {
            report_adapter_error("ERR: address must be 0-30");
        }
    } else if (strcmp(command, "++auto") == 0) {
        if (argument == NULL) {
            printf("%u\r\n", gpib_auto_read ? 1u : 0u);
        } else if (parse_uint(argument, 1u, &value)) {
            gpib_auto_read = value != 0u;
        } else {
            report_adapter_error("ERR: auto must be 0 or 1");
        }
    } else if (strcmp(command, "++eoi") == 0) {
        if (argument == NULL) {
            printf("%u\r\n", gpib_eoi_enabled ? 1u : 0u);
        } else if (parse_uint(argument, 1u, &value)) {
            gpib_eoi_enabled = value != 0u;
        } else {
            report_adapter_error("ERR: eoi must be 0 or 1");
        }
    } else if (strcmp(command, "++eos") == 0) {
        if (argument == NULL) {
            printf("%u\r\n", gpib_eos);
        } else if (parse_uint(argument, 3u, &value)) {
            gpib_eos = (uint8_t)value;
        } else {
            report_adapter_error("ERR: eos must be 0-3");
        }
    } else if (strcmp(command, "++read_tmo_ms") == 0) {
        if (argument == NULL) {
            printf("%lu\r\n", (unsigned long)gpib_read_timeout_ms);
        } else if (parse_uint(argument, 60000u, &value) && value > 0u) {
            gpib_read_timeout_ms = value;
        } else {
            report_adapter_error("ERR: timeout must be 1-60000 ms");
        }
    } else if (strcmp(command, "++ifc") == 0) {
        gpio_put(GPIB_IFC_PIN, 0u);
        sleep_us(100u);
        gpio_put(GPIB_IFC_PIN, 1u);
    } else if (strcmp(command, "++srq") == 0) {
        printf("%u\r\n", gpio_get(GPIB_SRQ_PIN) == 0u ? 1u : 0u);
    } else if (strcmp(command, "++read") == 0) {
        bool stop_on_eoi = argument == NULL || strcmp(argument, "eoi") == 0;
        int terminator = argument == NULL ? '\n' : (unsigned char)argument[0];
        if (!gpib_read_stream(stop_on_eoi, terminator)) {
            report_bus_error();
        }
    } else if (strcmp(command, "++bin") == 0) {
        char *operation = argument;
        char *length_argument = strtok(NULL, " \t");
        if (operation == NULL || length_argument == NULL ||
            !parse_uint(length_argument, GPIB_MAX_BINARY_LENGTH, &value)) {
            report_adapter_error("ERR: use ++bin read <maxlen> or ++bin write <len>");
        } else if (strcmp(operation, "write") == 0) {
            if (!gpib_binary_write(value)) {
                report_bus_error();
            }
        } else if (strcmp(operation, "read") == 0) {
            (void)gpib_read_ieee_block(value);
        } else {
            report_adapter_error("ERR: use ++bin read <maxlen> or ++bin write <len>");
        }
    } else if (strcmp(command, "++spoll") == 0) {
        uint32_t address = gpib_address;
        uint8_t status;
        if (argument != NULL && !parse_uint(argument, 30u, &address)) {
            report_adapter_error("ERR: address must be 0-30");
        } else if (!gpib_serial_poll((uint8_t)address, &status)) {
            report_bus_error();
        } else {
            printf("%u\r\n", status);
        }
    } else if (strcmp(command, "++clr") == 0 || strcmp(command, "++loc") == 0) {
        uint8_t addressed_command = strcmp(command, "++clr") == 0 ? 0x04u : 0x01u;
        const uint8_t commands[] = {0x3Fu, (uint8_t)(0x20u + gpib_address), addressed_command};
        if (!gpib_send_commands(commands, sizeof(commands))) {
            report_bus_error();
        } else {
            gpib_set_listener();
        }
    } else if (strcmp(command, "++llo") == 0) {
        const uint8_t commands[] = {0x11u};
        if (!gpib_send_commands(commands, sizeof(commands))) {
            report_bus_error();
        } else {
            gpib_set_listener();
        }
    } else if (strcmp(command, "++mode") == 0) {
        if (argument == NULL || strcmp(argument, "1") == 0) {
            puts("1");
        } else {
            report_adapter_error("ERR: controller mode only");
        }
    } else {
        report_adapter_error("ERR: unsupported command");
    }
}

static void handle_usb_line(char *line)
{
    while (*line != '\0' && isspace((unsigned char)*line)) {
        ++line;
    }
    size_t length = strlen(line);
    while (length > 0u && isspace((unsigned char)line[length - 1u])) {
        line[--length] = '\0';
    }
    if (length == 0u) {
        return;
    }

    if (strncmp(line, "++", 2u) == 0) {
        handle_adapter_command(line);
    } else if (!gpib_write_text(line)) {
        report_bus_error();
    } else if (gpib_auto_read && strchr(line, '?') != NULL &&
               !gpib_read_stream(true, '\n')) {
        report_bus_error();
    }
}

int main(void)
{
    char line[USB_LINE_SIZE];
    size_t line_length = 0u;
    bool line_overflow = false;

    stdio_init_all();
    gpib_gpio_init();

    while (true) {
        if (activity_led_deadline_us != 0u &&
            time_us_64() >= activity_led_deadline_us) {
            gpio_put(LED_ACTIVITY_PIN, 0u);
            activity_led_deadline_us = 0u;
        }

        int ch = getchar_timeout_us(0u);
        if (ch == PICO_ERROR_TIMEOUT) {
            tight_loop_contents();
            continue;
        }
        if (ch == '\r' || ch == '\n') {
            if (line_length > 0u && !line_overflow) {
                line[line_length] = '\0';
                handle_usb_line(line);
            } else if (line_overflow) {
                report_adapter_error("ERR: command too long");
            }
            line_length = 0u;
            line_overflow = false;
        } else if (!line_overflow) {
            if (line_length + 1u < sizeof(line)) {
                line[line_length++] = (char)ch;
            } else {
                line_overflow = true;
            }
        }
    }
}
