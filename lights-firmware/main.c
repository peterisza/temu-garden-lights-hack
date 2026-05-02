// Attiny441

#define F_CPU 16000000UL
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <stdint.h>

#define S16_TO_S32_SHIFT(x) ((int32_t)(x) >> 16)
#define U8_TO_U16(x) ((uint16_t)(x))
#define U8_TO_U16_SHIFT(x) ((uint16_t)(x) << 8)

#ifndef LAMP_ADDRESS
#error "LAMP_ADDRESS nincs definialva. Forditsd igy: -DLAMP_ADDRESS=<0..255> (Makefile: LAMP_ADDRESS=...)"
#endif

static const uint8_t bus_address = (uint8_t)(LAMP_ADDRESS);

void io_init() {
	// --- PWM kimenet (PA5 / TOCC4) ---

	DDRA |= (1 << PA3); // PA3 kimenet
	DDRA &= ~(1 << PA4);

	// 1. Lábak kimenetként (PB2, PA7, PA2, PA5)
	DDRA |= (1 << DDA7) | (1 << DDA5) | (1 << DDA2);
	DDRB |= (1 << DDB2);
    
	//PORTA |= (1 << PA4);
}

/* Timer1 overflow tick: Fast PWM-nél f_ov = F_CPU / (TOP+1). A PWM_MAX úgy van választva,
   hogy a korábbi phase-correct időalap (4 tick/ms) megmaradjon. */
static volatile uint8_t g_t1_ticks;
static volatile uint8_t g_last_rx_tick;

static void timer0_init_1ms(void) {
	PRR &= ~(1 << PRTIM0);

	/* CTC: TOP = OCR0A, compare match A interrupt
	   16 MHz, prescaler 64 => 250 kHz tick => OCR0A=249 => 1 kHz (1 ms) */
	TCCR0A = (1 << WGM01);
	TCCR0B = (1 << CS01) | (1 << CS00);
	OCR0A = 249;
	TCNT0 = 0;
	TIFR0 = (1 << OCF0A);
	TIMSK0 |= (1 << OCIE0A);
}

/* TCNT0/1/2 = 0 futó órával is rendben; cli a 16 bites TCNT1/2 írás miatt kell (TEMP regiszter).
 * A TIFR flag bitek ettől nem törlődnek — ha zavar a régi beakadás, azt külön kell 1-gyel írni. */
void timers_sync_counters_zero(void) {
	uint8_t sreg = SREG;
	cli();
	TCNT0 = 0;
	TCNT1 = 0;
	TCNT2 = 0;
	SREG = sreg;
}

/* Fast PWM TOP = PWM_MAX => periódus (tick) = (PWM_MAX+1) CPU ciklus prescaler=1-nél */
#define PWM_MAX 1000U
#define PWM_DITHERING_BITS 3
#define PWM_DITHERING_FACTOR (1 << PWM_DITHERING_BITS)
#define UART_SILENCE_TICKS_2MS (4U)

volatile uint16_t red_pwm = 0, blue_pwm = 0, green_pwm = 0, white_pwm = 0;

void set_pwm_registers() {
	OCR1A = red_pwm;
	OCR1B = white_pwm;
	OCR2A = blue_pwm;
	OCR2B = green_pwm;
	TCCR1A = (TCCR1A & (uint8_t)~((1 << COM1A1) | (1 << COM1B1)))
		| (red_pwm != 0 ? (1 << COM1A1) : 0) | (white_pwm != 0 ? (1 << COM1B1) : 0);
	TCCR2A = (TCCR2A & (uint8_t)~((1 << COM2A1) | (1 << COM2B1)))
		| (blue_pwm != 0 ? (1 << COM2A1) : 0) | (green_pwm != 0 ? (1 << COM2B1) : 0);
}


