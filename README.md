# SeaSensor

SeaSensor er en ESP32-basert brygge-/sjøsensor som måler

- Vanntemperatur (DS18B20)
- Sjønivå (JSN-SR20-Y1 ultralyd)
- Lufttemperatur/fuktighet/trykk (BME280/AHT20+BMP280)
- Batterispenning

Data vises på en SSD1306 OLED, web-kontrollpanel, MQTT og et lokalt Google Home-endepunkt. Målet er ett års drift på ~37 Wh batteri gjennom fleksible måleintervaller og strømstyring.

## Programvare
- ESP-IDF-basert firmware (se `main/`).
- Web-UI via `/api/*` + innebygd SPA (juster intervaller, offsets, Wi-Fi, navn).
- Google Local Home støtte: `/api/google/state` og `/api/google/homegraph`.
- Local Home SDK-app (`google_local_app/`) for å koble ESP32 inn i Google Home uten Tuya.

## Hurtigstart
```bash
. $HOME/esp-idf/export.sh
idf.py build
idf.py -p /dev/cu.SLAB_USBtoUART flash
./run_monitor.sh
```
Web-UI: `http://sea.local/` (STA) eller `http://192.168.4.1/` (AP fallback).

## Google-integrasjon
1. Følg `docs/google_home.md` for API-info og prosjektløype.
2. `cd google_local_app && npm install && npm run bundle` – last opp `dist/` i Google Home Console.
3. Del prosjektet med familiens Google-kontoer (App Sharing) og legg enheten til i Google Home-appen.
4. Lagre prosjekt-IDer/OAuth-nøkler i `google/secrets.private` (filen ignoreres av git).

## Privacy
`docs/privacy_policy.txt` beskriver personvern: sensorlagring lokalt, ingen deling utenfor brukerens nettverk.

## Lisens
Se LICENSE (eller legg til egen lisensfil).
