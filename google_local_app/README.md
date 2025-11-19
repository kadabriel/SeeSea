# SeaSensor Local Home app

Dette er et minimalt skjelett for Google Local Home SDK. Appen forwarder SYNC/QUERY/EXECUTE
fra Google Home til ESP32-ens lokale API-er (`/api/google/state` og `/api/google/homegraph`).

## Innhold
- `src/index.ts`: TypeScript-kilde som bruker `@google/local-home-sdk`.
- `manifest.json`: Enkel manifestfil du laster opp i Google Home Console.
- `package.json` + `tsconfig.json`: gjør det lett å bygge med `npm run build`.

## Bygg
```
cd google_local_app
npm install
npm run build
npm run bundle   # lager dist/app.js + manifest.json
```
Zip `dist/`-innholdet (app.js + manifest.json) og last det opp i Google Home Console
> Develop > Local Home > Web App.

## Tilpasning
- `APP_ID`, `DEFAULT_HOSTNAME` osv. i `src/index.ts` må evt. endres dersom du gir enheten
  et annet navn enn `sea`.
- `manifest.json` → oppdater `appId`, `name` og beskrivelse så de matcher prosjektet ditt.
- Legg på flere traits/kommandoer i `onExecute` hvis du senere vil styre flere funksjoner.

## Testing
Etter opplasting i Google Home Console:
1. Inviter Google-kontoen(e) til prosjektet (Project Sharing).
2. Kjør Google Home-appen → + → Konfigurer → «Virker med Google» → velg test-agenten din.
3. Når Google Home-appen kjører Local Home-bundle, bruker den mDNS (`sea.local`) og
   henter data via HTTP – ESP32 må være på samme nett.
4. Bruk Smart Home Simulator eller `curl` mot `/api/google/homegraph` for å verifisere at
   firmware-siden svarer likt som Google forventer.

Dette er et startpunkt; Google krever fortsatt at prosjektet har en tilknyttet cloud-agent
f.eks. for fallback. Dokumentasjonen i `docs/google_home.md` beskriver hele løypa.
