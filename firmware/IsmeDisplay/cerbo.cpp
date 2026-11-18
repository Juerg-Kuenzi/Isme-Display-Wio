// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Juerg Kuenzi

#include "cerbo.h"

#include <rpcWiFi.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Motorsignal per Modbus TCP vom Cerbo GX.
//
// Warum nicht MQTT: Der Cerbo verlangt fuer MQTT mit Passwort TLS. Der TLS-Client
// des WLAN-Chips (RTL8720DN) kommt mit den Verfahren des Cerbo nicht zurecht
// (Handshake bricht ab), unverschluesseltes MQTT gaebe es nur ohne jedes
// Passwort (Profil "Unsecured").
//
// Alle Netzwerkaufrufe laufen per RPC zum WLAN-Chip und halten die Hauptschleife
// an, solange sie dauern. Deshalb:
//  - Verbindungsaufbau erst 3 s nach der WLAN-Verbindung oder einer Aenderung,
//    nur auf der Hauptseite ohne Bedienung (allowConnect). Der Aufbau hat im
//    WLAN-Chip keine Zeitgrenze: meist unter 1 s, ist der Cerbo aus bis ~30 s.
//    Ein eigener nicht-blockierender Aufbau fuehrte zu Abstuerzen.
//  - Danach bleibt die Verbindung offen; jede Sekunde eine Anfrage (12 Bytes),
//    die Antwort wird ohne Warten abgeholt.
//  - Bei Fehlern wachsender Abstand bis zum naechsten Versuch (30 s bis 5 min).

