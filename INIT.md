# SeaSensor Project Brief

## Goal
Build an ESP32 (ESP32-U) based dock monitoring node that logs and displays sea level, water temperature, ambient temperature, humidity, and exposes the readings through Google Home (local fulfillment) and MQTT for Fibaro integration. The device must run a year on a ~37 Wh battery by aggressively managing power and duty cycles.

## Hardware Interfaces
- **MCU**: ESP32 with integrated ADC, I2C, Wi-Fi, and deep-sleep support.
- **Water probe**: DS18B20 1-Wire submerged temperature sensor (3-wire, parasite-power optional); measures sea temperature.
- **Ultrasonic**: JSN-SR20-Y1 dual-sensor waterproof module; supports single-sensor or dual averaging modes.
  - Mode-pads på kortet styrer signalformatet: 0 Ω = standard TRIG/ECHO (mode 0), 47 kΩ = UART auto (mode 1, 100 ms intervall), 120 kΩ = UART kommando (mode 2, trigger med 0x55), 200 kΩ = PWM auto (mode 3, pulsbredden representerer avstand), 360 kΩ = lavstrøm PWM kontrollert (mode 4, krever TRIG), 470 kΩ = bryterutgang mot fabrikkterskel (mode 5).
- **Ambient combo**: BME280 (temperature/humidity/pressure) on shared I2C (drop-in replacement for AHT20+BMP280 combo to free board space and simplify calibration).
- **Display**: 0.96" SSD1306 OLED (128x64, I2C) showing status, IP address, and readings.
- **Button**: Short press toggles display, long press (>30s) factory-resets Wi-Fi/config.
- **LED**: Visual proximity indicator; configurable target sensor set and blink cadence.
- **Power**: 37 Wh battery with voltage divider to ADC for state-of-charge estimation.

## Firmware Platform
Use ESP-IDF as the base to leverage low-power features, OTA updates, and the ESP open-source stack. Structure modules for sensors (`drivers/`), services (`services/mqtt`, `services/web`), UI (`ui/display`), power management, and configuration storage (NVS + JSON). Tuya-integrasjonen er fjernet til fordel for Google Home/åpen protokoll.

## Connectivity & UI
- Station-first Wi-Fi med captive portal fallback (AP SSID `SeaMonitor`); web-UI har separate felt for å skrive inn ønsket SSID/passord.
- For testmiljøet er default uplink satt til SSID `ROG` med passord `blomstervann`; fallback-AP `SeaMonitor-<navn>` står alltid oppe slik at enheten aldri blir utilgjengelig hvis uplinken feiler.
- Enhetsnavnet brukes også som hostname (`sea` som default), slik at nettverk som respekterer DHCP/mDNS gjerne eksponerer `sea.local` uten ekstra oppsett.
- Web UI: configure Wi-Fi credentials, sensor offsets (-10.0..10.0, 0.1 step), LED behavior, ultrasonic mode (single/dual), and MQTT credentials (device name doubles as Google Home identity for the local bridge).
- Lokal Google-app: `google_local_app/` inneholder Local Home SDK-skjelett (modus 2) – bygg med `npm install && npm run bundle` og last opp i Google Home Console for privat testing.
- Web UI: Wi-Fi-seksjon
    - Felt for SSID (default tom => AP fallback)
    - Felt for passord (lagres kryptert i NVS)
    - Fallback-AP navn `SeaMonitor` når ingen gyldig uplink finnes.
- Web UI: velg måleintervall per sensorgruppe via egne «minutter»/«sekunder»-felt (default 0 min 10 sek, kan settes ned til 1 sek):
    - Batteri `(<min>)min (<sek>)sek`
    - Lufttemperatur/fuktighet/trykk `(<min>)min (<sek>)sek`
    - Sjøtemperatur + ultralydnivå `(<min>)min (<sek>)sek`
    - Wi-Fi vakt (AP + STA) `(<min>)min (<sek>)sek` (0 = alltid aktiv)
    - Web UI oppetid `(<min>)min (<sek>)sek` (0 = alltid aktiv)
  Ultralyd bruker Mode 0 (TRIG via GPIO, måler, strøm kuttes via MOSFET) ved hvert intervall.
- Web UI: Displayseksjon med feltet «Skjerm på-tid (sekunder)»; verdi 0 betyr at skjermen holdes på kontinuerlig.
- Web UI: Navn-felt som styrer lokalidentitet/hostname og default-SSID-basis (default verdi `sea`).
- Web UI: Separate knapper for «Lagre», «Lagre og restart» og «Restart» slik at vi kan lagre felt uten reboot, eller trigge en kontrollert omstart (viser tydelig ventetekst i UI).
- Button-triggered "maintain" mode wakes the AP/UI for 10 minutes when device is in hourly sleep schedule.

