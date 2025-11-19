# Google Home Local Bridge

## Hvorfor
Tuya er ute av prosjektet. I stedet bygger vi en lokal Google Home-integrasjon som leverer data direkte fra ESP32-en over Wi-Fi. Vi følger Google Smart Home-protokollen (SYNC/QUERY/EXECUTE) og kan senere koble på Local Home SDK for å gjøre alt helt uten skyavhengighet.

## API-overblikk
Enheten eksponerer to HTTP-endepunkter som matcher Google-flows:

| Metode | URL | Beskrivelse |
|--------|-----|-------------|
| GET | `/api/google/state` | Lettvekts JSON med siste snapshot + Wi-Fi status. Greit å debugge manuelt (`curl http://sea.local/api/google/state`). |
| POST | `/api/google/homegraph` | Tar inn Google Smart Home-forespørsler (`action.devices.SYNC`, `...QUERY`, `...EXECUTE`). Returnerer payload slik Google forventer.

### SYNC
Eksempel-request:
```json
{
  "requestId": "abc",
  "inputs": [{"intent": "action.devices.SYNC"}]
}
```
Respons inneholder ett device-objekt av typen `action.devices.types.SENSOR` med sensorlisten vår og et `customData`-felt som peker tilbake til lokale endepunkt.

### QUERY
Requesten inneholder en liste med `devices`. Hvis ID matcher `sea.<device_name>`, svarer vi med:
- `temperatureAmbientCelsius`
- `humidityAmbientPercent`
- `on` (speiler om automasjonen er aktiv)
- `customState` med vann-/luftdata, trykk og batteri

### EXECUTE
Følgende kommandoer håndteres lokalt:
- `action.devices.commands.OnOff`: Toggler "automation"-flagget (vi bruker dette senere til å trigge maintain-modus eller slå av publisering).
- `action.devices.commands.Reboot`: Returnerer umiddelbart og planlegger en kontrollert reboot etter at svaret er sendt.

Alle andre kommandoer gir `functionNotSupported`.

## Plan for full Google Home-støtte
1. **Agent/Project**: Logg inn på [Google Home Developer Console](https://console.home.google.com/) og opprett et nytt Smart Home-prosjekt (type *Smart Home* med Local Home aktivert).
2. **Traits & sync schema**: Når Google spør etter metadata, pek på `action.devices.types.SENSOR` + `SensorState` og `OnOff` trait slik koden gjør. Dette gir oss temperatur/fukt + et logisk av/på-flagg.
3. **Local Discovery**: Google sin Local Home SDK krever discovery via mDNS, UDP broadcast eller ble-advertising. Vi har allerede mDNS på `sea.local` (service `_http._tcp`). Når Local Home web-app kjører i Google Home-appen, lar vi den gjøre HTTP-kall mot `http://sea.local/api/google/homegraph`.
4. **Cloud fallback**: Dersom Local Execution ikke når enheten kan vi legge til en enkel Cloud Function som proxier samme endepunkt. Det er frivillig så lenge lokal variant virker hos far.
5. **Auth**: Local Home krever normalt ikke ekstra auth, men Google liker at vi verifiserer at requesten kommer fra en paret bruker. Vi kan reuse agentUserId (`sea-monitor`) + et lokalt delingspassord senere.
6. **App/Script**: Lag en minimal Local Home web-app (JS bundle) som gjør:
   - SYNC/QUERY/EXECUTE forwarding til ESP32.
   - Oppdager IP via mDNS (`sea.local`).
   - Viser meningsfulle feilmeldinger i Google Home appen hvis ESP32 er offline.
7. **Testing**: Bruk [Google Smart Home Simulator](https://developers.home.google.com/smart-home/testing) for å sende SYNC/QUERY/EXECUTE mot `curl`/ngrok før vi prøver i Google Home appen. Lokalt kan vi gjøre `curl -X POST http://sea.local/api/google/homegraph -d '{...}'` for å bekrefte svarene.

## Neste steg
- [ ] Verifiser endepunkt med `curl` for SYNC/QUERY/EXECUTE.
- [ ] Bygg Local Home web-app (TypeScript/JS) med bundler (f.eks. Rollup) som kjører direkte i Google Home app.
- [ ] Konfigurer prosjektet i Google-konsollen (agent info, trait mapping, OAuth placeholder selv om local brukes).
- [ ] Dokumenter hvordan sensor-ID (`sea.<device_name>`) korrelerer med prosjektet slik at vi vet hva Google forventer når enheten gis bort.
- [ ] Når Local Home fungerer: vurdér enkel Cloud fallback ved å deploye samme kode i Cloud Run.

## Privat Local Home-prosjekt (modus 2)
Fremgangsmåte for å gi far (og andre du inviterer) tilgang uten offentlig sertifisering:

1. **Google Cloud-prosjekt**
   - Gå til [Google Home Developer Console](https://console.home.google.com/). Opprett et nytt *Smart Home* prosjekt (ingen kostnad).
   - Notér Project ID og aktiver *Local Home SDK* under *Develop → Actions*.

2. **Agent (utkast)**
   - I *Develop → Invocation* sett navn/beskrivelse (f.eks. «SeaSensor Test»). Dette vises kun for brukerne du inviterer.
   - Under *Develop → Traits* velger du `action.devices.types.SENSOR` og legger til traitene `SensorState` + `OnOff` slik firmwaren rapporterer.

3. **Local Home-app**
   - Bygg koden i `google_local_app/` (`npm install && npm run bundle`).
   - Gå til *Develop → Local Home* og last opp zip (inneholder `app.js` + `manifest.json`).
   - Sett Discovery til *mDNS* og registrer `_http._tcp` med navn `sea.local` (Google finner ESP32 når den er på samme nett).

4. **Testing / deling**
   - Under *Test → App Sharing* legg til Google-kontoene som skal få tilgang (fars konto, din egen osv.).
   - I Google Home-appen: trykk `+` → `Sett opp enhet` → `Virker med Google` → velg agenten (ser ut som «[test] SeaSensor»).
   - Når appen lastes første gang får du lokal kontroll så lenge ESP32 er på samme LAN.

5. **Feilsøking**
   - Bruk *Smart Home Simulator* i konsollen for å sende SYNC/QUERY/EXECUTE og sjekke svaret.
   - `curl http://sea.local/api/google/state` lokalt for å verifisere at firmware leverer data.
   - Google Home-appen kan vise «Lokalt» i enhetens detaljer når Local Execution lykkes; hvis ikke faller den tilbake til cloud (som vi ikke har enda).

Dette oppsettet er gratis og kan senere oppgraderes til en offentlig agent (modus 1) ved å legge til en sky-backend, kjøre sertifisering og slå på «Production» i Google Console.

Med dette notatet vet vi hvor vi er og hva som gjenstår for at far skal få sensorene inn i Google Home uten Tuya.
