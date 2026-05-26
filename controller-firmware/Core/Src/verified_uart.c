#include "verified_uart.h"

static void verified_uart_abort_frame(VerifiedUart *vu)
{
  vu->busy = 0U;
  vu->frame_len = 0U;
  vu->frame_index = 0U;
  vu->waiting_echo = 0U;
  __HAL_UART_DISABLE_IT(vu->huart, UART_IT_RXNE);
}

static void verified_uart_enter_fault(VerifiedUart *vu)
{
  vu->fault = 1U;
  verified_uart_abort_frame(vu);
}

static void verified_uart_finish_frame(VerifiedUart *vu)
{
  verified_uart_abort_frame(vu);
}

static void verified_uart_send_current_byte(VerifiedUart *vu)
{
  vu->expected_byte = vu->frame_buf[vu->frame_index];
  vu->huart->Instance->TDR = vu->expected_byte;
  vu->waiting_echo = 1U;
  vu->echo_deadline_ms = HAL_GetTick() + VERIFIED_UART_ECHO_TIMEOUT_MS;
  __HAL_UART_ENABLE_IT(vu->huart, UART_IT_RXNE);
}

void VerifiedUart_Init(VerifiedUart *vu, UART_HandleTypeDef *huart)
{
  vu->huart = huart;
  vu->frame_len = 0U;
  vu->frame_index = 0U;
  vu->expected_byte = 0U;
  vu->waiting_echo = 0U;
  vu->fault = 0U;
  vu->busy = 0U;
  vu->frame_drop_event = 0U;
  vu->echo_deadline_ms = 0U;
}

void VerifiedUart_Start(VerifiedUart *vu)
{
  USART_TypeDef *usart = vu->huart->Instance;

  while ((usart->ISR & USART_ISR_RXNE_RXFNE) != 0U) {
    (void)usart->RDR;
  }
  if ((usart->ISR & USART_ISR_ORE) != 0U) {
    __HAL_UART_CLEAR_OREFLAG(vu->huart);
  }

  __HAL_UART_DISABLE_IT(vu->huart, UART_IT_RXNE);
  __HAL_UART_DISABLE_IT(vu->huart, UART_IT_TXE);
  __HAL_UART_DISABLE_IT(vu->huart, UART_IT_TC);
}

VerifiedUartStatus VerifiedUart_SubmitFrame(VerifiedUart *vu, const uint8_t *data,
                                            uint16_t length)
{
  uint16_t i;

  if (vu->fault != 0U) {
    return VERIFIED_UART_FAULT;
  }
  if (vu->busy != 0U) {
    return VERIFIED_UART_BUSY;
  }
  if (length == 0U) {
    return VERIFIED_UART_OK;
  }
  if (length > VERIFIED_UART_MAX_FRAME) {
    return VERIFIED_UART_TOO_LONG;
  }

  for (i = 0U; i < length; ++i) {
    vu->frame_buf[i] = data[i];
  }
  vu->frame_len = length;
  vu->frame_index = 0U;
  vu->busy = 1U;

  verified_uart_send_current_byte(vu);
  return VERIFIED_UART_OK;
}

void VerifiedUart_Process(VerifiedUart *vu)
{
  if (vu->fault != 0U || vu->waiting_echo == 0U) {
    return;
  }

  if ((int32_t)(HAL_GetTick() - vu->echo_deadline_ms) >= 0) {
    verified_uart_enter_fault(vu);
  }
}

void VerifiedUart_IRQHandler(VerifiedUart *vu)
{
  USART_TypeDef *usart = vu->huart->Instance;
  uint32_t isr = usart->ISR;
  uint8_t rx;

  if ((isr & USART_ISR_ORE) != 0U) {
    __HAL_UART_CLEAR_OREFLAG(vu->huart);
  }

  if ((isr & USART_ISR_RXNE_RXFNE) == 0U) {
    return;
  }

  rx = (uint8_t)(usart->RDR & 0xFFU);
  vu->waiting_echo = 0U;
  __HAL_UART_DISABLE_IT(vu->huart, UART_IT_RXNE);

  if (vu->fault != 0U) {
    return;
  }

  if (rx != vu->expected_byte) {
    vu->frame_drop_event = 1U;
    verified_uart_abort_frame(vu);
    return;
  }

  vu->frame_index++;
  if (vu->frame_index < vu->frame_len) {
    verified_uart_send_current_byte(vu);
  } else {
    verified_uart_finish_frame(vu);
  }
}

bool VerifiedUart_IsFault(const VerifiedUart *vu)
{
  return vu->fault != 0U;
}

bool VerifiedUart_IsBusy(const VerifiedUart *vu)
{
  return vu->busy != 0U;
}

bool VerifiedUart_ConsumeFrameDropEvent(VerifiedUart *vu)
{
  if (vu->frame_drop_event == 0U) {
    return false;
  }
  vu->frame_drop_event = 0U;
  return true;
}
