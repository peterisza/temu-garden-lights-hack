#include "status_leds.h"

/* UART2 RX = PA5, link indicator = PA2 */
#define UART2_RX_PORT GPIOA
#define UART2_RX_PIN  GPIO_PIN_5
#define UART2_LINK_LED_PORT GPIOA
#define UART2_LINK_LED_PIN  GPIO_PIN_2

/* UART1 RX = PB7, link indicator = PA3 */
#define UART1_RX_PORT GPIOB
#define UART1_RX_PIN  GPIO_PIN_7
#define UART1_LINK_LED_PORT GPIOA
#define UART1_LINK_LED_PIN  GPIO_PIN_3

#define GREEN_LED_PORT GPIOA
#define GREEN_LED_PIN  GPIO_PIN_7
#define RED_LED_PORT   GPIOA
#define RED_LED_PIN    GPIO_PIN_6

/* 2 Hz blink: 250 ms on, 250 ms off */
#define LINK_LED_BLINK_HALF_PERIOD_MS 250U

static uint32_t g_red_pulse_until_ms = 0U;

static GPIO_PinState read_rx_line(GPIO_TypeDef *port, uint16_t pin)
{
  return HAL_GPIO_ReadPin(port, pin);
}

static void update_link_led(bool fault, GPIO_TypeDef *led_port, uint16_t led_pin,
                            GPIO_TypeDef *rx_port, uint16_t rx_pin)
{
  if (fault) {
    if (((HAL_GetTick() / LINK_LED_BLINK_HALF_PERIOD_MS) & 1U) == 0U) {
      HAL_GPIO_WritePin(led_port, led_pin, GPIO_PIN_SET);
    } else {
      HAL_GPIO_WritePin(led_port, led_pin, GPIO_PIN_RESET);
    }
    return;
  }

  if (read_rx_line(rx_port, rx_pin) == GPIO_PIN_SET) {
    HAL_GPIO_WritePin(led_port, led_pin, GPIO_PIN_SET);
  } else {
    HAL_GPIO_WritePin(led_port, led_pin, GPIO_PIN_RESET);
  }
}

void StatusLeds_Init(void)
{
  HAL_GPIO_WritePin(UART2_LINK_LED_PORT, UART2_LINK_LED_PIN, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(UART1_LINK_LED_PORT, UART1_LINK_LED_PIN, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GREEN_LED_PORT, GREEN_LED_PIN, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(RED_LED_PORT, RED_LED_PIN, GPIO_PIN_RESET);
  g_red_pulse_until_ms = 0U;
}

void StatusLeds_PulseRed(uint32_t duration_ms)
{
  const uint32_t now = HAL_GetTick();
  const uint32_t until = now + duration_ms;

  if ((int32_t)(until - g_red_pulse_until_ms) > 0) {
    g_red_pulse_until_ms = until;
  }
}

void StatusLeds_Update(bool uart1_fault, bool uart2_fault)
{
  const bool any_fault = uart1_fault || uart2_fault;

  update_link_led(uart2_fault, UART2_LINK_LED_PORT, UART2_LINK_LED_PIN,
                  UART2_RX_PORT, UART2_RX_PIN);
  update_link_led(uart1_fault, UART1_LINK_LED_PORT, UART1_LINK_LED_PIN,
                  UART1_RX_PORT, UART1_RX_PIN);

  if (any_fault) {
    HAL_GPIO_WritePin(GREEN_LED_PORT, GREEN_LED_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(RED_LED_PORT, RED_LED_PIN, GPIO_PIN_SET);
  } else {
    HAL_GPIO_WritePin(GREEN_LED_PORT, GREEN_LED_PIN, GPIO_PIN_SET);
    if ((int32_t)(HAL_GetTick() - g_red_pulse_until_ms) < 0) {
      HAL_GPIO_WritePin(RED_LED_PORT, RED_LED_PIN, GPIO_PIN_SET);
    } else {
      HAL_GPIO_WritePin(RED_LED_PORT, RED_LED_PIN, GPIO_PIN_RESET);
    }
  }
}
