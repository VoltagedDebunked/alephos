#include <stddef.h>
#include <core/drivers/rtc/rtc.h>
#include <utils/io.h>
#include <core/idt.h>
#include <core/drivers/pic.h>

static void (*rtc_callback)(void) = NULL;

// Convert BCD to binary if needed
static uint8_t bcd_to_binary(uint8_t bcd) {
    return ((bcd & 0xF0) >> 4) * 10 + (bcd & 0x0F);
}

// Convert binary to BCD if needed
static uint8_t binary_to_bcd(uint8_t binary) {
    return ((binary / 10) << 4) | (binary % 10);
}

// Read RTC register
static uint8_t read_rtc_reg(uint8_t reg) {
    outb(RTC_INDEX_PORT, reg);
    return inb(RTC_DATA_PORT);
}

// Write RTC register
static void write_rtc_reg(uint8_t reg, uint8_t value) {
    outb(RTC_INDEX_PORT, reg);
    outb(RTC_DATA_PORT, value);
}

// Check if RTC is updating
bool rtc_is_updating(void) {
    outb(RTC_INDEX_PORT, 0x0A);
    return (inb(RTC_DATA_PORT) & 0x80);
}

// Initialize RTC
void rtc_init(void) {
    uint8_t status;

    // Disable NMI
    outb(RTC_INDEX_PORT, RTC_STATUS_B | 0x80);

    // Read Status Register B
    status = read_rtc_reg(RTC_STATUS_B);

    // Set 24-hour mode and binary mode
    status |= RTC_24HR | RTC_DM_BINARY;
    write_rtc_reg(RTC_STATUS_B, status);

    // Clear any pending interrupts
    read_rtc_reg(RTC_STATUS_C);

    // Re-enable NMI
    outb(RTC_INDEX_PORT, RTC_STATUS_B);
}

// Read current time from RTC
void rtc_read_time(rtc_time_t* time) {
    // Wait until RTC is not updating
    while (rtc_is_updating());

    time->second = read_rtc_reg(RTC_SECONDS);
    time->minute = read_rtc_reg(RTC_MINUTES);
    time->hour = read_rtc_reg(RTC_HOURS);
    time->day = read_rtc_reg(RTC_DAY);
    time->month = read_rtc_reg(RTC_MONTH);
    time->year = read_rtc_reg(RTC_YEAR);
    time->weekday = read_rtc_reg(RTC_WEEKDAY);

    uint8_t status = read_rtc_reg(RTC_STATUS_B);

    // Convert from BCD if not in binary mode
    if (!(status & RTC_DM_BINARY)) {
        time->second = bcd_to_binary(time->second);
        time->minute = bcd_to_binary(time->minute);
        time->hour = bcd_to_binary(time->hour & 0x7F);
        time->day = bcd_to_binary(time->day);
        time->month = bcd_to_binary(time->month);
        time->year = bcd_to_binary(time->year);
    }

    // Convert 12-hour to 24-hour if needed
    if (!(status & RTC_24HR) && (time->hour & 0x80)) {
        time->hour = ((time->hour & 0x7F) + 12) % 24;
    }

    // Convert year to full year (assume 20xx)
    time->year += 2000;
}

// Write time to RTC
void rtc_write_time(const rtc_time_t* time) {
    uint8_t status = read_rtc_reg(RTC_STATUS_B);

    // Wait until RTC is not updating
    while (rtc_is_updating());

    // Disable interrupts during update
    write_rtc_reg(RTC_STATUS_B, status & ~0x70);

    // Convert to BCD if needed
    if (!(status & RTC_DM_BINARY)) {
        write_rtc_reg(RTC_SECONDS, binary_to_bcd(time->second));
        write_rtc_reg(RTC_MINUTES, binary_to_bcd(time->minute));
        write_rtc_reg(RTC_HOURS, binary_to_bcd(time->hour));
        write_rtc_reg(RTC_DAY, binary_to_bcd(time->day));
        write_rtc_reg(RTC_MONTH, binary_to_bcd(time->month));
        write_rtc_reg(RTC_YEAR, binary_to_bcd(time->year % 100));
    } else {
        write_rtc_reg(RTC_SECONDS, time->second);
        write_rtc_reg(RTC_MINUTES, time->minute);
        write_rtc_reg(RTC_HOURS, time->hour);
        write_rtc_reg(RTC_DAY, time->day);
        write_rtc_reg(RTC_MONTH, time->month);
        write_rtc_reg(RTC_YEAR, time->year % 100);
    }

    // Restore interrupt state
    write_rtc_reg(RTC_STATUS_B, status);
}

// Get UNIX timestamp from RTC
uint32_t rtc_get_timestamp(void) {
    rtc_time_t time;
    rtc_read_time(&time);

    // Days passed for each month (non-leap year)
    const uint16_t days_before_month[] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };

    // Calculate years since 1970
    uint32_t years_since_1970 = time.year - 1970;

    // Calculate leap years since 1970
    uint32_t leap_years = (time.year - 1969) / 4 - (time.year - 1901) / 100 + (time.year - 1601) / 400;

    // Calculate days since 1970
    uint32_t days_since_1970 = years_since_1970 * 365 + leap_years +
                              days_before_month[time.month - 1] + time.day - 1;

    // Add leap day if current year is leap and we're past February
    if (time.month > 2 && ((time.year % 4 == 0 && time.year % 100 != 0) || time.year % 400 == 0)) {
        days_since_1970++;
    }

    // Convert to seconds and add time
    return days_since_1970 * 86400 + time.hour * 3600 + time.minute * 60 + time.second;
}

// Set periodic interrupt rate (0-15)
void rtc_set_periodic_interrupt(uint8_t rate) {
    uint8_t status;

    rate &= 0x0F;  // Ensure rate is valid
    status = read_rtc_reg(RTC_STATUS_A);
    write_rtc_reg(RTC_STATUS_A, ((status & 0xF0) | rate));
}

// Enable RTC interrupts
void rtc_enable_interrupt(uint8_t mask) {
    uint8_t status = read_rtc_reg(RTC_STATUS_B);
    write_rtc_reg(RTC_STATUS_B, status | mask);
}

// Disable RTC interrupts
void rtc_disable_interrupt(uint8_t mask) {
    uint8_t status = read_rtc_reg(RTC_STATUS_B);
    write_rtc_reg(RTC_STATUS_B, status & ~mask);
}

// RTC interrupt handler
static void rtc_handler(struct interrupt_frame* frame) {
    // Read Status Register C to acknowledge interrupt
    read_rtc_reg(RTC_STATUS_C);

    // Call registered callback if any
    if (rtc_callback) {
        rtc_callback();
    }

    pic_send_eoi(8);  // End of interrupt for IRQ 8
}

// Register RTC interrupt handler
void rtc_register_handler(void (*handler)(void)) {
    rtc_callback = handler;
    register_interrupt_handler(IRQ8, rtc_handler);
    pic_clear_mask(8);  // Enable IRQ 8
}