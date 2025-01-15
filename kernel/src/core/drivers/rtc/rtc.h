#ifndef RTC_H
#define RTC_H

#include <stdint.h>
#include <stdbool.h>

// RTC port addresses
#define RTC_INDEX_PORT   0x70
#define RTC_DATA_PORT    0x71

// RTC registers
#define RTC_SECONDS      0x00
#define RTC_MINUTES      0x02
#define RTC_HOURS        0x04
#define RTC_WEEKDAY      0x06
#define RTC_DAY          0x07
#define RTC_MONTH        0x08
#define RTC_YEAR         0x09
#define RTC_STATUS_A     0x0A
#define RTC_STATUS_B     0x0B
#define RTC_STATUS_C     0x0C

// RTC Status Register B flags
#define RTC_24HR         0x02    // 24-hour mode
#define RTC_DM_BINARY    0x04    // Binary mode
#define RTC_PIE         0x40    // Periodic interrupt enable
#define RTC_AIE         0x20    // Alarm interrupt enable
#define RTC_UIE         0x10    // Update interrupt enable

// Time structure
typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
    uint8_t weekday;
} rtc_time_t;

// Function declarations
void rtc_init(void);
void rtc_read_time(rtc_time_t* time);
void rtc_write_time(const rtc_time_t* time);
uint32_t rtc_get_timestamp(void);
void rtc_set_periodic_interrupt(uint8_t rate);
void rtc_enable_interrupt(uint8_t mask);
void rtc_disable_interrupt(uint8_t mask);
void rtc_register_handler(void (*handler)(void));
bool rtc_is_updating(void);

#endif // RTC_H