#!/bin/bash

# Gå til prosjektkatalogen
cd /Users/avendsora/codex/sea  # Oppdater denne banen til din prosjektmappe

# Last inn ESP-IDF-miljøet
. $HOME/esp-idf/export.sh

# Start tmux og kjør idf.py monitor
tmux new-session -d -s esp_monitor 'idf.py -p /dev/cu.SLAB_USBtoUART monitor | tee monitor.log'

# Kjør tmux i 20 sekunder, deretter avslutt
# Kjør monitor lenger (40s) for å se Tuya/HTTP
sleep 40
tmux send-keys -t esp_monitor C-] 'exit' Enter

# Lagre loggen
tmux capture-pane -t esp_monitor -S -10000
tmux save-buffer monitor.log

# Avslutt tmux-økten
tmux kill-session -t esp_monitor

echo "Monitoren har kjørt i 20 sekunder, og loggen er lagret i monitor.log"
