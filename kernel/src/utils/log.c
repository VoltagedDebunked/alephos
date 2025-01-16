#include <utils/log.h>
#include <core/drivers/serial/serial.h>
#include <stdarg.h>
#include <utils/str.h>

// Level prefixes
static const char* level_strings[] = {
    [LOG_LEVEL_DEBUG] = "[DEBUG] ",
    [LOG_LEVEL_INFO] = "[INFO] ",
    [LOG_LEVEL_WARN] = "[WARN] ",
    [LOG_LEVEL_ERROR] = "[ERROR] ",
    [LOG_LEVEL_FATAL] = "[FATAL] "
};

void log_init(void) {
    serial_init(COM1);
}

static void write_serial(const char* str) {
    while (*str) {
        serial_write_char(COM1, *str++);
    }
}

void log_char(log_level_t level, char c) {
    write_serial(level_strings[level]);
    serial_write_char(COM1, c);
    serial_write_char(COM1, '\n');
}

void log_string(log_level_t level, const char* str) {
    write_serial(level_strings[level]);
    write_serial(str);
    if (str[strlen(str) - 1] != '\n') {
        serial_write_char(COM1, '\n');
    }
}

static void print_num(uint64_t num, int base) {
    char buffer[65];
    char* ptr = &buffer[64];
    *ptr = '\0';

    do {
        ptr--;
        *ptr = "0123456789abcdef"[num % base];
        num /= base;
    } while (num > 0);

    write_serial(ptr);
}

void log_hex(log_level_t level, uint64_t value) {
    write_serial(level_strings[level]);
    write_serial("0x");
    print_num(value, 16);
    serial_write_char(COM1, '\n');
}

void log_printf(log_level_t level, const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);

    char* ptr = buffer;
    while (*format != '\0' && (ptr - buffer) < 1023) {
        if (*format != '%') {
            *ptr++ = *format++;
            continue;
        }

        format++;
        switch (*format) {
            case 's': {
                const char* str = va_arg(args, const char*);
                while (*str && (ptr - buffer) < 1023) {
                    *ptr++ = *str++;
                }
                break;
            }
            case 'd': {
                int num = va_arg(args, int);
                if (num < 0) {
                    *ptr++ = '-';
                    num = -num;
                }
                int temp = num;
                int digits = 1;
                while (temp /= 10) digits++;
                ptr += digits;
                char* numptr = ptr;
                *numptr-- = '\0';
                do {
                    *numptr-- = '0' + (num % 10);
                    num /= 10;
                } while (num > 0);
                break;
            }
            case 'x': {
                int num = va_arg(args, int);
                *ptr++ = '0';
                *ptr++ = 'x';
                char hex[9];
                int i = 0;
                do {
                    hex[i++] = "0123456789abcdef"[num % 16];
                    num /= 16;
                } while (num > 0);
                while (--i >= 0) {
                    *ptr++ = hex[i];
                }
                break;
            }
            case '%':
                *ptr++ = '%';
                break;
            default:
                *ptr++ = '%';
                *ptr++ = *format;
                break;
        }
        format++;
    }
    *ptr = '\0';

    va_end(args);
    log_string(level, buffer);
}