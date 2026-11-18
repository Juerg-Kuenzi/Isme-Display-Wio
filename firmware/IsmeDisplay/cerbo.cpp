// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Juerg Kuenzi

#include "cerbo.h"

#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <rpcWiFi.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Alle Netzwerkaufrufe laufen per RPC zum WLAN-Chip und blockieren die
// Hauptschleife (Tasten, Anzeige, Konsole), bis sie zurueckkommen. Deshalb:
//  - Verbindungsaufbau erst, wenn das WLAN eine Weile steht, die Hauptseite
//    angezeigt wird und keine Taste gedrueckt wurde (allowConnect).
//  - Zuerst eine gewoehnliche TCP-Verbindung mit kurzer Zeitgrenze. Der TLS-
//    Client des WLAN-Chips wartet beim Verbindungsaufbau sonst ohne Zeitgrenze
//    und beim Handshake bis zu 120 s.
//  - TLS und MQTT mit kurzen Zeitgrenzen, zusammen hoechstens etwa 12 s.
//  - Trennen nur, wenn wirklich eine Verbindung bestand.
//  - Bei Fehlern wachsender Abstand bis zum naechsten Versuch (30 s bis 5 min).

namespace cerbo {
namespace {

const uint16_t MQTT_TLS_PORT = 8883;
const uint32_t KEEPALIVE_MS = 30000;          // Venus OS verlangt < 60 s
const uint32_t RETRY_MIN_MS = 30000;
const uint32_t RETRY_MAX_MS = 300000;
const uint32_t START_DELAY_MS = 20000;        // nach WLAN-Verbindung oder Aenderung
const int32_t TCP_PROBE_MS = 1500;
const int32_t TLS_TIMEOUT_MS = 6000;
const uint16_t MQTT_SOCKET_TIMEOUT_S = 4;
const uint32_t LOOP_INTERVAL_MS = 100;

WiFiClientSecure tls;
PubSubClient mqtt(tls);

isme::EngineState engine = isme::EngineState::Unknown;
char stateTopic[64] = "";
char lastText[64] = "aus";
uint32_t lastAttempt = 0, retryMs = 0, lastKeepalive = 0, lastLoop = 0;
uint32_t readySince = 0;      // seit wann WLAN und Einstellungen passen
bool ready = false;
bool attempted = false;
bool linkOpen = false;        // TLS-Verbindung (evtl. mit MQTT) besteht
uint8_t activeInput = 0;
void (*logger)(const char*) = nullptr;

// Protokoll fuer die Fehlersuche (Konsole: log=1). Jede Zeile wird sofort
// gesendet, damit bei einem Haenger die letzte Zeile zeigt, wo es steht.
void trace(const char* fmt, ...) {
    if (!logger) return;
    char text[120];
    int n = snprintf(text, sizeof(text), "[%lu.%03lu] cerbo: ", millis() / 1000, millis() % 1000);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text + n, sizeof(text) - n, fmt, ap);
    va_end(ap);
    logger(text);
}

void onMessage(char* topic, uint8_t* payload, unsigned int length) {
    trace("Meldung %s = %.*s", topic, static_cast<int>(length > 40 ? 40 : length), reinterpret_cast<const char*>(payload));
    if (strcmp(topic, stateTopic) != 0) return;
    long v;
    if (!isme::parseVictronInt(reinterpret_cast<const char*>(payload), length, v)) {
        engine = isme::EngineState::Unknown;   // Eingang weg oder ohne Wert
        snprintf(lastText, sizeof(lastText), "verbunden, Eingang meldet keinen Wert");
    } else if (v == 1) {
        engine = isme::EngineState::Running;
        snprintf(lastText, sizeof(lastText), "verbunden, Motor laeuft");
    } else if (v == 0) {
        engine = isme::EngineState::Off;
        snprintf(lastText, sizeof(lastText), "verbunden, Motor aus");
    } else {
        engine = isme::EngineState::Unknown;
        snprintf(lastText, sizeof(lastText), "Eingang nicht als 'Generic I/O' eingestellt (%ld)", v);
    }
}

// Nur wenn eine Verbindung besteht, sonst keine RPC-Aufrufe
void closeLink() {
    if (linkOpen) {
        uint32_t t = millis();
        trace("trenne ...");
        if (mqtt.state() == MQTT_CONNECTED) mqtt.disconnect();   // ruft auch tls.stop()
        tls.stop();
        linkOpen = false;
        trace("-> getrennt (%lu ms)", millis() - t);
    }
    engine = isme::EngineState::Unknown;
}

// Neuer Anlauf nach Aenderung oder neuer WLAN-Verbindung
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

void publishKeepalive(bool full, const char* portal) {
    char topic[48];
    snprintf(topic, sizeof(topic), "R/%s/keepalive", portal);
    mqtt.publish(topic, full ? "" : "{\"keepalive-options\":[\"suppress-republish\"]}");
}

bool resolve(const char* host, IPAddress& ip) {
    if (ip.fromString(host)) return true;
    return WiFi.hostByName(host, ip) == 1;
}

void connectNow(const Config& c, uint32_t now) {
    scheduleRetry(now);
    unsigned long wait = static_cast<unsigned long>(retryMs / 1000);

    IPAddress ip;
    uint32_t t = millis();
    trace("Versuch: Adresse %s aufloesen ...", c.host);
    bool resolved = resolve(c.host, ip);
    trace("-> %s %u.%u.%u.%u (%lu ms)", resolved ? "ok" : "FEHLER", ip[0], ip[1], ip[2], ip[3], millis() - t);
    if (!resolved) {
        snprintf(lastText, sizeof(lastText), "Adresse %.24s unbekannt, neuer Versuch in %lu s", c.host, wait);
        return;
    }
    // 1. Ist der Cerbo ueberhaupt erreichbar? (mit Zeitgrenze)
    {
        WiFiClient probe;
        t = millis();
        trace("TCP-Test Port %u ...", MQTT_TLS_PORT);
        bool ok = probe.connect(ip, MQTT_TLS_PORT, TCP_PROBE_MS);
        trace("-> %s (%lu ms)", ok ? "ok" : "FEHLER, nicht erreichbar", millis() - t);
        if (!ok) {
            snprintf(lastText, sizeof(lastText), "Cerbo nicht erreichbar (Port 8883), neuer Versuch in %lu s", wait);
            return;
        }
        t = millis();
        probe.stop();
        trace("TCP-Test beendet (%lu ms)", millis() - t);
    }
    // 2. TLS ohne Zertifikatspruefung, Handshake mit Zeitgrenze
    t = millis();
    trace("TLS-Handshake ...");
    bool tlsOk = tls.connect(ip, MQTT_TLS_PORT, TLS_TIMEOUT_MS);
    if (tlsOk) trace("-> ok (%lu ms)", millis() - t);
    else {
        char err[100] = "";
        int code = tls.lastError(err, sizeof(err));
        trace("-> FEHLER %d %s (%lu ms)", code, err, millis() - t);
    }
    if (!tlsOk) {
        snprintf(lastText, sizeof(lastText), "TLS-Verbindung fehlgeschlagen, neuer Versuch in %lu s", wait);
        return;
    }
    linkOpen = true;
    // 3. MQTT-Anmeldung. PubSubClient nimmt die bestehende TLS-Verbindung.
    snprintf(stateTopic, sizeof(stateTopic), "N/%s/digitalinput/%u/State", c.portal, c.input);
    mqtt.setServer(ip, MQTT_TLS_PORT);
    mqtt.setCallback(onMessage);
    mqtt.setKeepAlive(45);
    mqtt.setSocketTimeout(MQTT_SOCKET_TIMEOUT_S);
    // Benutzername beliebig (mind. ein Zeichen), Passwort wie im Cerbo eingestellt
    t = millis();
    trace("MQTT-Anmeldung ...");
    bool mqttOk = mqtt.connect("isme-display-wio", "isme", c.password);
    trace("-> %s, Zustand %d (%lu ms)", mqttOk ? "ok" : "FEHLER", mqtt.state(), millis() - t);
    if (!mqttOk) {
        int st = mqtt.state();
        if (st == MQTT_CONNECT_BAD_CREDENTIALS || st == MQTT_CONNECT_UNAUTHORIZED)
            snprintf(lastText, sizeof(lastText), "MQTT-Passwort falsch, neuer Versuch in %lu s", wait);
        else
            snprintf(lastText, sizeof(lastText), "MQTT-Anmeldung fehlgeschlagen (%d), neuer Versuch in %lu s", st, wait);
        closeLink();
        return;
    }
    retryMs = 0;
    t = millis();
    trace("abonniere %s ...", stateTopic);
    mqtt.subscribe(stateTopic);
    publishKeepalive(true, c.portal);   // einmal alle abonnierten Werte senden lassen
    char topic[64];
    snprintf(topic, sizeof(topic), "R/%s/digitalinput/%u/State", c.portal, c.input);
    mqtt.publish(topic, "");            // und den Zustand ausdruecklich anfragen
    lastKeepalive = now;
    lastLoop = now;
    trace("-> verbunden (%lu ms)", millis() - t);
    snprintf(lastText, sizeof(lastText), "verbunden, warte auf Eingang %u", c.input);
}

}  // namespace

