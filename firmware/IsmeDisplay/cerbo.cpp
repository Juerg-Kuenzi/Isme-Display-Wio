// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Juerg Kuenzi

#include "cerbo.h"

#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <stdio.h>
#include <string.h>

namespace cerbo {
namespace {

const uint16_t MQTT_TLS_PORT = 8883;
const uint32_t KEEPALIVE_MS = 30000;          // Venus OS verlangt < 60 s
const uint32_t RETRY_MIN_MS = 30000;
const uint32_t RETRY_MAX_MS = 300000;

WiFiClientSecure tls;
PubSubClient mqtt(tls);

isme::EngineState engine = isme::EngineState::Unknown;
char stateTopic[64] = "";
char lastText[64] = "aus";
uint32_t lastAttempt = 0, retryMs = 0, lastKeepalive = 0;
bool attempted = false;
uint8_t activeInput = 0;

void onMessage(char* topic, uint8_t* payload, unsigned int length) {
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

void disconnect() {
    if (mqtt.connected()) mqtt.disconnect();
    tls.stop();
    engine = isme::EngineState::Unknown;
}

void publishKeepalive(bool full, const char* portal) {
    char topic[48];
    snprintf(topic, sizeof(topic), "R/%s/keepalive", portal);
    mqtt.publish(topic, full ? "" : "{\"keepalive-options\":[\"suppress-republish\"]}");
}

}  // namespace

void reset() {
    disconnect();
    attempted = false;
    retryMs = 0;
}

void service(const Config& c, bool wifiUp, uint32_t now) {
    if (c.input == 0) {
        if (activeInput) reset();
        activeInput = 0;
        snprintf(lastText, sizeof(lastText), "aus");
        return;
    }
    if (!c.host[0] || !c.portal[0] || !c.password[0]) {
        disconnect();
        snprintf(lastText, sizeof(lastText), "nicht eingerichtet (cerbo=, portal=, mqttpass=)");
        return;
    }
    if (!wifiUp) {
        disconnect();
        snprintf(lastText, sizeof(lastText), "kein WLAN");
        return;
    }
    if (c.input != activeInput) {
        reset();
        activeInput = c.input;
    }

    if (mqtt.connected()) {
        mqtt.loop();
        if (now - lastKeepalive >= KEEPALIVE_MS) {
            publishKeepalive(false, c.portal);
            lastKeepalive = now;
        }
        return;
    }

    // nicht verbunden: Motorsignal unbekannt, neuer Versuch mit wachsendem Abstand
    engine = isme::EngineState::Unknown;
    if (attempted && now - lastAttempt < retryMs) return;
    attempted = true;
    lastAttempt = now;
    retryMs = retryMs ? (retryMs * 2 > RETRY_MAX_MS ? RETRY_MAX_MS : retryMs * 2) : RETRY_MIN_MS;

    snprintf(stateTopic, sizeof(stateTopic), "N/%s/digitalinput/%u/State", c.portal, c.input);
    tls.setHandshakeTimeout(10);
    mqtt.setServer(c.host, MQTT_TLS_PORT);
    mqtt.setCallback(onMessage);
    mqtt.setKeepAlive(45);
    mqtt.setSocketTimeout(5);
    // Benutzername beliebig (mind. ein Zeichen), Passwort wie im Cerbo eingestellt
    if (!mqtt.connect("isme-display-wio", "isme", c.password)) {
        snprintf(lastText, sizeof(lastText), "keine Verbindung zu %s:%u (Fehler %d), neuer Versuch in %lu s",
                 c.host, MQTT_TLS_PORT, mqtt.state(), static_cast<unsigned long>(retryMs / 1000));
        tls.stop();
        return;
    }
    retryMs = 0;
    mqtt.subscribe(stateTopic);
    publishKeepalive(true, c.portal);   // einmal alle abonnierten Werte senden lassen
    char topic[64];
    snprintf(topic, sizeof(topic), "R/%s/digitalinput/%u/State", c.portal, c.input);
    mqtt.publish(topic, "");            // und den Zustand ausdruecklich anfragen
    lastKeepalive = now;
    snprintf(lastText, sizeof(lastText), "verbunden, warte auf Eingang %u", c.input);
}

isme::EngineState engineState() { return engine; }

void status(char* out, size_t size) { snprintf(out, size, "%s", lastText); }

}  // namespace cerbo
