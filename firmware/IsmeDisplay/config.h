#pragma once

#define FW_VERSION              "1.0.0"

// Standardwerte. Die Einstellungen am Geraet werden im Flash gespeichert.
#define DEFAULT_CHAIN_PORT      1458    // ChainCounter, Spezifikation V1.7
#define DEFAULT_N2K_PORT        1457    // YDWG-02, RAW-Format
#define DEFAULT_ALARM_LIMIT     50      // Abgastemperatur in Grad Celsius

#define WIFI_RETRY_MS           15000   // WLAN neu verbinden, wenn so lange keine Verbindung besteht
#define TONE_INTERVAL_MS        2000    // Alarmton: alle 2 s ...
#define TONE_DURATION_MS        1000    // ... fuer 1 s
#define BUZZER_DUTY             128     // Lautstaerke des Summers (0 bis 255)
