# SeaSensor Privacy Policy

_Last updated: November 20, 2025_

This document describes how the SeaSensor project collects, stores, and processes information about users and connected devices. It is provided to satisfy the minimum privacy requirements for integrations published through the Google Home Developer Platform.

## 1. Data Transparency
- We only collect information that is necessary to deliver the primary SeaSensor features (sensor readings, device status, and configuration data).
- Users are informed whenever SeaSensor needs access to new categories of data. We explain what is collected, why it is collected, and how it will be used.
- SeaSensor never sells user or device data and never uses it for advertising.

## 2. Data Collection and Use
- **Data provided by users:** Wi-Fi credentials, device names, calibration offsets, schedules, and any configuration submitted via the SeaSensor web interface or companion apps.
- **Data collected automatically:** Sensor measurements (temperature, humidity, pressure, battery level, water level), diagnostics (uptime, error counters), and interaction logs (button presses, firmware version).
- **How data is used:** To display readings locally, synchronize with Google Home, deliver alerts or automations configured by the user, and improve device reliability (e.g., detecting sensor faults).
- **Sharing:** Data is shared only with the services explicitly enabled by the user (for example, Google Home/Google Cloud, MQTT brokers, or Firebase). Each service receives only the data required to deliver its functionality.

## 3. Permissions and Access
- SeaSensor requests the minimum scope of permissions from Google APIs or user accounts required to register the integration and exchange state updates.
- If optional services require additional permissions, the user must opt in explicitly.

## 4. Security
- All sensitive data is transmitted via encrypted channels (HTTPS/TLS or encrypted MQTT).
- Credentials and tokens are stored securely (for example, using ESP32 NVS encryption when available) and are never logged in plaintext.
- We follow modern security practices to protect data at rest and in transit.

## 5. Privacy Restrictions
- Data obtained from Google Home Developer Platform integrations (including Home APIs and the Home Hub Runtime) **must not** be used to train or fine-tune any artificial-intelligence model or related tool.
- User data is never sold, rented, or repurposed for advertising.

## 6. User Controls
- Users can view, update, or delete their configuration data at any time through the SeaSensor web UI or the Google Home app (where applicable).
- Users may request deletion of their cloud data by contacting the SeaSensor maintainer at privacy@seasensor.example. Requests are honored promptly, and confirmations are issued once data has been removed.

## 7. Retention and Deletion
- Sensor history kept in the cloud is retained only as long as necessary to provide the requested features (for example, history graphs or automations). Users may purge history at any time.
- When a device is factory-reset or unlinked from Google Home, all associated tokens and cached data are deleted.

## 8. Accessibility
- This policy is available at all times to every user in each language supported by the integration. Google crawlers are permitted to index this page.

---

# SeaSensor Personvernerklæring (Norsk)

_Sist oppdatert: 20. november 2025_

Dette dokumentet beskriver hvordan SeaSensor samler inn, lagrer og behandler informasjon om brukere og tilkoblede enheter. Erklæringen oppfyller minimumskravene for integrasjoner publisert via Google Home Developer Platform.

## 1. Åpenhet om data
- Vi samler kun inn informasjon som er nødvendig for å levere SeaSensors hovedfunksjoner (sensorverdier, enhetsstatus og konfigurasjonsdata).
- Brukere informeres hver gang SeaSensor trenger tilgang til nye datakategorier. Vi forklarer hva som samles inn, hvorfor vi gjør det og hvordan dataene brukes.
- SeaSensor selger aldri bruker- eller enhetsdata og bruker dem ikke til annonsering.

## 2. Innsamling og bruk
- **Data levert av brukere:** Wi-Fi-opplysninger, enhetsnavn, kalibreringer, tidsplaner og annen konfigurasjon sendt via SeaSensor sitt web-grensesnitt eller tilhørende apper.
- **Data samlet automatisk:** Sensoravlesninger (temperatur, fuktighet, trykk, batteri, vannnivå), diagnostikk (oppetid, feilstatistikk) og interaksjonslogger (knappetrykk, firmwareversjon).
- **Bruk av data:** Vise målinger lokalt, synkronisere med Google Home, levere varsler eller automatiseringer valgt av brukeren, samt forbedre driftssikkerheten (f.eks. oppdage sensorfeil).
- **Deling:** Data deles kun med tjenester brukeren eksplisitt aktiverer (for eksempel Google Home/Google Cloud, MQTT-meglere eller Firebase). Hver tjeneste mottar kun data som er nødvendige for funksjonen.

## 3. Tilganger
- SeaSensor ber om minste nødvendige tilgang fra Google-API-er eller brukerkontoer for å registrere integrasjonen og utveksle statusoppdateringer.
- Hvis valgfrie tjenester krever ekstra tilganger, må brukeren aktivere dette selv.

## 4. Sikkerhet
- All sensitiv data sendes over krypterte kanaler (HTTPS/TLS eller kryptert MQTT).
- Legitimasjon og token lagres sikkert (f.eks. i kryptert ESP32 NVS) og logges aldri i klartekst.
- Vi følger moderne sikkerhetspraksis for å beskytte data i ro og i transitt.

## 5. Personvernrestriksjoner
- Data hentet fra integrasjoner mot Google Home Developer Platform (inkludert Home API og Home Hub Runtime) **skal ikke** brukes til å trene eller justere kunstig intelligens eller relaterte verktøy.
- Brukerdata selges ikke, utleies ikke og brukes ikke til annonsering.

## 6. Brukerkontroll
- Brukere kan se, endre eller slette konfigurasjonsdata når som helst via SeaSensor-webgrensesnittet eller Google Home-appen (der det er aktuelt).
- Brukere kan be om sletting av skylagret data ved å kontakte SeaSensor-ansvarlig på privacy@seasensor.example. Henvendelser behandles raskt og bekreftes når data er fjernet.

## 7. Lagring og sletting
- Sensorhistorikk i skyen lagres kun så lenge det er nødvendig for funksjonen (f.eks. grafer eller automatiseringer). Brukeren kan slette historikken når som helst.
- Når en enhet tilbakestilles eller fjernes fra Google Home, slettes alle tilknyttede token og buffere.

## 8. Tilgjengelighet
- Denne personvernerklæringen er tilgjengelig til enhver tid på alle språk integrasjonen støtter, og Google sine crawlere har tilgang til dokumentet.