namespace cerbo {
namespace {

const uint32_t RETRY_MIN_MS = 30000;
const uint32_t RETRY_MAX_MS = 300000;
const uint32_t START_DELAY_MS = 3000;          // WLAN nach dem Verbinden sicher bereit
const uint32_t POLL_MS = 1000;
const uint32_t POLL_SLOW_MS = 5000;           // wenn der Cerbo die Geraete-ID nicht kennt
const uint32_t ANSWER_TIMEOUT_MS = 3000;
const uint32_t LOOP_INTERVAL_MS = 100;

WiFiClient client;

isme::EngineState engine = isme::EngineState::Unknown;
char lastText[64] = "aus";
uint32_t lastAttempt = 0, retryMs = 0, lastLoop = 0, lastRequest = 0, pollMs = POLL_MS;
uint32_t readySince = 0;
bool ready = false;
bool attempted = false;
bool linkOpen = false;
bool waiting = false;          // Anfrage gesendet, Antwort ausstehend
uint16_t transaction = 0;
uint8_t activeUnit = 0;
long lastState = -1;
uint8_t rx[32];
size_t rxLen = 0;
void (*logger)(const char*) = nullptr;

// Protokoll fuer die Fehlersuche (Konsole: log=ein). Jede Zeile wird sofort
// gesendet, damit bei einem Haenger die letzte Zeile zeigt, wo es steht.
void trace(const char* fmt, ...) {
    if (!logger) return;
    char text[120];
    uint32_t ms = millis();
    int n = snprintf(text, sizeof(text), "[%lu.%03lu] cerbo: ", static_cast<unsigned long>(ms / 1000),
                     static_cast<unsigned long>(ms % 1000));
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text + n, sizeof(text) - n, fmt, ap);
    va_end(ap);
    logger(text);
}

void closeLink() {
    if (linkOpen) {
        uint32_t t = millis();
        trace("trenne ...");
        client.stop();
        linkOpen = false;
        trace("-> getrennt (%lu ms)", static_cast<unsigned long>(millis() - t));
    }
    waiting = false;
    rxLen = 0;
    lastState = -1;
    engine = isme::EngineState::Unknown;
}

void restart(uint32_t now) {
    closeLink();
    attempted = false;
    retryMs = 0;
    ready = false;
    readySince = now;
}

void scheduleRetry(uint32_t now) {
    attempted = true;
    lastAttempt = now;
    retryMs = retryMs ? (retryMs * 2 > RETRY_MAX_MS ? RETRY_MAX_MS : retryMs * 2) : RETRY_MIN_MS;
}

void fail(uint32_t now, const char* what) {
    closeLink();
    scheduleRetry(now);
    snprintf(lastText, sizeof(lastText), "%s, neuer Versuch in %lu s", what, static_cast<unsigned long>(retryMs / 1000));
    trace("%s", lastText);
}

bool resolve(const char* host, IPAddress& ip) {
    if (ip.fromString(host)) return true;
    return WiFi.hostByName(host, ip) == 1;
}

void connectNow(const Config& c, uint32_t now) {
    IPAddress ip;
    trace("Versuch: Adresse %s aufloesen ...", c.host);
    if (!resolve(c.host, ip)) {
        fail(now, "Adresse unbekannt");
        return;
    }
    trace("Modbus TCP %u.%u.%u.%u:%u verbinden ...", ip[0], ip[1], ip[2], ip[3], isme::MODBUS_TCP_PORT);
    uint32_t t = millis();
    int r = client.connect(ip, isme::MODBUS_TCP_PORT);
    trace("-> %s (%lu ms)", r ? "verbunden" : "FEHLER", static_cast<unsigned long>(millis() - t));
    if (!r) {
        fail(now, "Cerbo nicht erreichbar (Modbus TCP aus?)");
        return;
    }
    linkOpen = true;
    retryMs = 0;
    waiting = false;
    rxLen = 0;
    pollMs = POLL_MS;
    lastRequest = now - POLL_MS;   // gleich anfragen
    snprintf(lastText, sizeof(lastText), "verbunden, frage Geraete-ID %u ab", c.unit);
}

void sendRequest(const Config& c, uint32_t now) {
    uint8_t rq[12];
    transaction++;
    isme::modbusReadRequest(rq, transaction, c.unit, isme::MODBUS_REG_DIGITALINPUT_STATE, 1);
    size_t n = client.write(rq, sizeof(rq));
    if (n != sizeof(rq)) {
        trace("Anfrage senden FEHLER (%u)", static_cast<unsigned>(n));
        fail(now, "Verbindung verloren");
        return;
    }
    waiting = true;
    lastRequest = now;
    rxLen = 0;
}

void receive(const Config& c, uint32_t now) {
    int avail = client.available();
    if (avail > 0) {
        size_t room = sizeof(rx) - rxLen;
        int n = client.read(rx + rxLen, avail < static_cast<int>(room) ? avail : static_cast<int>(room));
        if (n > 0) rxLen += static_cast<size_t>(n);
    }
    uint16_t value = 0;
    uint8_t exc = 0;
    size_t used = 0;
    switch (isme::parseModbusReadResponse(rx, rxLen, transaction, c.unit, value, exc, used)) {
        case isme::ModbusResult::Incomplete:
            if (rxLen >= sizeof(rx)) {
                fail(now, "Antwort zu lang");
            } else if (now - lastRequest > ANSWER_TIMEOUT_MS) {
                fail(now, "keine Antwort vom Cerbo");
            }
            return;
        case isme::ModbusResult::Invalid:
            trace("ungueltige Antwort (%u Bytes)", static_cast<unsigned>(rxLen));
            fail(now, "ungueltige Antwort");
            return;
        case isme::ModbusResult::Exception:
            waiting = false;
            pollMs = POLL_SLOW_MS;
            engine = isme::EngineState::Unknown;
            if (lastState != -2) trace("Modbus-Fehler %u", exc);
            lastState = -2;
            if (exc == 0x0A || exc == 0x0B)
                snprintf(lastText, sizeof(lastText), "Geraete-ID %u im Cerbo nicht gefunden (Eingang aktiv?)", c.unit);
            else
                snprintf(lastText, sizeof(lastText), "Cerbo meldet Modbus-Fehler %u", exc);
            return;
        case isme::ModbusResult::Value:
            break;
    }
    waiting = false;
    pollMs = POLL_MS;
    if (static_cast<long>(value) != lastState) trace("Zustand %u", value);
    lastState = value;
    engine = isme::engineFromDigitalInputState(value);
    switch (engine) {
        case isme::EngineState::Running: snprintf(lastText, sizeof(lastText), "verbunden, Motor laeuft"); break;
        case isme::EngineState::Off: snprintf(lastText, sizeof(lastText), "verbunden, Motor aus"); break;
        default:
            snprintf(lastText, sizeof(lastText), "Typ des Eingangs passt nicht (Zustand %u)", value);
            break;
    }
}

}  // namespace

