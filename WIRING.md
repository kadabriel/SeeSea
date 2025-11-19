# Hardware Wiring Plan

## ⭐ Nåværende breadboard-oppsett (test)
- JSN-SR20-Y1: VCC 3.3 V, GND felles. **TRIG=ECHO → GPIO27** (single-wire; firmware switcher pinnen fra output til input med pull-up). Ingen seriemotstander i testoppsettet.
- DS18B20 (vann): DQ → GPIO4 med 4.7 kΩ pull-up til 3.3 V. VDD 3.3 V, GND felles.
- I2C/OLED/BME: SDA = GPIO21, SCL = GPIO22, 3.3 V.
- Knapp: GPIO33 til **3.3 V** (firmware har pull-down og leser aktiv høy). Kort trykk bytter skjerm.
- Batteri-ADC: GPIO32 er ledig nå i single-wire-modus, men hold batteridivider koblet fra inntil vi bestemmer hvor ADC skal ligge permanent.

> Etter felt-test kan vi gå tilbake til pogo/enkeltbuss-planen under, men dagens kode forventer dedikerte TRIG/ECHO-pinner (32/27).

## Magnetic Pogo Harness (Remote Sensor Pod ↔ Main Enclosure)
Pin order assumes Pin 1 is keyed/marked on the magnetic connector. Keep total harness length ≤3 m and use shielded 4-core (26–24 AWG) cable.

| Pin | Signal    | Direction | Notes |
|-----|-----------|-----------|-------|
| 1   | +5 V      | Main → Pod | Powers JSN-SR20-Y1 and feeds local 3.3 V regulator for DS18B20. Budget ≤250 mA.
| 2   | GND       | Shared     | Common return; tie to enclosure ground plane at a single point.
| 3   | US_BUS    | Bidirectional | Single-wire ultrasonic interface. Bridge TRIG and ECHO on the JSN board through 220 Ω resistors and run the shared node to this pin. Protect the ESP32 side with a BSS138 level shifter (or a 10 kΩ/18 kΩ divider if space is tight).
| 4   | 1W_DQ     | Pod ↔ Main | DS18B20 data line. Pull up to 3.3 V (4.7 kΩ) on the main PCB. Keep this trace away from fast edges to avoid ringing.

> Install a TVS (SMBJ5.0A or similar) between Pin 1 and Pin 2 near the pogo connector to clamp ESD/lightning surges coming from the dock wiring.

## Remote Sensor Pod Wiring
- **JSN-SR20-Y1**
  - `VCC` → Pin 1 (+5 V)
  - `GND` → Pin 2 (GND)
  - `TRIG` → 220 Ω → US_BUS node
  - `ECHO` → 220 Ω → US_BUS node (join with TRIG after the resistors)
  - Place a 100 nF decoupling cap across VCC/GND at the module header.

- **DS18B20**
  - `VDD` → Output of a local 3.3 V LDO (MCP1700-33, AMS1117-3.3, etc.) fed from Pin 1.
  - `DQ` → Pin 4 (1W_DQ)
  - `GND` → Pin 2 (shared ground)
  - Add a 100 nF cap between VDD and GND at the sensor tail to stabilise parasite current spikes.

- **3.3 V LDO**
  - Input → Pin 1 (+5 V)
  - Output → DS18B20 VDD (optionally power any future I²C expander).
  - Ground → Pin 2
  - Place 1 µF input/output capacitors per regulator datasheet.

## Main Enclosure Wiring
- **ESP32 (custom PCB or devkit header)**
  - `GPIO21 (I2C SDA)` → AHT20 SDA, BMP280 SDA, SSD1306 SDA (2.2 kΩ pull-up to 3.3 V shared).
  - `GPIO22 (I2C SCL)` → AHT20 SCL, BMP280 SCL, SSD1306 SCL (2.2 kΩ pull-up to 3.3 V shared).
  - `GPIO4` → Pin 4 (1W_DQ) with 4.7 kΩ pull-up to 3.3 V, plus a 100 Ω series resistor at the MCU pad for lightning protection.
  - `GPIO27` (configurable) → Pin 3 (US_BUS) through a BSS138 bidirectional level shifter. Add 1 MΩ pull-down on the ESP32 side to force LOW during boot.
  - `GPIO5` → NPN transistor (2N3904) driving status LED anode (LED cathode to GND via 200 Ω). PWM this pin to realise distance-proportional blinking.
  - `GPIO33` → User button (active-low). Use 100 kΩ pull-up to 3.3 V and 100 nF debounce capacitor to ground. Long-press detection handled in firmware.
  - `GPIO32 (ADC1_CH4)` → Battery divider midpoint. Use 220 kΩ (high side) / 100 kΩ (low side) from battery positive to ground; add 100 nF from ADC pin to ground for noise filtering. Resulting max ≈1.31 V at 4.2 V battery.
  - `EN` pin → Tie to 3.3 V with 10 kΩ, add test pad for manual reset.

- **Power Tree**
  - Battery → protection/charging circuitry → 3.3 V buck regulator (efficiency >90%).
  - 5 V rail (for JSN pod) generated via boost or kept from charger input; isolate with a high-side switch (TPS27081A) so firmware can cut power to the pod during deep sleep.
  - Place a current-sense resistor (0.1 Ω) if you want coulomb counting later; otherwise rely on voltage-based SoC.

- **Display / Sensors**
  - SSD1306 OLED, AHT20, BMP280 all share the 3.3 V rail and I2C bus. Keep trace stubs short (<20 mm) and add 10 Ω series resistors on SDA/SCL if ringing is observed.
  - If you use a combo BME280 (temp/humidity/pressure) module instead, land it on the same bus (0x76 default) and place it in free air inside the enclosure lid for faster response.

## Optional/Reserved Signals
- `GPIO17` reserved for future ultrasonic sensor select (if JSN-SR20-Y1 exposes separate enable pins). Route this to an unpopulated pad near the pogo connector for easy retrofit.
- `GPIO26` reserved for external wake/expander.

## Open Questions
- Confirm JSN-SR20-Y1 dual-sensor pinout; if separate TRIG/ECHO pairs exist, the harness must be upgraded to 6 pins or a small microcontroller must be added in the pod.
- Verify magnetic connector current capacity at 5 V (peak draw during ultrasonic pulses can reach 150 mA).
- Decide whether additional surge protection (GDTs, MOVs) is required for long dock wiring runs.
