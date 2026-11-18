// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Juerg Kuenzi
#pragma once

// Motorsignal vom Cerbo GX ueber MQTT (Venus OS, dbus-flashmq).
//
// Profil "Secured": Port 8883, TLS ohne Zertifikatspruefung (der Cerbo hat ein
// selbst signiertes Zertifikat), beliebiger Benutzername, Passwort aus den
// Einstellungen des Cerbo. Gelesen wird
//   N/<Portal-ID>/digitalinput/<Eingang>/State
// eines Digitaleingangs vom Typ "Generic I/O": 1 = high = Motor laeuft,
// 0 = low = Motor aus. Ob ein geschlossener Kontakt high oder low ist, stellt man
// im Cerbo mit "Invert" ein. Jede andere Meldung, keine Verbindung oder keine
// Meldung ergibt EngineState::Unknown; die Abgasueberwachung laeuft dann wie
// ohne Motorsignal.

#include <stddef.h>
#include <stdint.h>

#include "core.h"

namespace cerbo {

struct Config {
    uint8_t input;          // 0 = aus, 1-4 = Digitaleingang
    const char* host;       // IP-Adresse oder Name des Cerbo
    const char* portal;     // VRM Portal ID
    const char* password;   // MQTT-Passwort ("Secured"/"Weak")
};

// In jeder Schleife aufrufen. Baut die Verbindung bei Bedarf auf (blockiert dann
// einige Sekunden fuer den TLS-Handshake, deshalb hoechstens alle 30 s bis 5 min).
void service(const Config& c, bool wifiUp, uint32_t now);

// Nach einer Aenderung der Einstellungen: Verbindung trennen, neu aufbauen
void reset();

isme::EngineState engineState();

// Zustand in Worten, z. B. "verbunden, Motor laeuft"
void status(char* out, size_t size);

}  // namespace cerbo
