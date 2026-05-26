#ifndef STATUS_LEDS_H
#define STATUS_LEDS_H

#include "stm32c0xx_hal.h"
#include <stdbool.h>

void StatusLeds_Init(void);
void StatusLeds_Update(bool uart1_fault, bool uart2_fault);
void StatusLeds_PulseRed(uint32_t duration_ms);

#endif /* STATUS_LEDS_H */
