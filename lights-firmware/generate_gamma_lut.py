#!/usr/bin/env python3
import math

def generate_gamma_lut(max_out, gamma=2.2, bits=4):
    """
    max_out: A PWM maximum értéke (pl. 780 vagy 2000)
    gamma: A gamma korrekciós tényező (2.2 az általános)
    bits: A bemeneti felbontás (8 bit = 256 elem)
    """
    max_in = (2**bits) - 1
    lut = []
    
    for i in range(max_in + 1):
        # Képlet: Out = Max * (In / MaxIn) ^ Gamma
        val = int(round(max_out * math.pow(i / max_in, gamma)))
        lut.append(val)
    
    return lut

def print_c_array(lut, columns=12):
    print("const uint16_t gamma_lut[" + str(len(lut)) + "] PROGMEM = {")
    for i in range(0, len(lut), columns):
        row = lut[i:i+columns]
        row_str = ", ".join(f"{v:4}" for v in row)
        # Utolsó sornál ne legyen vessző a végén (opcionális, de szebb)
        if i + columns < len(lut):
            print(f"    {row_str},")
        else:
            print(f"    {row_str}")
    print("};")

# --- BEÁLLÍTÁSOK ---
MAX_PWM_VALUE = 8191  # Ide írd a kívánt maximumot (pl. 2000 a 2kHz-hez)
GAMMA_VALUE = 2.2     # 2.2 - 2.8 között érdemes kísérletezni

# Generálás és kiírás
lut_data = generate_gamma_lut(MAX_PWM_VALUE, GAMMA_VALUE)
print_c_array(lut_data)