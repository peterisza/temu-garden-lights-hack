# --- Projekt beállítások ---
TARGET = main
SRCS = main.c
MCU = attiny441
F_CPU = 16000000UL
PROGRAMMER = stk500v2

# Kötelező: add meg a lámpa címét fordításkor:
#   make program LAMP_ADDRESS=0
ifndef LAMP_ADDRESS
$(error LAMP_ADDRESS nincs megadva. Pelda: make program LAMP_ADDRESS=0)
endif

# --- Eszközök ---
CC = avr-gcc
OBJCOPY = avr-objcopy
AVRDUDE = avrdude -P /dev/ttyUSB0

# --- Fordítási jelzők ---
CFLAGS = -mmcu=$(MCU) -DF_CPU=$(F_CPU) -DLAMP_ADDRESS=$(LAMP_ADDRESS) -Wall -O2
# Linkeléskor kiírja a memóriarégiók használatát (túlcsordulásnál is: Used vs Region Size).
LDFLAGS = -Wl,--print-memory-usage

all: $(TARGET).hex

$(TARGET).elf: $(SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET).elf $(SRCS)

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -j .text -j .data -O ihex $(TARGET).elf $(TARGET).hex

# Programozás
flash: $(TARGET).hex
	sudo $(AVRDUDE) -c $(PROGRAMMER) -p t441 -U flash:w:$(TARGET).hex:i

set_fuses:
	sudo $(AVRDUDE) -c $(PROGRAMMER) -p t441 -U lfuse:w:0xff:m -U hfuse:w:0xd4:m

program: clean all set_fuses flash

#sudo $(AVRDUDE) -c $(PROGRAMMER) -p t441 -U lfuse:w:0x6e:m

clean:
	rm -f $(TARGET).elf $(TARGET).hex

.PHONY: all flash clean set_fuses
