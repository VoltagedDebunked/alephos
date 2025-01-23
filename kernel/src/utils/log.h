#ifndef LOG_H
#define LOG_H

#include <stdint.h>
#include <stddef.h>

// Log levels
typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_FATAL
} log_level_t;

struct log_state {
    uint32_t cursor_x;
    uint32_t cursor_y;
    uint32_t width;
    uint32_t height;
};

// Initialize logging system
void log_init(void);

// Core logging functions
void log_printf(log_level_t level, const char* format, ...);
void log_string(log_level_t level, const char* str);
void log_char(log_level_t level, char c);
void log_hex(log_level_t level, uint64_t value);

// Helper macros for different log levels
#define log_debug(...) log_printf(LOG_LEVEL_DEBUG, __VA_ARGS__)
#define log_info(...) log_printf(LOG_LEVEL_INFO, __VA_ARGS__)
#define log_warn(...) log_printf(LOG_LEVEL_WARN, __VA_ARGS__)
#define log_error(...) log_printf(LOG_LEVEL_ERROR, __VA_ARGS__)
#define log_fatal(...) log_printf(LOG_LEVEL_FATAL, __VA_ARGS__)

#endif // LOG_H