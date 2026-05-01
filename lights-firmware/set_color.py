#!/usr/bin/env python3
import argparse
import hid
from typing import List


def _build_crc8_atm_table() -> List[int]:
	# CRC-8/ATM: poly=0x07, init=0x00, MSB-first
	table = []
	for n in range(256):
		c = n
		for _ in range(8):
			if c & 0x80:
				c = ((c << 1) ^ 0x07) & 0xFF
			else:
				c = (c << 1) & 0xFF
		table.append(c)
	return table


CRC8_ATM_TABLE = _build_crc8_atm_table()


def crc8_atm(data: bytes) -> int:
	crc = 0
	for b in data:
		crc = CRC8_ATM_TABLE[crc ^ b]
	return crc


def clamp(v: int, lo: int, hi: int) -> int:
	return max(lo, min(hi, v))


def build_frame_4bit(
	*,
	length: int,
	addr: int,
	r4: int,
	g4: int,
	b4: int,
	w4: int,
	brightness: int,
	time_ms: int,
) -> bytes:
	"""
	AVR protokoll (main_byte 0..31):
	- length = main_byte + 1, és 2 bájt / lámpa
	- payload (2*length bytes): lámpánként:
	    byte0: low nibble = R (0..15), high nibble = G (0..15)
	    byte1: low nibble = B (0..15), high nibble = W (0..15)
	- trailer: [time_hi][time_lo][brightness][crc]
	"""
	length = clamp(length, 1, 31)  # length < 32
	addr = clamp(addr, 0, length - 1)
	r4 = clamp(r4, 0, 15)
	g4 = clamp(g4, 0, 15)
	b4 = clamp(b4, 0, 15)
	w4 = clamp(w4, 0, 15)
	brightness = clamp(brightness, 0, 255)
	time_ms = clamp(time_ms, 0, 65535)

	payload = bytearray([0x00] * (length * 2))
	off = addr * 2
	payload[off] = (r4 & 0x0F) | ((g4 & 0x0F) << 4)
	payload[off + 1] = (b4 & 0x0F) | ((w4 & 0x0F) << 4)

	main_byte = (length - 1)  # 0..30
	time_hi = (time_ms >> 8) & 0xFF
	time_lo = time_ms & 0xFF
	frame_wo_crc = bytes([main_byte]) + bytes(payload) + bytes([time_hi, time_lo, brightness])
	return frame_wo_crc + bytes([crc8_atm(frame_wo_crc)])


def build_frame_packed_2bit(
	*,
	length: int,
	addr: int,
	r2: int,
	g2: int,
	b2: int,
	w2: int,
	brightness: int,
	time_ms: int,
) -> bytes:
	"""
	AVR protokoll (main_byte 32..63):
	- main_byte = 32 + (length-1)
	- payload: length darab byte, mindegyikben R,G,B,W 2 biten (LSB->MSB sorrend):
	    bits 1:0 = R, bits 3:2 = G, bits 5:4 = B, bits 7:6 = W
	- trailer: [time_hi][time_lo][brightness][crc]
	"""
	# Firmware oldalán ez is max 32 elemre van kódolva; itt a ">=32" küszöb miatt 32-re állunk.
	length = clamp(length, 32, 32)  # length >= 32 -> 32
	addr = clamp(addr, 0, length - 1)
	brightness = clamp(brightness, 0, 255)
	time_ms = clamp(time_ms, 0, 65535)

	r2 = clamp(r2, 0, 3)
	g2 = clamp(g2, 0, 3)
	b2 = clamp(b2, 0, 3)
	w2 = clamp(w2, 0, 3)

	payload = bytearray([0x00] * length)
	payload[addr] = (r2 & 0x03) | ((g2 & 0x03) << 2) | ((b2 & 0x03) << 4) | ((w2 & 0x03) << 6)

	main_byte = 32 + (length - 1)
	time_hi = (time_ms >> 8) & 0xFF
	time_lo = time_ms & 0xFF

	frame_wo_crc = bytes([main_byte]) + bytes(payload) + bytes([time_hi, time_lo, brightness])
	crc = crc8_atm(frame_wo_crc)
	return frame_wo_crc + bytes([crc])


def main() -> int:
	ap = argparse.ArgumentParser(description="Send a Stringlight color frame over USB HID (CRC-8/ATM).")
	ap.add_argument("--vid", type=lambda x: int(x, 0), default=0x0483)
	ap.add_argument("--pid", type=lambda x: int(x, 0), default=0x5711)
	ap.add_argument("--mode", choices=["auto", "4bit", "2bit"], default="auto")
	ap.add_argument("--addr", type=int, default=0, help="bus address index (0..length-1)")
	ap.add_argument("--length", type=int, required=True, help="number of nodes on bus (4bit: <32, 2bit: >=32)")
	ap.add_argument("--r", type=int, required=True, help="red   (4bit: 0..15, 2bit: 0..3)")
	ap.add_argument("--g", type=int, required=True, help="green (4bit: 0..15, 2bit: 0..3)")
	ap.add_argument("--b", type=int, required=True, help="blue  (4bit: 0..15, 2bit: 0..3)")
	ap.add_argument("--w", type=int, required=True, help="white (4bit: 0..15, 2bit: 0..3)")
	ap.add_argument("--brightness", type=int, default=255, help="0..255")
	ap.add_argument("--time", type=int, default=0, help="transition time (0..65535), unit: ms (as interpreted by firmware)")
	args = ap.parse_args()

	if args.mode == "auto":
		mode = "4bit" if args.length < 32 else "2bit"
	else:
		mode = args.mode

	if mode == "4bit":
		frame = build_frame_4bit(
			length=args.length,
			addr=args.addr,
			r4=args.r,
			g4=args.g,
			b4=args.b,
			w4=args.w,
			brightness=args.brightness,
			time_ms=args.time,
		)
	else:
		frame = build_frame_packed_2bit(
			length=args.length,
			addr=args.addr,
			r2=args.r,
			g2=args.g,
			b2=args.b,
			w2=args.w,
			brightness=args.brightness,
			time_ms=args.time,
		)

	dev = hid.device()
	dev.open(args.vid, args.pid)
	try:
		# HID report ID 0x00 + a tényleges frame bájtok (nem pad-olunk, hogy ne menjen extra 0 az UART-ra)
		out = bytes([0x00]) + frame
		n = dev.write(out)
		print(f"sent {n} bytes: {frame.hex(' ')}")
	finally:
		dev.close()
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