## Power & Duty Modes
1. **Always-On (Diagnostic) Mode**
   - Trigger: USB/bench supply present during bring-up or explicit web command.
   - Behaviour: Wi-Fi + Google-local bridge + MQTT stay connected, display remains powered, sensor pod stays energized for rapid iteration.
   - Use: Development, firmware updates, and field maintenance with tethered power.

2. **Duty-Cycled Field Mode (Default once schedules are relaxed)**
   - Trigger: Running on battery without maintenance request.
   - Behaviour: ESP32 wakes per scheduler, enables sensor domains just-in-time, publishes snapshot, then returns to deep sleep; Wi-Fi/Google/MQTT are off between cycles to save energy med mindre intervall=0 (alltid på).
   - Notes: Sea + air + battery tasks run at independent intervals from `config_store`. Power domains (sensor pod, display) are gated via power_manager.

3. **Maintain Mode (Button or Remote Trigger)**
   - Trigger: User button long-press or remote command (MQTT/Google EXECUTE) når vi støtter det.
   - Behaviour: Forces Wi-Fi AP (`SeaSensor`) + web UI up for 10 minutes, display forced on, scheduled measurements continue but device does not deep sleep; after timeout, falls back to Duty-Cycled mode.
   - Notes: Use for on-dock configuration, log downloads, or pairing with home Wi-Fi.

## Data & Integrations
Expose JSON payload with sea level, water temp, ambient temp, humidity, battery %, and status flags. Support MQTT topics for Fibaro (retain last status) and the Google Local Home bridge (`/api/google/state` + `/api/google/homegraph`) so vi kan koble til Google Home uten skylås.

## Open Items
- Finjuster DS18B20/JSN-SR20 kalibrering (offsetfelt finnes nå, men krever reelle verdier fra feltet).
- Ferdigstille Google Home-integrasjonen (SYNC/QUERY/EXECUTE-mapping + Local Home SDK app) og koble den til familien sin Google-konto.
- Definere MQTT-topologi (TLS, tema-navn, QoS) og eksponere felt i UI.
- Legge inn faktisk strømmodus (duty-cycle + maintain-modus med knapp) utover dagens "alltid på"-kjøring.
- Dokumentere hvordan display-rotasjonen skal styres (auto vs. knapp) og hvordan LED/knapp skal kobles inn i strømplanen.

## Praktisk logg-oppsett
Når vi må ta en kjapp logg uten interaktiv TTY, bruker vi `run_monitor.sh` (ligger i prosjektroten). Skriptet starter tmux, kjører `idf.py monitor | tee monitor.log` i 20 sekunder og stopper automatisk.

```
#!/bin/bash
cd /Users/avendsora/codex/sea
. $HOME/esp-idf/export.sh
tmux new-session -d -s esp_monitor 'idf.py -p /dev/cu.SLAB_USBtoUART monitor | tee monitor.log'
sleep 20
tmux send-keys -t esp_monitor C-]
tmux capture-pane -t esp_monitor -S -10000
tmux save-buffer monitor.log
tmux kill-session -t esp_monitor
```

Kjøring:

```
cd /Users/avendsora/codex/sea
./run_monitor.sh
```

Resultatet blir `monitor.log` i prosjektmappa, klar for analyse.

## Status 2025-11-17 (single-wire test)
- Senere builds: single-wire ultralyd (TRIG=ECHO=GPIO27) nå med 30 µs puls, 400 µs startdelay, realistiske timeouts (<40 ms) og gjennomsnitt over 5 målinger (±5 cm). Pinnen settes tilbake til output etter hver måling for å unngå at den blir hengende som input. Luft: hvis BME (ID=0x60) brukes t/h/p fra 0x77; hvis BMP (ID=0x58) brukes AHT20 (0x38) for t/h og BMP for trykk. AHT20 init robust.
- Display viser glattet sjø-nivå: enkeltstående hopp >20 cm vises ikke før de er bekreftet av neste måling (innen ±5 cm), deretter lavpasses på skjermen. Publiserte verdier er urørte.
- Intervaller: 1s for batteri/air/sea for rask feilsøk. Wi-Fi/Web 0=alltid på. Wi-Fi default ROG/blomstervann, AP alltid oppe.
- Knapp: GPIO33 med intern pull-down, aktiv høy (koble til 3.3 V). Kort trykk bytter skjerm. Ingen auto-rotasjon.
- Monitor: **Etter hver flash skal `./run_monitor.sh` kjøres og loggen sjekkes** (luft/trykk/ultralyd) før du går videre.
- 2025-11-18: Google-bridge eksponerer `/api/google/state` og `/api/google/homegraph`; SYNC/QUERY/EXECUTE kjører lokalt og loggene viser hele forespørselen hvis noe feiler.

## Tonen i chatten
- Når du svarer her er det helt greit å slenge inn en emoji i ny og ne (🙌😄👍 osv.) for å holde stemningen oppe mens vi feilsøker. Emojiene må ikke ta overhånd, men et lite «pep» når ting virker (eller ikke virker) gjør debug-maratonet hyggeligere.