void pwm_init() {
	// 2. TOCP Pin Mux beállítása (Timer kimenetek lábakhoz rendelése)
	// TOCC7:OC1A, TOCC6:OC1B, TOCC4:OC2B, TOCC1:OC2A
	TOCPMSA1 = (0<<TOCC7S1) | (1<<TOCC7S0) | (0<<TOCC6S1) | (1<<TOCC6S0) | (1<<TOCC4S1) | (0<<TOCC4S0);
	TOCPMSA0 = (1<<TOCC1S1) | (0<<TOCC1S0);

	// TOCC kimenetek engedélyezése
	TOCPMCOE = (1<<TOCC7OE) | (1<<TOCC6OE) | (1<<TOCC4OE) | (1<<TOCC1OE);

	// 3. Timer1 beállítása (PB2 és PA7) - Fast PWM, Mode 14, TOP = ICR1
	TCCR1A = (1 << COM1A1) | (1 << COM1B1) | (1 << WGM11);
	TCCR1B = (1 << WGM13) | (1 << WGM12) | (1 << CS10);
	ICR1 = PWM_MAX-1;
	TIMSK1 |= (1 << TOIE1); /* időalap: TIMER1_OVF_vect */

	// 4. Timer2 beállítása (PA2 és PA5) - Fast PWM, TOP = ICR2 (WGM2[2:0]=111)
	TCCR2A = (1 << COM2A1) | (1 << COM2B1) | (1 << WGM21) | (1 << WGM20);
	TCCR2B = (1 << WGM22) | (1 << CS20);
	ICR2 = PWM_MAX-1;

	// 5. Példa kitöltési tényezők (0 - 500 tartományban)
	set_pwm_registers();
}




#define UART_BUFFER_SIZE 70
static volatile uint8_t uart_buffer[UART_BUFFER_SIZE];
static volatile uint8_t uart_buffer_length;

/* CRC-8/ATM (poly 0x07, init 0x00, no xorout), table-driven, MSB-first */
static const uint8_t crc8_atm_table[256] PROGMEM = {
	0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15, 0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
	0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65, 0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
	0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5, 0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
	0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85, 0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
	0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2, 0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
	0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2, 0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
	0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32, 0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
	0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42, 0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
	0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C, 0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
	0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC, 0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
	0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C, 0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
	0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C, 0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
	0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B, 0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
	0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B, 0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
	0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB, 0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
	0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB, 0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3,
};

static uint8_t crc8_atm(const volatile uint8_t *data, uint8_t len) {
	uint8_t crc = 0x00;
	for (uint8_t i = 0; i < len; i++) {
		uint8_t idx = (uint8_t)(crc ^ data[i]);
		crc = pgm_read_byte(&crc8_atm_table[idx]);
	}
	return crc;
}

void uart_init(void) {
	/* USART1 RX @ PA4 (RXD1), 10000 baud, 8N1 (polling + buffer) */
	PRR &= ~(1 << PRUSART1);

	/* 16 MHz: baud = F_CPU / (16 * (UBRR+1)) => UBRR = 99 pontosan 10000 baud */
	UBRR1 = 99;
	UCSR1A = 0; /* U2X1=0 */

	/* 8N1 */
	UCSR1C = (1 << UCSZ11) | (1 << UCSZ10);
	UCSR1B = (1 << RXEN1);
}

const uint16_t gamma_lut[16] PROGMEM = {
	0,   21,   97,  237,  447,  731, 1091, 1532, 2055, 2662, 3357, 4140,
 5013, 5979, 7037, 8191
};

/* index: 0=R, 1=B, 2=G, 3=W — megegyezik a set_pwm_registers / OCR sorrenddel */
#define NUM_CHANNELS 4

volatile int32_t channels[NUM_CHANNELS];
volatile int32_t targets[NUM_CHANNELS];
volatile int32_t step[NUM_CHANNELS];
volatile uint8_t errors[NUM_CHANNELS];

volatile int32_t brightness = 0;
volatile int32_t brightness_target = 0;
volatile int32_t brightness_step = 0;

