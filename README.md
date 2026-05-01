# Temu garden lights hack

I bought 12V-24V outdoor garden lights on Temu. The label says:

- Product Name: Low Voltage LED Lights
- Model: CTY-10W
- Serial Number / Seri Numarasi : RGDC50245
- Manufacturer: Shenzhen Changtaiying Technology Co., Ltd
- Address: No. 4 Yintian Road, Bao'an District, Shenzhen City, Guangdong province, China.

Originally, the lamp had an FMD FT60E122 microcontroller in an SO-14 package with the following pinout:

| # | Function |
| :--- | :--- |
| 1 | VDD |
| 2 | Crystal1 (16 MHz) |
| 3 | Crystal2 |
| 4 | radio recv |
| 5 | Red PWM 62.8μs (max 10μs) |
| 6 | White PWM 62.8μs (max 49.2μs) |
| 7 | not connected |
| 8 | Green PWM 62.8μs (max 49.2μs) |
| 9 | not connected |
| 10 | not connected |
| 11 | Blue PWM 62.8μs (max 49.2μs) |
| 12 | test point 2 |
| 13 | test point 1 |
| 14 | GND |
