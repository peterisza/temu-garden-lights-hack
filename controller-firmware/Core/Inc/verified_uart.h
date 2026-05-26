#ifndef VERIFIED_UART_H
#define VERIFIED_UART_H

#include "stm32c0xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#define VERIFIED_UART_MAX_FRAME 128U
#define VERIFIED_UART_ECHO_TIMEOUT_MS 10U

typedef enum {
  VERIFIED_UART_OK = 0,
  VERIFIED_UART_BUSY,
  VERIFIED_UART_FAULT,
  VERIFIED_UART_TOO_LONG,
} VerifiedUartStatus;

typedef struct VerifiedUart {
  UART_HandleTypeDef *huart;
  uint8_t frame_buf[VERIFIED_UART_MAX_FRAME];
  uint16_t frame_len;
  uint16_t frame_index;
  uint8_t expected_byte;
  volatile uint8_t waiting_echo;
  volatile uint8_t fault;
  volatile uint8_t busy;
  volatile uint8_t frame_drop_event;
  uint32_t echo_deadline_ms;
} VerifiedUart;

void VerifiedUart_Init(VerifiedUart *vu, UART_HandleTypeDef *huart);
void VerifiedUart_Start(VerifiedUart *vu);
VerifiedUartStatus VerifiedUart_SubmitFrame(VerifiedUart *vu, const uint8_t *data,
                                            uint16_t length);
void VerifiedUart_Process(VerifiedUart *vu);
void VerifiedUart_IRQHandler(VerifiedUart *vu);
bool VerifiedUart_IsFault(const VerifiedUart *vu);
bool VerifiedUart_IsBusy(const VerifiedUart *vu);
bool VerifiedUart_ConsumeFrameDropEvent(VerifiedUart *vu);

#endif /* VERIFIED_UART_H */
