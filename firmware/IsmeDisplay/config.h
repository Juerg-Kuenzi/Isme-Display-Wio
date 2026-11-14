#pragma once

#define FW_VERSION              "1.3.0"

// Standardwerte. Die Einstellungen am Geraet werden im Flash gespeichert.
#define DEFAULT_CHAIN_PORT      1458    // ChainCounter, Spezifikation V1.8
#define DEFAULT_N2K_PORT        1457    // YDWG-02, RAW-Format
#define DEFAULT_ALARM_LIMIT     50      // Abgastemperatur in Grad Celsius

#define WIFI_RETRY_MS           15000   // WLAN neu verbinden, wenn so lange keine Verbindung besteht
// Alarmton. Ein Piezo-Summer ist bei seiner Resonanzfrequenz am lautesten,
// deshalb ist die Tonhoehe am Geraet unter "Ton testen" einstellbar.
#define TONE_DEFAULT_HZ         2700
#define TONE_MIN_HZ             500
#define TONE_MAX_HZ             5000
#define TONE_STEP_HZ            100
#define DEFAULT_UTC_OFFSET      0       // Zeitzone in Stunden, UTC-12 bis UTC+14
