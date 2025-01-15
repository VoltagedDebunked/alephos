#include <core/pit.h>
#include <utils/io.h>
#include <core/process.h>

#define PIT_CHANNEL0 0x40
#define PIT_CHANNEL1 0x41
#define PIT_CHANNEL2 0x42
#define PIT_COMMAND  0x43

#define PIT_MODE0 0x00  // Interrupt on terminal count
#define PIT_MODE1 0x02  // Hardware re-triggerable one-shot
#define PIT_MODE2 0x04  // Rate generator
#define PIT_MODE3 0x06  // Square wave generator
#define PIT_MODE4 0x08  // Software triggered strobe
#define PIT_MODE5 0x0A  // Hardware triggered strobe

void pit_init(void) {
    // Set channel 0 to mode 2 (rate generator)
    outb(PIT_COMMAND, 0x34);  // Channel 0, lobyte/hibyte, mode 2, binary

    // Set frequency to ~100 Hz (for system timer)
    uint16_t divisor = PIT_FREQUENCY / 100;
    outb(PIT_CHANNEL0, divisor & 0xFF);        // Low byte
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF); // High byte
    scheduler_tick();
}

void pit_wait(uint32_t milliseconds) {
    // Calculate number of cycles needed
    uint16_t cycles = PIT_FREQUENCY * milliseconds / 1000;

    // Set up channel 0 for countdown
    outb(PIT_COMMAND, 0x30);  // Channel 0, lobyte/hibyte, mode 0, binary
    outb(PIT_CHANNEL0, cycles & 0xFF);
    outb(PIT_CHANNEL0, (cycles >> 8) & 0xFF);

    // Wait for countdown to finish
    while (!(inb(PIT_CHANNEL0) & 0x80)) {
        __asm__ volatile("pause");
    }
}