static const uint16_t channel_max[NUM_CHANNELS] = {
	160U,
	780U,
	780U,
	780U,
};

void update_pwm(void) {
	uint32_t c[NUM_CHANNELS];
	for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
		c[i] = (uint32_t)(channels[i] >> 16);
	}
	uint32_t r = c[0], b = c[1], g = c[2], w = c[3];
	uint32_t sum = r + g + b + 3 * w;
	uint32_t sum_target = brightness >> 8;
	sum_target *= sum_target;
	sum_target >>= 16;

	if (sum == 0) {
		sum = 1;
	}

	uint32_t norm[NUM_CHANNELS];
	norm[0] = r * sum_target / sum;
	norm[1] = b * sum_target / sum;
	norm[2] = g * sum_target / sum;
	norm[3] = w * sum_target / sum;

	uint16_t pwm[NUM_CHANNELS];
	for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
		pwm[i] = (uint16_t)((norm[i] * channel_max[i]) >> (13 - PWM_DITHERING_BITS));
		errors[i] += (uint8_t)(pwm[i] & (PWM_DITHERING_FACTOR - 1));
		pwm[i] >>= PWM_DITHERING_BITS;
		if (errors[i] >= PWM_DITHERING_FACTOR) {
			pwm[i]++;
			errors[i] -= PWM_DITHERING_FACTOR;
		}
		if (pwm[i] > channel_max[i]) {
			pwm[i] = channel_max[i];
		}
	}

	red_pwm = pwm[0];
	blue_pwm = pwm[1];
	green_pwm = pwm[2];
	white_pwm = pwm[3];
}

volatile int16_t color_transition_counter = 0;

ISR(TIMER1_OVF_vect) {
	set_pwm_registers();
}

ISR(TIMER0_COMPA_vect) {
	g_t1_ticks++;
	/*int16_t phase = color_transition_counter_max - color_transition_counter;
	if(color_transition_counter < phase)
		phase = color_transition_counter;
	phase = 100 - phase;
	
	soft_transition_counter++;
	if(soft_transition_counter <= phase)
		return;
	soft_transition_counter = 0;*/

	if (color_transition_counter > 0) {
		color_transition_counter--;
		for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
			channels[i] += step[i];
		}
		brightness += brightness_step;
		if (color_transition_counter == 0) {
			for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
				channels[i] = targets[i];
			}
			brightness = brightness_target;
		}
	}
	update_pwm();
}


uint16_t sqrt32(uint32_t n) {
    uint32_t root = 0;
    uint32_t bit = 1UL << 30; // A legmagasabb bitpárral kezdünk

    // Megkeressük a legnagyobb 4 hatványt, ami kisebb vagy egyenlő 'n'-nél
    while (bit > n) {
        bit >>= 2;
    }

    // Bitenkénti közelítés
    while (bit != 0) {
        if (n >= root + bit) {
            n -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }

    return (uint16_t)root;
}

void set_color_target(uint8_t r, uint8_t g, uint8_t b, uint8_t w, uint8_t brightness_factor, int16_t time)
{
	/*if(r > 15) r = 15;
	if(g > 15) g = 15;
	if(b > 15) b = 15;
	if(w > 15) w = 15;*/
	r &= 0x0F;
	g &= 0x0F;
	b &= 0x0F;
	w &= 0x0F;

	color_transition_counter = time;

	uint32_t red_raw = (uint32_t)pgm_read_word(&gamma_lut[r]);
	uint32_t blue_raw = (uint32_t)pgm_read_word(&gamma_lut[b]);
	uint32_t green_raw = (uint32_t)pgm_read_word(&gamma_lut[g]);
	uint32_t white_raw = (uint32_t)pgm_read_word(&gamma_lut[w]);

	uint32_t sum = red_raw + blue_raw + green_raw + 3 * white_raw;
	uint32_t norm = (uint32_t)sqrt32(sum << 16) * brightness_factor;

	targets[0] = (int32_t)(red_raw << 16);
	targets[1] = (int32_t)(blue_raw << 16);
	targets[2] = (int32_t)(green_raw << 16);
	targets[3] = (int32_t)(white_raw << 16);
	brightness_target = (int32_t)norm;

	step[0] = targets[0] - channels[0];
	step[1] = targets[1] - channels[1];
	step[2] = targets[2] - channels[2];
	step[3] = targets[3] - channels[3];
	brightness_step = brightness_target - brightness;

	if (time > 2) {
		int32_t time_half = time / 2;
		int32_t time_32 = time;
		for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
			step[i] = (step[i] + time_half) / time_32;
		}
		brightness_step = (brightness_step + time_half) / time_32;
	} else {
		for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
			channels[i] = targets[i];
		}
		brightness = brightness_target;
		update_pwm();
	}
}

