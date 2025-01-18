#ifndef PIT_H
#define PIT_H

#include <stdint.h>

#define PIT_FREQUENCY 1193180

void pit_init(void);
void pit_wait(uint32_t milliseconds);

#endif