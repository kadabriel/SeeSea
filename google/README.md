# Google-integrasjon

Denne mappa samler alt du trenger for å kjøre Google Home-integrasjonen lokalt (modus 2).

## Oversikt
- `../docs/google_home.md` → Lang beskrivelse av API og prosjektløypa.
- `../google_local_app/` → Local Home SDK-app (TypeScript) som lastes opp i Google Home Console.
- `secrets.template` → Kopiér til `secrets.private` og fyll inn prosjekt-IDer/nøkler.

## Steg for steg (privat prosjekt)
1. **Google Home Developer Console**
   - Gå til <https://console.home.google.com/> og opprett et nytt *Smart Home*-prosjekt, noter `Project ID`.
   - Slå på *Local Home SDK* (se <https://developers.home.google.com/local-home/sdk>).
2. **Legg inn metadata**
   - Under *Develop → Invocation*: navn/beskrivelse (f.eks. «SeaSensor Test»).
   - Under *Develop → Traits*: `action.devices.types.SENSOR` + traitene `SensorState` og `OnOff`.
3. **Last opp Local Home-app**
   - `cd google_local_app && npm install && npm run bundle`.
   - Zip filene i `google_local_app/dist/` og last opp under *Develop → Local Home*.
   - Sett discovery til mDNS `sea.local` (service `_http._tcp`).
4. **Del med brukere**
   - *Test → App Sharing* (eller "Project Sharing"): legg til Google-kontoene som skal bruke enheten (f.eks. far). Dokumentasjon: <https://developers.home.google.com/cloud-to-cloud/integration/sharing>.
   - I Google Home-appen: `+` → `Sett opp enhet` → `Virker med Google` → velg test-agenten.
5. **Test**
   - Fra samme nett som ESP32: `curl http://sea.local/api/google/state`.
   - Bruk Smart Home Simulator (<https://developers.home.google.com/smart-home/testing/simulator>) for å sende SYNC/QUERY/EXECUTE og sjekk svar.

## Hemmelige nøkler
- Kopiér `secrets.template` til `secrets.private` og fyll inn (Project ID, agentUserId, osv.).
- Filen er ignorert av git og kan brukes som din egen notatblokk for nøkler.

## Videre
Når du en dag vil publisere til alle (modus 1), gjenbruker du dette prosjektet, legger på en cloud-backend og kjører Google sin sertifisering.
