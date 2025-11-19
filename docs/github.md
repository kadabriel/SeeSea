# Publisere SeaSensor til GitHub

1. **Lag repo på github.com**
   - Logg inn → `New repository` → navn f.eks. `sea-sensor` → public/private.
   - Ikke legg til README/licence (vi har filer allerede).

2. **Koble lokalt repo**
   ```bash
   cd /Users/avendsora/codex/sea
   git remote add origin git@github.com:<bruker>/sea-sensor.git
   git branch -M main
   git add .
   git commit -m "Initial import"
   git push -u origin main
   ```

3. **SSH-nøkler**
   - Hvis du ikke har SSH key: `ssh-keygen -t ed25519 -C "din@epost"` og legg `~/.ssh/id_ed25519.pub` inn i GitHub → Settings → SSH keys.

4. **Videre arbeid**
   - Bruk `git status` før commit for å sjekke hva som endres.
   - Oppdater `google/secrets.private` for hånd; fila er ignorert og pushes ikke.

Nå er prosjektet klart for GitHub. Når du senere lager releases kan du tagge med `git tag v1.0 && git push origin v1.0`.