void reset() {
    closeLink();
    activeUnit = 0;
    ready = false;
}

void service(const Config& c, bool wifiUp, bool allowConnect, uint32_t now) {
    if (c.unit == 0) {
        if (activeUnit) closeLink();
        activeUnit = 0;
        engine = isme::EngineState::Unknown;
        snprintf(lastText, sizeof(lastText), "aus");
        return;
    }
    if (!c.host[0]) {
        closeLink();
        ready = false;
        snprintf(lastText, sizeof(lastText), "Adresse des Cerbo fehlt (cerbo=)");
        return;
    }
    if (!wifiUp) {
        closeLink();
        ready = false;
        snprintf(lastText, sizeof(lastText), "kein WLAN");
        return;
    }
    if (c.unit != activeUnit) {
        activeUnit = c.unit;
        restart(now);
    }
    if (!ready) {
        trace("bereit, Geraete-ID %u, %s, erster Versuch in %lu s", c.unit, c.host,
              static_cast<unsigned long>(START_DELAY_MS / 1000));
        ready = true;
        readySince = now;
        attempted = false;
        retryMs = 0;
    }

    if (linkOpen) {
        if (now - lastLoop < LOOP_INTERVAL_MS) return;
        lastLoop = now;
        if (waiting) receive(c, now);
        else if (now - lastRequest >= pollMs) sendRequest(c, now);
        return;
    }

    engine = isme::EngineState::Unknown;
    if (now - readySince < START_DELAY_MS) {
        snprintf(lastText, sizeof(lastText), "Verbindung in %lu s",
                 static_cast<unsigned long>((START_DELAY_MS - (now - readySince) + 999) / 1000));
        return;
    }
    if (attempted && now - lastAttempt < retryMs) return;
    if (!allowConnect) {
        static uint32_t lastWaitLog = 0;
        if (now - lastWaitLog > 10000) {
            trace("warte: nicht auf der Hauptseite oder Taste vor weniger als 10 s");
            lastWaitLog = now;
        }
        if (!attempted) snprintf(lastText, sizeof(lastText), "verbindet, sobald die Hauptseite angezeigt wird");
        return;
    }
    connectNow(c, now);
}

isme::EngineState engineState() { return engine; }

void status(char* out, size_t size) { snprintf(out, size, "%s", lastText); }

void setLogger(void (*fn)(const char*)) { logger = fn; }

void testTcp(const char* host, uint16_t port, void (*out)(const char*)) {
    void (*saved)(const char*) = logger;
    logger = out;
    IPAddress ip;
    if (!resolve(host, ip)) {
        trace("Test: Adresse %s unbekannt", host);
    } else {
        trace("Test: Verbindung zu %s:%u (ohne Zeitgrenze) ...", host, port);
        uint32_t t = millis();
        WiFiClient test;
        int r = test.connect(ip, port);
        trace("Test: %s nach %lu ms, trenne ...", r ? "verbunden" : "FEHLER", static_cast<unsigned long>(millis() - t));
        test.stop();
        trace("Test: getrennt");
    }
    logger = saved;
}

}  // namespace cerbo
