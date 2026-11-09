// Anzeige, Bedienung, WLAN und Summer fuer das Seeed Wio Terminal.
// Funktional wie die Android-App Isme: Seiten Ankerkette und NMEA2000, Einstellungen, Abgasalarm.
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <rpcWiFi.h>
#include <WiFiUdp.h>
// Darf nur in einer Datei eingebunden werden (stellt EEPROM im Flash bereit).
#include <FlashStorage_SAMD.h>

#include <stdio.h>
#include <string.h>

#include "app.h"
#include "config.h"
#include "core.h"
#include "secrets.h"

using namespace isme;

// ---------------------------------------------------------------------------
// Tasten. Die drei Tasten oben sind von links nach rechts C, B, A.
// ---------------------------------------------------------------------------
#ifndef KEY_MENU
#define KEY_MENU WIO_KEY_C   // links: Menue oeffnen und zurueck
#endif
#ifndef KEY_MUTE
#define KEY_MUTE WIO_KEY_A   // rechts: Alarmton aus
#endif

namespace {

TFT_eSPI tft;
WiFiUDP chainUdp;
WiFiUDP n2kUdp;

// ---------------------------------------------------------------------------
// Einstellungen im Flash
// ---------------------------------------------------------------------------
const uint32_t SETTINGS_MAGIC_V1 = 0x15E00001;   // Firmware 1.0.0, ohne Tonhoehe
const uint32_t SETTINGS_MAGIC = 0x15E00002;

struct Settings {
    uint32_t magic;
    uint16_t chainPort;
    uint16_t n2kPort;
    uint16_t alarmLimit;
    uint8_t dark;
    uint8_t reserved;
    uint16_t toneHz;
};

Settings settings;

void loadSettings() {
    EEPROM.get(0, settings);
    if (settings.magic == SETTINGS_MAGIC_V1) {
        settings.magic = SETTINGS_MAGIC;
        settings.toneHz = TONE_DEFAULT_HZ;
    }
    if (settings.toneHz < TONE_MIN_HZ || settings.toneHz > TONE_MAX_HZ) settings.toneHz = TONE_DEFAULT_HZ;
    if (settings.magic != SETTINGS_MAGIC) {
        settings.magic = SETTINGS_MAGIC;
        settings.chainPort = DEFAULT_CHAIN_PORT;
        settings.n2kPort = DEFAULT_N2K_PORT;
        settings.alarmLimit = DEFAULT_ALARM_LIMIT;
        settings.dark = 0;
        settings.reserved = 0;
        settings.toneHz = TONE_DEFAULT_HZ;
    }
}

void saveSettings() {
    EEPROM.put(0, settings);
    EEPROM.commit();
}

// ---------------------------------------------------------------------------
// Farben wie in der App: tags Seekarte, nachts rote Instrumentenbeleuchtung
// ---------------------------------------------------------------------------
struct Palette {
    uint16_t bg, ink, muted, rule, warning, onWarning;
};
Palette pal;

void applyPalette() {
    if (settings.dark) {
        pal = {TFT_BLACK, tft.color565(0xFF, 0x4B, 0x3A), tft.color565(0xD0, 0x5A, 0x47),
               tft.color565(0x4A, 0x1A, 0x15), tft.color565(0xFF, 0xB0, 0x00), TFT_BLACK};
    } else {
        pal = {tft.color565(0xEE, 0xF3, 0xF1), tft.color565(0x0E, 0x2A, 0x3B), tft.color565(0x56, 0x6B, 0x78),
               tft.color565(0xB5, 0xC6, 0xCC), tft.color565(0xA3, 0x14, 0x6B), TFT_WHITE};
    }
}

// ---------------------------------------------------------------------------
// Text mit Umlauten. Die eingebauten Schriften 2 und 4 kennen nur ASCII,
// deshalb werden die Punkte von ä ö ü und das Gradzeichen selbst gezeichnet.
// ---------------------------------------------------------------------------
enum Align { ALIGN_LEFT, ALIGN_RIGHT, ALIGN_CENTER };

struct Mark {
    uint8_t index;
    uint8_t kind;   // 1 Umlaut klein, 2 Umlaut gross, 3 Grad
};

int toAscii(const char* s, int font, char* out, int size, Mark* marks, int maxMarks) {
    int n = 0, m = 0;
    while (*s && n < size - 3) {
        uint8_t c = static_cast<uint8_t>(*s);
        if (c == 0xC3 && s[1]) {
            uint8_t d = static_cast<uint8_t>(s[1]);
            char base = '?';
            uint8_t kind = 0;
            switch (d) {
                case 0xA4: base = 'a'; kind = 1; break;
                case 0xB6: base = 'o'; kind = 1; break;
                case 0xBC: base = 'u'; kind = 1; break;
                case 0x84: base = 'A'; kind = 2; break;
                case 0x96: base = 'O'; kind = 2; break;
                case 0x9C: base = 'U'; kind = 2; break;
                case 0x9F: out[n++] = 's'; base = 's'; break;
                case 0xA9: base = 'e'; break;
                default: break;
            }
            if (kind && m < maxMarks) marks[m++] = {static_cast<uint8_t>(n), kind};
            out[n++] = base;
            s += 2;
        } else if (c == 0xC2 && static_cast<uint8_t>(s[1]) == 0xB0) {
            if (m < maxMarks) marks[m++] = {static_cast<uint8_t>(n), 3};
            out[n++] = ' ';
            if (font == 2) out[n++] = ' ';
            s += 2;
        } else if (c < 0x80) {
            out[n++] = *s++;
        } else {
            out[n++] = '?';
            s++;
            while ((static_cast<uint8_t>(*s) & 0xC0) == 0x80) s++;
        }
    }
    out[n] = 0;
    return m;
}

int textWidth(const char* text, int font) {
    char a[128];
    Mark marks[16];
    toAscii(text, font, a, sizeof(a), marks, 16);
    return tft.textWidth(a, font);
}

// Zeichnet ohne Hintergrund zu loeschen. Liefert die Breite.
int drawText(const char* text, int x, int y, int font, uint16_t fg, uint16_t bg, Align align) {
    char a[128];
    Mark marks[16];
    int m = toAscii(text, font, a, sizeof(a), marks, 16);
    int w = tft.textWidth(a, font);
    int x0 = align == ALIGN_RIGHT ? x - w : align == ALIGN_CENTER ? x - w / 2 : x;
    tft.setTextDatum(TL_DATUM);
    tft.setTextPadding(0);
    tft.setTextColor(fg, bg);
    tft.drawString(a, x0, y, font);
    for (int i = 0; i < m; i++) {
        char prefix[128];
        memcpy(prefix, a, marks[i].index);
        prefix[marks[i].index] = 0;
        char one[2] = {a[marks[i].index], 0};
        int px = x0 + tft.textWidth(prefix, font);
        int cx = px + tft.textWidth(one, font) / 2;
        bool big = font >= 4;
        if (marks[i].kind == 3) {
            int r = big ? 3 : 2;
            int cy = y + (big ? 6 : 4);
            tft.drawCircle(cx, cy, r, fg);
            if (big) tft.drawCircle(cx, cy, r - 1, fg);
        } else {
            int dy = marks[i].kind == 2 ? 0 : (big ? 4 : 2);
            int s = big ? 3 : 2;
            tft.fillRect(cx - s - 1, y + dy, s, s, fg);
            tft.fillRect(cx + 1, y + dy, s, s, fg);
        }
    }
    return w;
}

// Bricht Text an Wortgrenzen um. Liefert die Zahl der Zeilen.
int drawWrapped(const char* text, int x, int y, int width, int font, uint16_t fg, uint16_t bg, int maxLines,
                int lineHeight) {
    char line[128];
    int lines = 0;
    const char* p = text;
    while (*p && lines < maxLines) {
        int len = 0, lastBreak = -1;
        const char* q = p;
        while (*q && *q != '\n') {
            if (len >= static_cast<int>(sizeof(line)) - 1) break;
            line[len] = *q;
            line[len + 1] = 0;
            if (textWidth(line, font) > width && lastBreak > 0) break;
            if (*q == ' ') lastBreak = len;
            len++;
            q++;
        }
        if (*q && *q != '\n' && lastBreak > 0) {
            len = lastBreak;
            q = p + lastBreak + 1;
        } else if (*q == '\n') {
            q++;
        }
        line[len] = 0;
        drawText(line, x, y + lines * lineHeight, font, fg, bg, ALIGN_LEFT);
        lines++;
        p = q;
    }
    return lines;
}

// Bereiche nur neu zeichnen, wenn sich ihr Inhalt geaendert hat.
const int REGIONS = 24;
char regionCache[REGIONS][96];

bool changed(int region, const char* signature) {
    if (strncmp(regionCache[region], signature, sizeof(regionCache[0]) - 1) == 0) return false;
    strncpy(regionCache[region], signature, sizeof(regionCache[0]) - 1);
    regionCache[region][sizeof(regionCache[0]) - 1] = 0;
    return true;
}

void invalidateRegions() {
    for (int i = 0; i < REGIONS; i++) regionCache[i][0] = 1;   // passt zu keiner Signatur
}

// ---------------------------------------------------------------------------
// Zustand
// ---------------------------------------------------------------------------
enum class UiScreen { Main, Menu, Detail, Info };

enum Item {
    ITEM_DARK, ITEM_ZERO, ITEM_LENGTH, ITEM_PORT, ITEM_DEBOUNCE, ITEM_FACTOR, ITEM_RESTART,
    ITEM_N2K_PORT, ITEM_ALARM, ITEM_TONE, ITEM_INFO, ITEM_COUNT
};

const char* const ITEM_LABELS[ITEM_COUNT] = {
    "Hell/Dunkel", "Zähler nullen", "Länge setzen", "Port ChainCounter", "Entprellzeit",
    "Umrechnungsfaktor", "Restart", "Port NMEA2000", "Alarmwert Abgas", "Ton testen",
    "Informationen",
};

const char* const ITEM_TEXT[ITEM_COUNT] = {
    "",
    "Setzt den Kettenzähler auf 0. Das Gerät speichert den Wert spätestens nach 15 Minuten.",
    "Setzt die Kettenlänge in ganzen Metern. Das Gerät speichert den Wert spätestens nach 15 Minuten.",
    "Port, auf dem der ChainCounter sendet. Am Gerät wird er an der Konsole mit port= eingestellt.",
    "Entprellzeit des Impulseingangs. Das Gerät meldet diesen Wert nicht zurück.",
    "Meter pro Umdrehung. Das Gerät meldet diesen Wert nicht zurück.",
    "Das Gerät speichert seine Daten und startet neu. Dafür braucht es Firmware 1.2.0.",
    "Port, auf dem die NMEA2000-Bridge (YDWG-02) im RAW-Format sendet.",
    "Darüber gibt es Alarm mit Ton. Der Alarm endet 1 °C unter dem Alarmwert.",
    "Spielt den Alarmton. Stell die Tonhöhe so ein, dass er am lautesten ist.",
    "",
};

UiScreen screen = UiScreen::Main;
int page = 0;          // 0 Ankerkette, 1 NMEA2000
int menuIndex = 0;
Item detailItem = ITEM_ZERO;
uint16_t testToneHz = TONE_DEFAULT_HZ;   // Tonhoehe auf der Seite "Ton testen"

void formatIp(char* out, size_t size, const IPAddress& ip) {
    snprintf(out, size, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}
bool fullRedraw = true;

ChainState chain;
ExhaustMonitor exhaust;
TemperatureSelector exhaustSelector(SOURCE_EXHAUST_GAS, 0, 6000);
DuplicateFilter n2kDuplicates(50);
IPAddress deviceIp;
bool deviceKnown = false;
IPAddress bridgeIp;
bool bridgeKnown = false;

bool wifiConnected = false;
bool udpOpen = false;
uint32_t wifiStartedAt = 0;

char messages[ITEM_COUNT][72];
int lastSendItem = -1;

// Zahleneditor: Ziffer fuer Ziffer mit dem Joystick
struct Editor {
    uint8_t digits[8];
    uint8_t count;
    uint8_t decimals;
    uint8_t cursor;
    const char* unit;
};
Editor editor;

void setEditor(long value, uint8_t count, uint8_t decimals, const char* unit) {
    editor.count = count;
    editor.decimals = decimals;
    editor.cursor = 0;
    editor.unit = unit;
    if (value < 0) value = 0;
    for (int i = count - 1; i >= 0; i--) {
        editor.digits[i] = static_cast<uint8_t>(value % 10);
        value /= 10;
    }
}

long editorValue() {
    long v = 0;
    for (int i = 0; i < editor.count; i++) v = v * 10 + editor.digits[i];
    return v;
}

bool itemHasEditor(Item item) {
    return item == ITEM_LENGTH || item == ITEM_PORT || item == ITEM_DEBOUNCE || item == ITEM_FACTOR ||
           item == ITEM_N2K_PORT || item == ITEM_ALARM;
}

// Einstellungen, die nur das Display betreffen und nichts ans Geraet senden
bool itemIsLocal(Item item) {
    return item == ITEM_PORT || item == ITEM_N2K_PORT || item == ITEM_ALARM || item == ITEM_TONE;
}

// ---------------------------------------------------------------------------
// WLAN und UDP
// ---------------------------------------------------------------------------
void openUdp() {
    chainUdp.stop();
    n2kUdp.stop();
    chainUdp.begin(settings.chainPort);
    n2kUdp.begin(settings.n2kPort);
    udpOpen = true;
}

void startWifi(uint32_t now) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    wifiStartedAt = now;
}

void serviceWifi(uint32_t now) {
    bool connected = WiFi.status() == WL_CONNECTED;
    if (connected && !udpOpen) openUdp();
    if (!connected) {
        if (udpOpen) {
            chainUdp.stop();
            n2kUdp.stop();
            udpOpen = false;
        }
        if (now - wifiStartedAt > WIFI_RETRY_MS) startWifi(now);
    }
    wifiConnected = connected;
}

char rxBuffer[1600];   // die YDWG-02 sendet Pakete bis etwa 1420 Bytes

void handleChainPacket(const char* text, int len, IPAddress sender, uint32_t now) {
    WindlassData data;
    switch (parseWindlass(text, len, data)) {
        case ParseResult::Data:
            chain.onData(data, now);
            deviceIp = sender;
            deviceKnown = true;
            break;
        case ParseResult::OtherSentence: break;
        case ParseResult::Invalid: chain.invalidSentences++; break;
    }
}

void handleN2kPacket(const char* text, int len, IPAddress sender, uint32_t now) {
    bridgeIp = sender;
    bridgeKnown = true;
    if (n2kDuplicates.isDuplicate(text, len, now)) return;
    int start = 0;
    for (int i = 0; i <= len; i++) {
        if (i == len || text[i] == '\n') {
            CanFrame frame;
            float celsius;
            if (parseYdRawLine(text + start, i - start, frame) && exhaustSelector.onFrame(frame, now, celsius)) {
                exhaust.onReading(celsius, now);
            }
            start = i + 1;
        }
    }
}

void pollUdp(uint32_t now) {
    if (!udpOpen) return;
    for (int i = 0; i < 6; i++) {
        if (chainUdp.parsePacket() <= 0) break;
        int len = chainUdp.read(reinterpret_cast<uint8_t*>(rxBuffer), sizeof(rxBuffer) - 1);
        if (len > 0) handleChainPacket(rxBuffer, len, chainUdp.remoteIP(), now);
    }
    for (int i = 0; i < 30; i++) {
        if (n2kUdp.parsePacket() <= 0) break;
        int len = n2kUdp.read(reinterpret_cast<uint8_t*>(rxBuffer), sizeof(rxBuffer) - 1);
        if (len > 0) handleN2kPacket(rxBuffer, len, n2kUdp.remoteIP(), now);
    }
}

void sendCommand(Command command, long value, const char* sentMessage) {
    uint32_t now = millis();
    Item item = detailItem;
    if (!udpOpen || !deviceKnown || !chain.isFresh(now)) {
        snprintf(messages[item], sizeof(messages[0]), "Nicht gesendet: Vom Gerät kommen keine aktuellen Daten.");
        return;
    }
    char sentence[48];
    size_t n = buildCommand(sentence, sizeof(sentence), command, value);
    bool ok = n > 0 && chainUdp.beginPacket(deviceIp, settings.chainPort) &&
              chainUdp.write(reinterpret_cast<const uint8_t*>(sentence), n) == n && chainUdp.endPacket();
    snprintf(messages[item], sizeof(messages[0]), "%s", ok ? sentMessage : "Senden fehlgeschlagen.");
    lastSendItem = item;
    chain.clearRestoredNotice();
}

// ---------------------------------------------------------------------------
// Alarmton
// ---------------------------------------------------------------------------
// Muster: hoch, tief, hoch, tief, Pause. Zusammen 2 s.
const uint8_t TONE_STEPS = 5;
const uint16_t TONE_STEP_MS[TONE_STEPS] = {200, 200, 200, 200, 1200};
const uint8_t TONE_STEP_KIND[TONE_STEPS] = {2, 1, 2, 1, 0};   // 2 hoch, 1 tief, 0 still

bool tonePlaying = false;
uint8_t toneStep = 0;
uint32_t toneStepAt = 0;

bool toneTesting() { return screen == UiScreen::Detail && detailItem == ITEM_TONE; }

void playToneStep() {
    uint16_t hz = toneTesting() ? testToneHz : settings.toneHz;
    switch (TONE_STEP_KIND[toneStep]) {
        case 2: tone(WIO_BUZZER, hz); break;
        case 1: tone(WIO_BUZZER, static_cast<unsigned int>(hz) * TONE_LOW_PERCENT / 100); break;
        default: noTone(WIO_BUZZER); break;
    }
}

void serviceBuzzer(uint32_t now) {
    bool wanted = exhaust.toneWanted() || toneTesting();
    if (!wanted) {
        if (tonePlaying) {
            noTone(WIO_BUZZER);
            tonePlaying = false;
        }
        return;
    }
    if (!tonePlaying) {
        tonePlaying = true;
        toneStep = 0;
        toneStepAt = now;
        playToneStep();
    } else if (now - toneStepAt >= TONE_STEP_MS[toneStep]) {
        toneStep = (toneStep + 1) % TONE_STEPS;
        toneStepAt = now;
        playToneStep();
    }
}

// ---------------------------------------------------------------------------
// Zeichnen
// ---------------------------------------------------------------------------
enum Region {
    R_TITLE, R_LENGTH, R_DIRECTION, R_VCC, R_BOTTOM, R_EXHAUST, R_LIMIT,
    R_ROW0,   // R_ROW0 .. R_ROW0 + 11 fuer Menue- und Infozeilen
    R_DETAIL_CURRENT = R_ROW0 + 12, R_EDITOR, R_MESSAGE, R_STATUS, R_HINT
};

void titleBar(const char* title, bool dots) {
    tft.fillRect(0, 0, 320, 32, pal.bg);
    drawText(title, 8, 4, 4, pal.ink, pal.bg, ALIGN_LEFT);
    if (dots) {
        for (int i = 0; i < 2; i++) {
            int x = 290 + i * 16;
            if (i == page) tft.fillCircle(x, 16, 5, pal.ink);
            else tft.drawCircle(x, 16, 5, pal.ink);
        }
    }
    tft.drawFastHLine(0, 32, 320, pal.rule);
}

const char* chainWarning(uint32_t now, char* buf, size_t size) {
    if (chain.isFresh(now)) return nullptr;
    if (!wifiConnected) snprintf(buf, size, "Keine WLAN-Verbindung.");
    else if (chain.hasData()) snprintf(buf, size, "Keine Daten seit %lu s. Die Werte sind veraltet.",
                                       static_cast<unsigned long>(chain.secondsSinceData(now)));
    else snprintf(buf, size, "Warte auf Daten auf Port %u.", settings.chainPort);
    return buf;
}

const char* n2kWarning(uint32_t now, char* buf, size_t size) {
    if (exhaust.isFresh(now)) return nullptr;
    if (!wifiConnected) snprintf(buf, size, "Keine WLAN-Verbindung.");
    else if (exhaust.hasValue()) snprintf(buf, size, "Keine Abgastemperatur seit %lu s.",
                                          static_cast<unsigned long>(exhaust.secondsSinceData(now)));
    else if (bridgeKnown) snprintf(buf, size, "Die Bridge sendet, aber der Abgassensor meldet sich nicht.");
    else snprintf(buf, size, "Warte auf Daten auf Port %u.", settings.n2kPort);
    return buf;
}

void alarmLine(char* buf, size_t size) {
    char t[16];
    formatOneDecimal(t, sizeof(t), exhaust.celsius());
    snprintf(buf, size, "Abgas %s °C, über %u °C", t, settings.alarmLimit);
}

// Grosser Wert rechtsbuendig, mit kleinerer Schrift, falls er nicht passt
void bigNumber(const char* text, int right, int y, int height, int font, uint16_t color) {
    int f = font;
    if (tft.textWidth(text, f) > right - 4) f = 7;
    int dy = (f == 8) ? 0 : height - 48;
    tft.fillRect(0, y, right + 2, height + 2, pal.bg);
    tft.setTextDatum(TR_DATUM);
    tft.setTextPadding(0);
    tft.setTextColor(color, pal.bg);
    tft.drawString(text, right, y + dy, f);
}

void renderChainPage(uint32_t now) {
    if (fullRedraw) {
        titleBar("Ankerkette", true);
        drawText("m", 268, 38 + 75 - 26, 4, pal.ink, pal.bg, ALIGN_LEFT);
        tft.drawFastHLine(0, 158, 320, pal.rule);
        drawText("Speisung", 8, 170, 2, pal.muted, pal.bg, ALIGN_LEFT);
        tft.drawFastHLine(0, 196, 320, pal.rule);
    }
    bool fresh = chain.isFresh(now);
    char s[48], sig[96];
    if (chain.hasData()) formatOneDecimal(s, sizeof(s), chain.data().lengthM);
    else snprintf(s, sizeof(s), "-");
    snprintf(sig, sizeof(sig), "%s|%d", s, fresh);
    if (changed(R_LENGTH, sig)) bigNumber(s, 262, 38, 75, 8, fresh ? pal.ink : pal.muted);

    int dir = chain.hasData() ? (chain.data().directionUp ? 1 : 2) : 0;
    snprintf(sig, sizeof(sig), "%d|%d", dir, fresh);
    if (changed(R_DIRECTION, sig)) {
        uint16_t c = fresh ? pal.ink : pal.muted;
        tft.fillRect(0, 120, 320, 34, pal.bg);
        if (dir == 1) tft.fillTriangle(8, 148, 32, 148, 20, 126, c);
        if (dir == 2) tft.fillTriangle(8, 126, 32, 126, 20, 148, c);
        drawText(dir == 1 ? "Auf" : dir == 2 ? "Ab" : "-", 42, 124, 4, c, pal.bg, ALIGN_LEFT);
    }

    if (chain.hasData()) {
        char v[16];
        formatOneDecimal(v, sizeof(v), chain.data().vccVolt);
        snprintf(s, sizeof(s), "%s V", v);
    } else {
        snprintf(s, sizeof(s), "-");
    }
    if (changed(R_VCC, s)) {
        tft.fillRect(150, 162, 170, 30, pal.bg);
        drawText(s, 312, 164, 4, pal.ink, pal.bg, ALIGN_RIGHT);
    }

    // Unten: Abgasalarm hat Vorrang vor der Warnung zur Ankerkette
    char l1[72] = "", l2[72] = "", w[72];
    bool warning = false;
    if (exhaust.alarmActive()) {
        alarmLine(l1, sizeof(l1));
        if (!exhaust.muted()) snprintf(l2, sizeof(l2), "Taste rechts: Ton aus");
        warning = true;
    } else if (chainWarning(now, w, sizeof(w))) {
        snprintf(l1, sizeof(l1), "%s", w);
        warning = true;
    }
    snprintf(sig, sizeof(sig), "%s|%s|%d", l1, l2, warning);
    if (changed(R_BOTTOM, sig)) {
        tft.fillRect(0, 197, 320, 43, pal.bg);
        if (warning) {
            tft.fillRect(0, 198, 320, 42, pal.warning);
            if (l2[0]) {
                drawText(l1, 8, 202, 2, pal.onWarning, pal.warning, ALIGN_LEFT);
                drawText(l2, 8, 220, 2, pal.onWarning, pal.warning, ALIGN_LEFT);
            } else {
                drawWrapped(l1, 8, 202, 304, 2, pal.onWarning, pal.warning, 2, 18);
            }
        }
    }
}

void renderN2kPage(uint32_t now) {
    if (fullRedraw) {
        titleBar("NMEA2000", true);
        drawText("Abgastemperatur", 8, 42, 2, pal.muted, pal.bg, ALIGN_LEFT);
        drawText("°C", 268, 62 + 48 - 26, 4, pal.ink, pal.bg, ALIGN_LEFT);
        tft.drawFastHLine(0, 120, 320, pal.rule);
        drawText("Alarmwert", 8, 132, 2, pal.muted, pal.bg, ALIGN_LEFT);
        tft.drawFastHLine(0, 158, 320, pal.rule);
    }
    bool fresh = exhaust.isFresh(now);
    char s[48], sig[120];
    if (exhaust.hasValue()) formatOneDecimal(s, sizeof(s), exhaust.celsius());
    else snprintf(s, sizeof(s), "-");
    snprintf(sig, sizeof(sig), "%s|%d", s, fresh);
    if (changed(R_EXHAUST, sig)) bigNumber(s, 262, 62, 48, 7, fresh ? pal.ink : pal.muted);

    snprintf(s, sizeof(s), "%u °C", settings.alarmLimit);
    if (changed(R_LIMIT, s)) {
        tft.fillRect(150, 124, 170, 30, pal.bg);
        drawText(s, 312, 126, 4, pal.ink, pal.bg, ALIGN_RIGHT);
    }

    char l1[72] = "", l2[72] = "", l3[72] = "", w[72];
    bool warning = false;
    if (exhaust.alarmActive()) {
        char t[16];
        formatOneDecimal(t, sizeof(t), exhaust.celsius());
        snprintf(l1, sizeof(l1), "Abgastemperatur %s °C", t);
        snprintf(l2, sizeof(l2), "über dem Alarmwert %u °C", settings.alarmLimit);
        if (!exhaust.muted()) snprintf(l3, sizeof(l3), "Taste rechts: Ton aus");
        warning = true;
    } else if (n2kWarning(now, w, sizeof(w))) {
        snprintf(l1, sizeof(l1), "%s", w);
        warning = true;
    }
    snprintf(sig, sizeof(sig), "%s|%s|%s|%d", l1, l2, l3, warning);
    if (changed(R_BOTTOM, sig)) {
        tft.fillRect(0, 159, 320, 81, pal.bg);
        if (warning) {
            tft.fillRect(0, 166, 320, 74, pal.warning);
            if (exhaust.alarmActive()) {
                drawText(l1, 8, 172, 2, pal.onWarning, pal.warning, ALIGN_LEFT);
                drawText(l2, 8, 192, 2, pal.onWarning, pal.warning, ALIGN_LEFT);
                if (l3[0]) drawText(l3, 8, 212, 2, pal.onWarning, pal.warning, ALIGN_LEFT);
            } else {
                drawWrapped(l1, 8, 172, 304, 2, pal.onWarning, pal.warning, 3, 20);
            }
        }
    }
}

// Unterste Zeile auf Menue-, Detail- und Infoseiten: Alarm oder Bedienhinweis
void footer(const char* hint) {
    char l[72], sig[96];
    bool alarm = exhaust.alarmActive();
    if (alarm) {
        alarmLine(l, sizeof(l));
        if (!exhaust.muted()) strncat(l, ", rechts: Ton aus", sizeof(l) - strlen(l) - 1);
    } else {
        snprintf(l, sizeof(l), "%s", hint);
    }
    snprintf(sig, sizeof(sig), "%s|%d", l, alarm);
    if (!changed(R_HINT, sig)) return;
    tft.fillRect(0, 218, 320, 22, alarm ? pal.warning : pal.bg);
    drawText(l, 8, 221, 2, alarm ? pal.onWarning : pal.muted, alarm ? pal.warning : pal.bg, ALIGN_LEFT);
}

void renderMenu() {
    if (fullRedraw) titleBar("Einstellungen", false);
    for (int i = 0; i < ITEM_COUNT; i++) {
        bool sel = i == menuIndex;
        char tone[12];
        snprintf(tone, sizeof(tone), "%u Hz", settings.toneHz);
        const char* value = i == ITEM_DARK ? (settings.dark ? "Dunkel" : "Hell") : i == ITEM_TONE ? tone : "";
        char sig[64];
        snprintf(sig, sizeof(sig), "%d|%s", sel, value);
        if (!changed(R_ROW0 + i, sig)) continue;
        int y = 35 + i * 16;
        uint16_t bg = sel ? pal.ink : pal.bg, fg = sel ? pal.bg : pal.ink;
        tft.fillRect(0, y, 320, 16, bg);
        drawText(ITEM_LABELS[i], 12, y, 2, fg, bg, ALIGN_LEFT);
        if (*value) drawText(value, 308, y, 2, fg, bg, ALIGN_RIGHT);
    }
    footer("Drücken: öffnen   Taste links: zurück");
}

void renderInfo() {
    if (fullRedraw) titleBar("Informationen", false);
    char v[8][40];
    const char* labels[8] = {"Systemstarts", "Laufzeit seit Start", "Laufzeit gesamt", "ChainCounter-Adresse",
                             "Bridge-Adresse", "WLAN", "Firmware", "Ungültige Sätze"};
    if (chain.hasData()) {
        snprintf(v[0], sizeof(v[0]), "%lu", static_cast<unsigned long>(chain.data().systemStarts));
        formatDuration(v[1], sizeof(v[1]), chain.data().actualRuntimeS);
        formatDuration(v[2], sizeof(v[2]), chain.data().totalRuntimeS);
    } else {
        snprintf(v[0], sizeof(v[0]), "-");
        snprintf(v[1], sizeof(v[1]), "-");
        snprintf(v[2], sizeof(v[2]), "-");
    }
    if (deviceKnown) formatIp(v[3], sizeof(v[3]), deviceIp);
    else snprintf(v[3], sizeof(v[3]), "-");
    if (bridgeKnown) formatIp(v[4], sizeof(v[4]), bridgeIp);
    else snprintf(v[4], sizeof(v[4]), "-");
    if (wifiConnected) formatIp(v[5], sizeof(v[5]), WiFi.localIP());
    else snprintf(v[5], sizeof(v[5]), "nicht verbunden");
    snprintf(v[6], sizeof(v[6]), "%s", FW_VERSION);
    snprintf(v[7], sizeof(v[7]), "%lu", static_cast<unsigned long>(chain.invalidSentences));
    for (int i = 0; i < 8; i++) {
        if (!changed(R_ROW0 + i, v[i])) continue;
        int y = 38 + i * 22;
        tft.fillRect(0, y, 320, 22, pal.bg);
        drawText(labels[i], 8, y + 3, 2, pal.muted, pal.bg, ALIGN_LEFT);
        drawText(v[i], 312, y + 3, 2, pal.ink, pal.bg, ALIGN_RIGHT);
    }
    footer("Taste links: zurück");
}

void renderDetail(uint32_t now) {
    Item item = detailItem;
    if (fullRedraw) {
        titleBar(ITEM_LABELS[item], false);
        drawWrapped(ITEM_TEXT[item], 8, 38, 304, 2, pal.muted, pal.bg, 3, 17);
    }
    char s[72], sig[96];

    // Aktuelle Laenge bei Zaehler nullen und Laenge setzen
    s[0] = 0;
    if (item == ITEM_ZERO || item == ITEM_LENGTH) {
        char l[16] = "-";
        if (chain.hasData()) formatOneDecimal(l, sizeof(l), chain.data().lengthM);
        snprintf(s, sizeof(s), "Das Gerät meldet %s m.", l);
    }
    if (changed(R_DETAIL_CURRENT, s)) {
        tft.fillRect(0, 92, 320, 18, pal.bg);
        if (s[0]) drawText(s, 8, 92, 2, pal.ink, pal.bg, ALIGN_LEFT);
    }

    // Editor oder Aktion
    if (itemHasEditor(item)) {
        char digits[16];
        int n = 0;
        for (int i = 0; i < editor.count; i++) {
            if (editor.decimals && i == editor.count - editor.decimals) digits[n++] = '.';
            digits[n++] = static_cast<char>('0' + editor.digits[i]);
        }
        digits[n] = 0;
        snprintf(sig, sizeof(sig), "%s|%d", digits, editor.cursor);
        if (changed(R_EDITOR, sig)) {
            tft.fillRect(0, 112, 320, 36, pal.bg);
            int x = 8;
            for (int i = 0; i < editor.count; i++) {
                if (editor.decimals && i == editor.count - editor.decimals) x += drawText(".", x, 114, 4, pal.ink, pal.bg, ALIGN_LEFT);
                char d[2] = {static_cast<char>('0' + editor.digits[i]), 0};
                int w = drawText(d, x, 114, 4, pal.ink, pal.bg, ALIGN_LEFT);
                if (i == editor.cursor) tft.fillRect(x, 142, w, 3, pal.ink);
                x += w + 2;
            }
            drawText(editor.unit, x + 6, 114, 4, pal.ink, pal.bg, ALIGN_LEFT);
        }
    } else if (item == ITEM_TONE) {
        snprintf(sig, sizeof(sig), "%u Hz", testToneHz);
        if (changed(R_EDITOR, sig)) {
            tft.fillRect(0, 112, 320, 36, pal.bg);
            drawText(sig, 8, 114, 4, pal.ink, pal.bg, ALIGN_LEFT);
        }
    } else if (changed(R_EDITOR, "action")) {
        tft.fillRect(0, 112, 320, 36, pal.bg);
    }

    const char* msg = messages[item];
    if (changed(R_MESSAGE, msg)) {
        tft.fillRect(0, 150, 320, 36, pal.bg);
        if (*msg) drawWrapped(msg, 8, 150, 304, 2, pal.ink, pal.bg, 2, 17);
    }

    // Verbindung zum ChainCounter, nur fuer Einstellungen, die ans Geraet gehen
    char w[72] = "";
    int kind = 0;
    if (!itemIsLocal(item)) {
        if (chainWarning(now, w, sizeof(w))) kind = 1;
        else if (chain.receptionRestored()) {
            snprintf(w, sizeof(w), "Empfang wieder hergestellt.");
            kind = 2;
        }
    }
    snprintf(sig, sizeof(sig), "%s|%d", w, kind);
    if (changed(R_STATUS, sig)) {
        uint16_t bg = kind == 1 ? pal.warning : kind == 2 ? pal.rule : pal.bg;
        tft.fillRect(0, 190, 320, 24, bg);
        if (kind) drawText(w, 8, 194, 2, kind == 1 ? pal.onWarning : pal.ink, bg, ALIGN_LEFT);
    }

    if (item == ITEM_TONE) footer("Auf/ab: Tonhöhe   Drücken: speichern");
    else footer(itemIsLocal(item) ? "Drücken: speichern   Taste links: zurück" : "Drücken: senden   Taste links: zurück");
}

void render(uint32_t now) {
    if (fullRedraw) {
        tft.fillScreen(pal.bg);
        invalidateRegions();
    }
    switch (screen) {
        case UiScreen::Main:
            if (page == 0) renderChainPage(now);
            else renderN2kPage(now);
            break;
        case UiScreen::Menu: renderMenu(); break;
        case UiScreen::Detail: renderDetail(now); break;
        case UiScreen::Info: renderInfo(); break;
    }
    fullRedraw = false;
}

// ---------------------------------------------------------------------------
// Bedienung
// ---------------------------------------------------------------------------
enum Key { K_UP, K_DOWN, K_LEFT, K_RIGHT, K_PRESS, K_MENU, K_MUTE, K_COUNT };

struct KeyState {
    uint8_t pin;
    bool repeats;
    bool down;
    uint32_t changedAt;
    uint32_t repeatAt;
};

KeyState keys[K_COUNT] = {
    {WIO_5S_UP, true, false, 0, 0},    {WIO_5S_DOWN, true, false, 0, 0}, {WIO_5S_LEFT, true, false, 0, 0},
    {WIO_5S_RIGHT, true, false, 0, 0}, {WIO_5S_PRESS, false, false, 0, 0}, {KEY_MENU, false, false, 0, 0},
    {KEY_MUTE, false, false, 0, 0},
};

int pollKeys(uint32_t now) {
    for (int i = 0; i < K_COUNT; i++) {
        KeyState& k = keys[i];
        bool raw = digitalRead(k.pin) == LOW;
        if (raw != k.down && now - k.changedAt > 25) {
            k.down = raw;
            k.changedAt = now;
            if (raw) {
                k.repeatAt = now + 450;
                return i;
            }
        } else if (k.down && k.repeats && static_cast<int32_t>(now - k.repeatAt) >= 0) {
            k.repeatAt = now + 120;
            return i;
        }
    }
    return -1;
}

void show(UiScreen s) {
    screen = s;
    fullRedraw = true;
}

void openDetail(Item item) {
    detailItem = item;
    chain.clearRestoredNotice();
    switch (item) {
        case ITEM_LENGTH: {
            long m = chain.hasData() ? static_cast<long>(chain.data().lengthM + 0.5f) : 0;
            if (m < 0) m = 0;
            if (m > LENGTH_MAX) m = LENGTH_MAX;
            setEditor(m, 4, 0, "m");
            break;
        }
        case ITEM_PORT: setEditor(settings.chainPort, 5, 0, ""); break;
        case ITEM_N2K_PORT: setEditor(settings.n2kPort, 5, 0, ""); break;
        case ITEM_DEBOUNCE: setEditor(10, 4, 0, "ms"); break;
        case ITEM_FACTOR: setEditor(4000, 7, 4, "m/U"); break;
        case ITEM_ALARM: setEditor(settings.alarmLimit, 3, 0, "°C"); break;
        case ITEM_TONE: testToneHz = settings.toneHz; break;
        default: break;
    }
    show(UiScreen::Detail);
}

bool inRange(long v, long lo, long hi, const char* unit) {
    if (v >= lo && v <= hi) return true;
    snprintf(messages[detailItem], sizeof(messages[0]), "Erlaubt sind %ld bis %ld%s%s.", lo, hi, *unit ? " " : "",
             unit);
    return false;
}

void submitDetail() {
    long v = editorValue();
    char m[72];
    switch (detailItem) {
        case ITEM_ZERO: sendCommand(Command::ZeroCounter, 0, "Zähler nullen gesendet."); break;
        case ITEM_RESTART: sendCommand(Command::Restart, 0, "Restart gesendet."); break;
        case ITEM_LENGTH:
            if (!inRange(v, 0, LENGTH_MAX, "m")) break;
            snprintf(m, sizeof(m), "Länge %ld m gesendet.", v);
            sendCommand(Command::SetLength, v, m);
            break;
        case ITEM_DEBOUNCE:
            if (!inRange(v, DEBOUNCE_MIN, DEBOUNCE_MAX, "ms")) break;
            snprintf(m, sizeof(m), "Entprellzeit %ld ms gesendet.", v);
            sendCommand(Command::SetDebounce, v, m);
            break;
        case ITEM_FACTOR: {
            if (v < FACTOR_MIN || v > FACTOR_MAX) {
                snprintf(messages[detailItem], sizeof(messages[0]), "Erlaubt sind 0.001 bis 100 m/U.");
                break;
            }
            char f[16];
            formatFactor(f, sizeof(f), v);
            snprintf(m, sizeof(m), "Umrechnungsfaktor %s m/U gesendet.", f);
            sendCommand(Command::SetFactor, v, m);
            break;
        }
        case ITEM_PORT:
        case ITEM_N2K_PORT: {
            if (!inRange(v, PORT_MIN, PORT_MAX, "")) break;
            bool chainPort = detailItem == ITEM_PORT;
            if (v == (chainPort ? settings.n2kPort : settings.chainPort)) {
                snprintf(messages[detailItem], sizeof(messages[0]), "Diesen Port verwendet schon die andere Verbindung.");
                break;
            }
            if (chainPort) {
                settings.chainPort = static_cast<uint16_t>(v);
                chain.forget();
                deviceKnown = false;
            } else {
                settings.n2kPort = static_cast<uint16_t>(v);
                exhaust.forget();
                bridgeKnown = false;
            }
            saveSettings();
            if (udpOpen) openUdp();
            snprintf(messages[detailItem], sizeof(messages[0]), "Port %ld eingestellt.", v);
            break;
        }
        case ITEM_ALARM:
            if (!inRange(v, ALARM_MIN, ALARM_MAX, "°C")) break;
            settings.alarmLimit = static_cast<uint16_t>(v);
            saveSettings();
            snprintf(messages[detailItem], sizeof(messages[0]), "Alarmwert %ld °C gespeichert.", v);
            break;
        case ITEM_TONE:
            settings.toneHz = testToneHz;
            saveSettings();
            snprintf(messages[detailItem], sizeof(messages[0]), "Tonhöhe %u Hz gespeichert.", testToneHz);
            break;
        default: break;
    }
}

void handleKey(int key) {
    if (key == K_MUTE) {
        exhaust.mute();
        return;
    }
    switch (screen) {
        case UiScreen::Main:
            if (key == K_LEFT && page != 0) { page = 0; fullRedraw = true; }
            if (key == K_RIGHT && page != 1) { page = 1; fullRedraw = true; }
            if (key == K_MENU) show(UiScreen::Menu);
            break;
        case UiScreen::Menu:
            if (key == K_UP) menuIndex = (menuIndex + ITEM_COUNT - 1) % ITEM_COUNT;
            if (key == K_DOWN) menuIndex = (menuIndex + 1) % ITEM_COUNT;
            if (key == K_MENU || key == K_LEFT) show(UiScreen::Main);
            if (key == K_PRESS || key == K_RIGHT) {
                if (menuIndex == ITEM_DARK) {
                    settings.dark = !settings.dark;
                    saveSettings();
                    applyPalette();
                    fullRedraw = true;
                } else if (menuIndex == ITEM_INFO) {
                    show(UiScreen::Info);
                } else {
                    openDetail(static_cast<Item>(menuIndex));
                }
            }
            break;
        case UiScreen::Detail:
            if (key == K_MENU) { show(UiScreen::Menu); break; }
            if (key == K_PRESS) { submitDetail(); break; }
            if (detailItem == ITEM_TONE) {
                if (key == K_UP && testToneHz + TONE_STEP_HZ <= TONE_MAX_HZ) testToneHz += TONE_STEP_HZ;
                if (key == K_DOWN && testToneHz - TONE_STEP_HZ >= TONE_MIN_HZ) testToneHz -= TONE_STEP_HZ;
                if (key == K_UP || key == K_DOWN) messages[ITEM_TONE][0] = 0;
                if (key == K_LEFT) show(UiScreen::Menu);
                break;
            }
            if (!itemHasEditor(detailItem)) {
                if (key == K_LEFT) show(UiScreen::Menu);
                break;
            }
            if (key == K_LEFT && editor.cursor > 0) editor.cursor--;
            if (key == K_RIGHT && editor.cursor + 1 < editor.count) editor.cursor++;
            if (key == K_UP) editor.digits[editor.cursor] = (editor.digits[editor.cursor] + 1) % 10;
            if (key == K_DOWN) editor.digits[editor.cursor] = (editor.digits[editor.cursor] + 9) % 10;
            break;
        case UiScreen::Info:
            if (key == K_MENU || key == K_LEFT || key == K_PRESS) show(UiScreen::Menu);
            break;
    }
}

uint32_t lastRender = 0;
bool renderNeeded = true;

}  // namespace

void appSetup() {
    Serial.begin(115200);
    for (int i = 0; i < K_COUNT; i++) pinMode(keys[i].pin, INPUT_PULLUP);
    pinMode(WIO_BUZZER, OUTPUT);
    noTone(WIO_BUZZER);
    loadSettings();
    tft.begin();
    tft.setRotation(3);
    applyPalette();
    for (int i = 0; i < ITEM_COUNT; i++) messages[i][0] = 0;
    startWifi(millis());
    fullRedraw = true;
}

void appLoop() {
    uint32_t now = millis();
    serviceWifi(now);
    pollUdp(now);
    int key = pollKeys(now);
    if (key >= 0) {
        handleKey(key);
        renderNeeded = true;
    }
    chain.onTick(now);
    exhaust.update(now, settings.alarmLimit);
    serviceBuzzer(now);
    if (renderNeeded || now - lastRender >= 250) {
        render(now);
        lastRender = now;
        renderNeeded = false;
    }
}