void reset() {
    closeLink();
    activeInput = 0;   // beim naechsten service() neu beginnen, mit Wartezeit
    ready = false;
}

void service(const Config& c, bool wifiUp, bool allowConnect, uint32_t now) {
    if (c.input == 0) {
        if (activeInput) closeLink();
        activeInput = 0;
        engine = isme::EngineState::Unknown;
        snprintf(lastText, sizeof(lastText), "aus");
        return;
    }
    if (!c.host[0] || !c.portal[0] || !c.password[0]) {
        closeLink();
        ready = false;
        snprintf(lastText, sizeof(lastText), "nicht eingerichtet (cerbo=, portal=, mqttpass=)");
        return;
    }
    if (!wifiUp) {
        closeLink();
        ready = false;
        snprintf(lastText, sizeof(lastText), "kein WLAN");
        return;
    }
    if (c.input != activeInput) {
        activeInput = c.input;
        restart(now);
    }
    if (!ready) {   // WLAN gerade verbunden oder Einstellungen geaendert
        trace("bereit, Eingang %u, %s, erster Versuch in %lu s", c.input, c.host, START_DELAY_MS / 1000);
        ready = true;
        readySince = now;
        attempted = false;
        retryMs = 0;
    }

    if (linkOpen) {
        if (now - lastLoop < LOOP_INTERVAL_MS) return;
        lastLoop = now;
        uint32_t t = millis();
        bool alive = mqtt.loop();
        if (millis() - t > 200) trace("mqtt.loop langsam (%lu ms)", millis() - t);
        if (alive) {
            if (now - lastKeepalive >= KEEPALIVE_MS) {
                t = millis();
                trace("Keepalive ...");
                publishKeepalive(false, c.portal);
                lastKeepalive = now;
                trace("-> gesendet (%lu ms)", millis() - t);
            }
            return;
        }
        trace("Verbindung verloren, MQTT-Zustand %d", mqtt.state());
        closeLink();   // Verbindung verloren
        retryMs = 0;
        scheduleRetry(now);
        snprintf(lastText, sizeof(lastText), "Verbindung verloren, neuer Versuch in %lu s",
                 static_cast<unsigned long>(retryMs / 1000));
        return;
    }

    // nicht verbunden: Motorsignal unbekannt
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

void setLogger(void (*fn)(const char*)) { logger = fn; }

void status(char* out, size_t size) { snprintf(out, size, "%s", lastText); }

}  // namespace cerbo