void uart_process_buffer(void) {
	/* CRC-8: az utolsó bájt a várt CRC */
	if (uart_buffer_length < 2) {
		return;
	}

	uint8_t expected = uart_buffer[uart_buffer_length - 1];
	uint8_t computed = crc8_atm(uart_buffer, uart_buffer_length - 1);

	if (computed != expected) {
		return;
	}

	uint8_t main_byte = uart_buffer[0];
	if(main_byte <= 31) {
		uint8_t length = main_byte + 1;
		if(uart_buffer_length != length * 2 + 5)
			return;
		if(bus_address >= length)
			return;
		uint8_t offset = bus_address * 2 + 1;
		set_color_target(
			uart_buffer[offset] & 0x0F,
			uart_buffer[offset] >> 4,
			uart_buffer[offset + 1] & 0x0F,
			uart_buffer[offset + 1] >> 4,
			uart_buffer[uart_buffer_length - 2],
			(int16_t)(U8_TO_U16(uart_buffer[uart_buffer_length - 3]) + U8_TO_U16_SHIFT(uart_buffer[uart_buffer_length - 4]))
		);
	} else if(main_byte <= 63) {
		uint8_t length = main_byte - 32 + 1;
		if(uart_buffer_length != length + 5)
			return;
		if(bus_address >= length)
			return;
		uint8_t offset = bus_address + 1;
		set_color_target(
			(uart_buffer[offset] & 0x03) * 5,
			((uart_buffer[offset] >> 2) & 0x03) * 5,
			((uart_buffer[offset] >> 4) & 0x03) * 5,
			(uart_buffer[offset] >> 6) * 5,
			uart_buffer[uart_buffer_length - 2],
			(int16_t)(U8_TO_U16(uart_buffer[uart_buffer_length - 3]) + U8_TO_U16_SHIFT(uart_buffer[uart_buffer_length - 4]))
		);

	} else if(main_byte == 64) { // new bus address
		/* bus_address fixen fordításkor van beégetve -> ignore */
	} else if(main_byte == 65) { // clear bus address
		/* bus_address fixen fordításkor van beégetve -> ignore */
	}

}

int main(void) {
	io_init();

	_delay_us(2*LAMP_ADDRESS);

	pwm_init();
	timer0_init_1ms();
	uart_init();
	timers_sync_counters_zero();
	sei();

	while (1) {
		/* UART polling: ha van bájt, tedd pufferbe és frissítsd a last_rx időt */
		if (UCSR1A & (1 << RXC1)) {
			if (uart_buffer_length < UART_BUFFER_SIZE) {
				uart_buffer[uart_buffer_length] = UDR1;
				uart_buffer_length++;
			}
			g_last_rx_tick = g_t1_ticks;
		}

		/* Ha 2ms-ig nem jött új bájt és van mit feldolgozni */
		if (uart_buffer_length > 0 && (uint8_t)(g_t1_ticks - g_last_rx_tick) >= (uint8_t)UART_SILENCE_TICKS_2MS) {
			uart_process_buffer();
			uart_buffer_length = 0;
		}
    }
}
