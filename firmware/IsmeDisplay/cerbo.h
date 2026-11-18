// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Juerg Kuenzi
#pragma once

// Motorsignal vom Cerbo GX ueber Modbus TCP (Port 502, unverschluesselt, ohne
// Passwort; im Cerbo unter Einstellungen > Integrationen > Modbus TCP).
//
// Gelesen wird Register 3422 (/State) des Digitaleingangs mit der Geraete-ID
// (Modbus Unit-ID) aus "Verfuegbare Dienste". Typ des Eingangs z. B.
// "Bilgepumpe": ein = Motor laeuft, aus = Motor aus; vertauscht mit
// "Invertieren". Keine Verbindung, keine Antwort oder ein unpassender Typ ergibt
// EngineState::Unknown; die Abgasueberwachung laeuft dann wie ohne Motorsignal.

#include <stddef.h>
#include <stdint.h>

#include "core.h"

namespace cerbo {

struct Config {
    uint8_t unit;           // 0 = aus, sonst Geraete-ID (Modbus Unit-ID) des Digitaleingangs
    const char* host;       // IP-Adresse oder Name des Cerbo
};

// In jeder Schleife aufrufen. Der Verbindungsaufbau haelt die Schleife an (meist
// unter 1 s, ist der Cerbo aus bis ~30 s). Er beginnt fruehestens 20 s nach der
// WLAN-Verbindung oder einer Aenderung, nur wenn allowConnect gesetzt ist
// (Hauptseite, keine Bedienung), nach Fehlern erst wieder nach 30 s bis 5 min.
void service(const Config& c, bool wifiUp, bool allowConnect, uint32_t now);

// Nach einer Aenderung der Einstellungen: Verbindung trennen, neu aufbauen
void reset();

isme::EngineState engineState();

// Zustand in Worten, z. B. "verbunden, Motor laeuft"
void status(char* out, size_t size);

// Protokoll an die Konsole (nullptr = aus)
void setLogger(void (*fn)(const char*));

// Fehlersuche: TCP-Verbindung zu host:port testen, jeder Schritt an out
void testTcp(const char* host, uint16_t port, void (*out)(const char*));

}  // namespace cerbo
