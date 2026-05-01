// Attiny441

#define F_CPU 16000000UL
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
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
	DDRA |= (1 << PA5);

	DDRA |= (1 << PA2);
	DDRB |= (1 << PB2); // PB2 kimenet
	DDRA |= (1 << PA7); // PA7 kimenet
	DDRA |= (1 << PA3); // PA3 kimenet

	DDRA &= ~(1 << PA4);
    
	//PORTA |= (1 << PA4);
}

/* Timer1 overflow tick @ ~2kHz (PWM base): 0.5ms / tick, tehát 4 tick = 2ms */
static volatile uint8_t g_t1_ticks;
static volatile uint8_t g_last_rx_tick;

void pwm_phase_correct_init() {
	// 1. Lábak kimenetként (PB2, PA7, PA2, PA5)
	DDRA |= (1 << DDA7) | (1 << DDA5) | (1 << DDA2);
	DDRB |= (1 << DDB2);

	// 2. TOCP Pin Mux beállítása (Timer kimenetek lábakhoz rendelése)
	// TOCC7:OC1A, TOCC6:OC1B, TOCC4:OC2B, TOCC1:OC2A
	TOCPMSA1 = (0<<TOCC7S1) | (1<<TOCC7S0) | (0<<TOCC6S1) | (1<<TOCC6S0) | (1<<TOCC4S1) | (0<<TOCC4S0);
	TOCPMSA0 = (1<<TOCC1S1) | (0<<TOCC1S0);

	// TOCC kimenetek engedélyezése
	TOCPMCOE = (1<<TOCC7OE) | (1<<TOCC6OE) | (1<<TOCC4OE) | (1<<TOCC1OE);

	// 3. Timer1 beállítása (PB2 és PA7) - Mode 10: Phase Correct, TOP = ICR1
	TCCR1A = (1 << COM1A1) | (1 << COM1B1) | (1 << WGM11);
	TCCR1B = (1 << WGM13) | (1 << CS10); 
	ICR1 = 8000; 
	TIMSK1 |= (1 << TOIE1); /* időalap: TIMER1_OVF_vect @ ~2kHz */

	// 4. Timer2 beállítása (PA2 és PA5) - Mode 10: Phase Correct, TOP = ICR2
	TCCR2A = (1 << COM2A1) | (1 << COM2B1) | (1 << WGM21);
	TCCR2B = (1 << WGM23) | (1 << CS20);
	ICR2 = 8000; 

	// 5. Példa kitöltési tényezők (0 - 500 tartományban)
	OCR1A = 0; // red
	OCR1B = 0; // white
	OCR2A = 0; // blue
	OCR2B = 0; // green
}

#define UART_SILENCE_TICKS_2MS 4U


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

volatile int32_t red = 0, blue = 0, green = 0, white = 0, brightness = 0;
volatile int32_t red_target = 0, blue_target = 0, green_target = 0, white_target = 0, brightness_target = 0;
volatile int32_t red_step = 0, blue_step = 0, green_step = 0, white_step = 0, brightness_step = 0;


#define RED_MAX 1274 // 1274
#define BLUE_MAX 6240 // 6240
#define GREEN_MAX 6240 // 6240
#define WHITE_MAX 6240

void update_pwm(void) {
	uint32_t r = red >> 16;
	uint32_t b = blue >> 16;
	uint32_t g = green >> 16;
	uint32_t w = white >> 16;
	uint32_t sum = r + g + b + 3*w;
	uint32_t sum_target = brightness >> 8;
	sum_target *= sum_target;
	sum_target >>= 16;
	
	if(sum == 0)
		sum = 1;
	
	uint32_t red_normalized = r * sum_target / sum;
	uint32_t blue_normalized = b * sum_target / sum;
	uint32_t green_normalized = g * sum_target / sum;
	uint32_t white_normalized = w * sum_target / sum;
	
	uint16_t red_pwm = (red_normalized * RED_MAX) >> 13;
	uint16_t blue_pwm = (blue_normalized * BLUE_MAX) >> 13;
	uint16_t green_pwm = (green_normalized * GREEN_MAX) >> 13;
	uint16_t white_pwm = (white_normalized * WHITE_MAX) >> 13;
	
	if(red_pwm > RED_MAX) red_pwm = RED_MAX;
	if(blue_pwm > BLUE_MAX) blue_pwm = BLUE_MAX;
	if(green_pwm > GREEN_MAX) green_pwm = GREEN_MAX;
	if(white_pwm > WHITE_MAX) white_pwm = WHITE_MAX;
	
	OCR1A = red_pwm;
	OCR1B = white_pwm;
	OCR2A = blue_pwm;
	OCR2B = green_pwm;
}

volatile int16_t color_transition_counter = 0;

ISR(TIMER1_OVF_vect) {
	g_t1_ticks++;
	/*int16_t phase = color_transition_counter_max - color_transition_counter;
	if(color_transition_counter < phase)
		phase = color_transition_counter;
	phase = 100 - phase;
	
	soft_transition_counter++;
	if(soft_transition_counter <= phase)
		return;
	soft_transition_counter = 0;*/

	if(color_transition_counter > 0) {
		color_transition_counter--;
		red += red_step;
		blue += blue_step;
		green += green_step;
		white += white_step;
		brightness += brightness_step;
		if(color_transition_counter == 0) {
			red = red_target;
			blue = blue_target;
			green = green_target;
			white = white_target;
			brightness = brightness_target;
		}
		update_pwm();
	}
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

	red_target = red_raw << 16;
	blue_target = blue_raw << 16;
	green_target = green_raw << 16;
	white_target = white_raw << 16;
	brightness_target = norm;

	red_step = (red_target - red);
	blue_step = (blue_target - blue);
	green_step = (green_target - green);
	white_step = (white_target - white);
	brightness_step = (brightness_target - brightness);

	if(time > 2) {
		int32_t time_half = time / 2;
		int32_t time_32 = time;
		red_step = (red_step + time_half) / time_32;
		blue_step = (blue_step + time_half) / time_32;
		green_step = (green_step + time_half) / time_32;
		white_step = (white_step + time_half) / time_32;
		brightness_step = (brightness_step + time_half) / time_32;
	} else {
		red = red_target;
		blue = blue_target;
		green = green_target;
		white = white_target;
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
	pwm_phase_correct_init();
	uart_init();
	sei();

	update_pwm();

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
