// Anzeige, Bedienung, WLAN und Summer fuer das Seeed Wio Terminal.
// Funktional wie die Android-App Isme: Seiten Ankerkette und NMEA2000, Einstellungen, Abgasalarm.
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <rpcWiFi.h>
#include <WiFiUdp.h>
// Darf nur in einer Datei eingebunden werden. Verwendet wird nur
// FlashStorageClass mit fester Adresse (siehe Einstellungen im Flash).
#include <FlashStorage_SAMD.h>

#include <stdio.h>
#include <string.h>

#include "app.h"
#include "config.h"
#include "core.h"

// Die WLAN-Zugangsdaten werden ueber die USB-Konsole gesetzt und im Flash
// gespeichert (ab Firmware 1.5.0). secrets.h ist nur noch optional: Falls
// vorhanden, liefert sie die Startwerte beim allerersten Start und beim Update
// von einer Firmware vor 1.5.0. Die Builds auf GitHub haben keine secrets.h.
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

using namespace isme;

// ---------------------------------------------------------------------------
// Tasten. Die drei Tasten oben sind von links nach rechts C, B, A.
// ---------------------------------------------------------------------------
#ifndef KEY_MENU
#define KEY_MENU WIO_KEY_C   // links: Menue oeffnen und zurueck
#endif
#ifndef KEY_MUTE
#define KEY_MUTE WIO_KEY_B   // Mitte: Alarmton aus (bis 1.5.1 rechts)
#endif
#ifndef KEY_NIGHT
#define KEY_NIGHT WIO_KEY_A  // rechts: Tag/Nacht umschalten
#endif

namespace {

TFT_eSPI tft;
WiFiUDP chainUdp;
WiFiUDP n2kUdp;

// ---------------------------------------------------------------------------
// Einstellungen im Flash
//
// Ab Firmware 1.5.0 liegen die Einstellungen im letzten 8-KB-Block des internen
// Flash (512 KB), ausserhalb des Programms. Ein Update per UF2 schreibt nur den
// Programmbereich, die Einstellungen und WLAN-Zugangsdaten bleiben deshalb
// erhalten. Bis 1.4.0 lagen sie im EEPROM-Ersatz von FlashStorage_SAMD, der Teil
// des Programmabbilds ist und bei jedem Update geloescht wurde.
// ---------------------------------------------------------------------------
const uint32_t SETTINGS_MAGIC_V3 = 0x15E00003;   // Firmware 1.5.x, ohne Nachtstufe
const uint32_t SETTINGS_MAGIC_V4 = 0x15E00004;   // Firmware 1.6.x, ohne Displaygruppe
const uint32_t SETTINGS_MAGIC = 0x15E00005;
const uint8_t DEFAULT_HELM_GROUP = 1;            // Raymarine "Helm 1" = Bruecke
const uint32_t SETTINGS_FLASH_ADDR = 0x7E000;   // letzter Loeschblock (8 KB)

struct Settings {
    uint32_t magic;
    uint16_t chainPort;
    uint16_t n2kPort;
    uint16_t alarmLimit;
    uint8_t dark;                      // 1 = Nacht: rote Darstellung, gedimmt
    int8_t utcOffset;
    uint16_t toneHz;
    char ssid[SSID_MAX + 1];           // nullterminiert
    char password[PASSWORD_MAX + 1];   // nullterminiert, leer = offenes Netz
    uint8_t nightLevel;                // ab 1.6.0: Helligkeit in der Nacht, Stufe 1 bis 10
    uint8_t helmGroup;                 // ab 1.7.0: nachts der Helligkeit dieser Raymarine-Gruppe folgen, 0 = aus
};

Settings settings;
FlashStorageClass<Settings> settingsStore(reinterpret_cast<const void*>(SETTINGS_FLASH_ADDR));

void saveSettings() {
    settingsStore.write(settings);
}

void loadSettings() {
    settingsStore.read(settings);
    if (settings.magic == SETTINGS_MAGIC_V3) {
        // Update von 1.5.x: alle Einstellungen bleiben, neu ist nur die Nachtstufe
        settings.magic = SETTINGS_MAGIC_V4;
        settings.nightLevel = NIGHT_LEVEL_DEFAULT;
    }
    if (settings.magic == SETTINGS_MAGIC_V4) {
        // Update von 1.6.x: alle Einstellungen bleiben, neu ist nur die Displaygruppe
        settings.magic = SETTINGS_MAGIC;
        settings.helmGroup = DEFAULT_HELM_GROUP;
        saveSettings();
    }
    if (settings.magic != SETTINGS_MAGIC) {
        // Erster Start (Block leer oder alter Inhalt): Standardwerte. Die
        // Zugangsdaten kommen aus secrets.h, falls beim Kompilieren vorhanden,
        // sonst bleiben sie leer und werden ueber die USB-Konsole gesetzt.
        settings.magic = SETTINGS_MAGIC;
        settings.chainPort = DEFAULT_CHAIN_PORT;
        settings.n2kPort = DEFAULT_N2K_PORT;
        settings.alarmLimit = DEFAULT_ALARM_LIMIT;
        settings.dark = 0;
        settings.utcOffset = DEFAULT_UTC_OFFSET;
        settings.toneHz = TONE_DEFAULT_HZ;
        snprintf(settings.ssid, sizeof(settings.ssid), "%s", WIFI_SSID);
        snprintf(settings.password, sizeof(settings.password), "%s", WIFI_PASSWORD);
        settings.nightLevel = NIGHT_LEVEL_DEFAULT;
        settings.helmGroup = DEFAULT_HELM_GROUP;
        saveSettings();
    }
    if (settings.nightLevel < NIGHT_LEVEL_MIN || settings.nightLevel > NIGHT_LEVEL_MAX)
        settings.nightLevel = NIGHT_LEVEL_DEFAULT;
    if (settings.helmGroup > HELM_GROUP_MAX) settings.helmGroup = DEFAULT_HELM_GROUP;
    if (settings.utcOffset < UTC_OFFSET_MIN || settings.utcOffset > UTC_OFFSET_MAX) settings.utcOffset = DEFAULT_UTC_OFFSET;
    if (settings.toneHz < TONE_MIN_HZ || settings.toneHz > TONE_MAX_HZ) settings.toneHz = TONE_DEFAULT_HZ;
    settings.ssid[sizeof(settings.ssid) - 1] = 0;
    settings.password[sizeof(settings.password) - 1] = 0;
}

// ---------------------------------------------------------------------------
// Farben wie in der App: tags Seekarte, nachts rote Instrumentenbeleuchtung
// ---------------------------------------------------------------------------
struct Palette {
    uint16_t bg, ink, muted, rule, warning, onWarning;
};
Palette pal;

void applyPalette(bool night) {
    if (night) {
        pal = {TFT_BLACK, tft.color565(0xFF, 0x4B, 0x3A), tft.color565(0xD0, 0x5A, 0x47),
               tft.color565(0x4A, 0x1A, 0x15), tft.color565(0xFF, 0xB0, 0x00), TFT_BLACK};
    } else {
        pal = {tft.color565(0xEE, 0xF3, 0xF1), tft.color565(0x0E, 0x2A, 0x3B), tft.color565(0x56, 0x6B, 0x78),
               tft.color565(0xB5, 0xC6, 0xCC), tft.color565(0xA3, 0x14, 0x6B), TFT_WHITE};
    }
}

// ---------------------------------------------------------------------------
// Hintergrundbeleuchtung (LCD_BACKLIGHT, Pin PC05). Der Pin hat kein PWM. Wie im
// Beispiel von Seeed (Seeed_Arduino_Sketchbook, examples/WioTerminal_BackLight)
// erzeugt ein Timer das PWM-Signal, die Logikzelle CCL gibt es weiter und das
// Ereignissystem schaltet damit den Pin. Abweichend vom Beispiel TC1 mit LUT1
// statt TC0 mit LUT0: tone() belegt auf dem SAMD51 TC0 fuer den Alarmton.
//
// Bei voller Helligkeit (Tag) wird der Pin wie bisher einfach eingeschaltet und
// das PWM abgehaengt. So bleibt der Bildschirm tagsueber hell, auch wenn das
// Dimmen auf einer Hardware nicht funktionieren sollte.
// ---------------------------------------------------------------------------
const uint8_t BACKLIGHT_PWM_TOP = 100;   // Prozent = Vergleichswert
bool backlightPwmReady = false;

void backlightInitPwm() {
    // Takte: TC0/TC1 teilen sich den Takteingang; tone() verwendet ebenfalls GCLK0.
    GCLK->PCHCTRL[TC1_GCLK_ID].reg = GCLK_PCHCTRL_GEN_GCLK0 | GCLK_PCHCTRL_CHEN;
    while (!GCLK->PCHCTRL[TC1_GCLK_ID].bit.CHEN) {}
    GCLK->PCHCTRL[EVSYS_GCLK_ID_0].reg = GCLK_PCHCTRL_GEN_GCLK0 | GCLK_PCHCTRL_CHEN;
    while (!GCLK->PCHCTRL[EVSYS_GCLK_ID_0].bit.CHEN) {}
    GCLK->PCHCTRL[CCL_GCLK_ID].reg = GCLK_PCHCTRL_GEN_GCLK0 | GCLK_PCHCTRL_CHEN;
    while (!GCLK->PCHCTRL[CCL_GCLK_ID].bit.CHEN) {}
    MCLK->APBAMASK.bit.TC1_ = 1;
    MCLK->APBBMASK.bit.EVSYS_ = 1;
    MCLK->APBCMASK.bit.CCL_ = 1;

    // Ereignis: Ausgang LUT1 -> Kanal 0 -> PORT-Ereigniseingang 0, asynchron
    EVSYS->USER[EVSYS_ID_USER_PORT_EV_0].reg = 0 + 1;   // Kanal 0 (Wert = Kanal + 1)
    EVSYS->Channel[0].CHANNEL.reg = EVSYS_CHANNEL_EVGEN(EVSYS_ID_GEN_CCL_LUTOUT_1) |
                                    EVSYS_CHANNEL_PATH_ASYNCHRONOUS;

    // CCL LUT1: Ausgang = Eingang 0 = TC1 WO[0]
    CCL->CTRL.reg = CCL_CTRL_SWRST;   // wie im Seeed-Beispiel, ohne Warten
    CCL->SEQCTRL[0].reg = 0;
    CCL->LUTCTRL[1].reg = CCL_LUTCTRL_TRUTH(0xAA) | CCL_LUTCTRL_LUTEO | CCL_LUTCTRL_INSEL0_TC |
                          CCL_LUTCTRL_ENABLE;
    CCL->CTRL.reg = CCL_CTRL_ENABLE;

    // TC1: 8 Bit, normales PWM, Periode 100 -> Vergleichswert = Prozent
    TC1->COUNT8.CTRLA.reg = TC_CTRLA_SWRST;
    while (TC1->COUNT8.SYNCBUSY.bit.SWRST) {}
    TC1->COUNT8.CTRLA.reg = TC_CTRLA_MODE_COUNT8 | TC_CTRLA_PRESCALER_DIV16 | TC_CTRLA_PRESCSYNC_PRESC;
    TC1->COUNT8.WAVE.reg = TC_WAVE_WAVEGEN_NPWM;
    TC1->COUNT8.PER.reg = BACKLIGHT_PWM_TOP;
    TC1->COUNT8.CC[0].reg = BACKLIGHT_PWM_TOP;
    TC1->COUNT8.CC[1].reg = 0;
    while (TC1->COUNT8.SYNCBUSY.reg) {}
    TC1->COUNT8.CTRLA.bit.ENABLE = 1;
    while (TC1->COUNT8.SYNCBUSY.bit.ENABLE) {}
    backlightPwmReady = true;
}

void setBacklight(uint8_t percent) {
    const uint8_t pin = g_APinDescription[LCD_BACKLIGHT].ulPin;
    if (percent >= BACKLIGHT_DAY_PERCENT) {
        PORT->Group[g_APinDescription[LCD_BACKLIGHT].ulPort].EVCTRL.reg = 0;   // PWM abhaengen
        digitalWrite(LCD_BACKLIGHT, HIGH);
        return;
    }
    if (!backlightPwmReady) backlightInitPwm();
    TC1->COUNT8.CC[0].reg = percent;
    while (TC1->COUNT8.SYNCBUSY.bit.CC0) {}
    // Pin folgt dem Ereignis (Aktion OUT), Ereigniseingang 0 aktiv
    PORT->Group[g_APinDescription[LCD_BACKLIGHT].ulPort].EVCTRL.reg =
        PORT_EVCTRL_PID0(pin) | PORT_EVCTRL_EVACT0(0) | PORT_EVCTRL_PORTEI0;
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
const int REGIONS = 32;
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
    ITEM_NIGHT, ITEM_HELM, ITEM_ZERO, ITEM_LENGTH, ITEM_PORT, ITEM_DEBOUNCE, ITEM_FACTOR, ITEM_RESTART,
    ITEM_N2K_PORT, ITEM_ALARM, ITEM_TONE, ITEM_TIMEZONE, ITEM_INFO, ITEM_COUNT
};

const char* const ITEM_LABELS[ITEM_COUNT] = {
    "Nachthelligkeit", "Helligkeit folgt", "Zähler nullen", "Länge setzen", "Port ChainCounter", "Entprellzeit",
    "Umrechnungsfaktor", "Restart", "Port NMEA2000", "Alarmwert Abgas", "Ton testen",
    "Zeitzone", "Informationen",
};

const char* const ITEM_TEXT[ITEM_COUNT] = {
    "Helligkeit bei Nacht. Die rechte Taste schaltet zwischen Tag (hell) und Nacht (rot, diese Stufe) um.",
    "Nachts übernimmt das Display die Helligkeitsstufe dieser Raymarine-Displaygruppe, sobald sie dort verstellt wird.",
    "Setzt den Kettenzähler auf 0. Das Gerät speichert den Wert spätestens nach 15 Minuten.",
    "Setzt die Kettenlänge in ganzen Metern. Das Gerät speichert den Wert spätestens nach 15 Minuten.",
    "Port, auf dem der ChainCounter sendet. Am Gerät wird er an der Konsole mit port= eingestellt.",
    "Entprellzeit des Impulseingangs. Das Gerät meldet diesen Wert nicht zurück.",
    "Meter pro Umdrehung. Das Gerät meldet diesen Wert nicht zurück.",
    "Das Gerät speichert seine Daten und startet neu. Dafür braucht es Firmware 1.2.0.",
    "Port, auf dem die NMEA2000-Bridge (YDWG-02) im RAW-Format sendet.",
    "Darüber gibt es Alarm mit Ton. Der Alarm endet 1 °C unter dem Alarmwert.",
    "Spielt den Alarmton. Stell die Tonhöhe so ein, dass er am lautesten ist.",
    "Die Ortszeit ist die UTC-Zeit vom GPS (NMEA2000) plus diese Zahl von Stunden.",
    "",
};

UiScreen screen = UiScreen::Main;
PageSelector pages;    // Ankerkette, NMEA2000, Uhrzeit
int menuIndex = 0;
Item detailItem = ITEM_ZERO;
uint16_t testToneHz = TONE_DEFAULT_HZ;   // Tonhoehe auf der Seite "Ton testen"
int testUtcOffset = 0;                   // Zeitzone auf der Seite "Zeitzone"
int testNightLevel = NIGHT_LEVEL_DEFAULT;   // Stufe auf der Seite "Nachthelligkeit"
int testHelmGroup = DEFAULT_HELM_GROUP;     // Gruppe auf der Seite "Helligkeit folgt"

// Zuletzt von der Raymarine-Gruppe gemeldete Stufe, 0 = noch keine Meldung.
// Die Instrumente senden nur bei einer Aenderung; bis dahin gilt die eigene
// Nachtstufe. Das Speichern der eigenen Nachtstufe hebt die Meldung wieder auf.
int helmLevel = 0;
RaymarineBrightness helmBrightness;
void applyDisplayMode();

void formatIp(char* out, size_t size, const IPAddress& ip) {
    snprintf(out, size, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}
bool fullRedraw = true;

ChainState chain;
ExhaustMonitor exhaust;
TemperatureSelector exhaustSelector(SOURCE_EXHAUST_GAS, 0, 6000);
DuplicateFilter n2kDuplicates(50);
BusClock busClock;
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
    return item == ITEM_PORT || item == ITEM_N2K_PORT || item == ITEM_ALARM || item == ITEM_TONE ||
           item == ITEM_TIMEZONE || item == ITEM_NIGHT || item == ITEM_HELM;
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
    wifiStartedAt = now;
    if (settings.ssid[0] == 0) return;   // noch nicht eingerichtet, siehe USB-Konsole
    if (settings.password[0] == 0) WiFi.begin(settings.ssid);
    else WiFi.begin(settings.ssid, settings.password);
}

// Nach ssid= oder pass= an der Konsole melden, ob die Verbindung zustande kommt.
// Nur dann, damit ohne offenes Terminal nichts gesendet wird.
const uint32_t CONSOLE_WIFI_REPORT_MS = 30000;
bool consoleWifiReport = false;
uint32_t consoleWifiReportSince = 0;
void consolePrint(const char* text);

void serviceWifi(uint32_t now) {
    bool connected = WiFi.status() == WL_CONNECTED;
    if (consoleWifiReport) {
        if (connected) {
            char ip[20], line[64];
            formatIp(ip, sizeof(ip), WiFi.localIP());
            snprintf(line, sizeof(line), "WLAN verbunden, IP %s", ip);
            consolePrint(line);
            consoleWifiReport = false;
        } else if (now - consoleWifiReportSince > CONSOLE_WIFI_REPORT_MS) {
            consolePrint("WLAN noch nicht verbunden, es wird weiter versucht. SSID und Passwort pruefen (status).");
            consoleWifiReport = false;
        }
    }
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
            if (parseYdRawLine(text + start, i - start, frame)) {
                uint32_t days, tenthMillis;
                uint8_t percent;
                if (exhaustSelector.onFrame(frame, now, celsius)) exhaust.onReading(celsius, now);
                else if (helmBrightness.onFrame(frame, settings.helmGroup, percent)) {
                    helmLevel = raymarineLevel(percent);
                    applyDisplayMode();   // wirkt sofort, die Anzeige folgt beim naechsten Zeichnen
                }
                else if (frame.pgn == PGN_SYSTEM_TIME && decodeSystemTime(frame.data, frame.length, days, tenthMillis))
                    busClock.onTime(days, tenthMillis, now);
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
// Muster: vier gleiche Toene auf der eingestellten Frequenz, dann Pause. Zusammen 2 s.
const uint8_t TONE_STEPS = 8;
const uint16_t TONE_STEP_MS[TONE_STEPS] = {150, 100, 150, 100, 150, 100, 150, 1100};
const bool TONE_STEP_ON[TONE_STEPS] = {true, false, true, false, true, false, true, false};

bool tonePlaying = false;
bool toneTimerStarted = false;   // erst nach dem ersten tone() darf noTone() aufgerufen werden
uint8_t toneStep = 0;
uint32_t toneStepAt = 0;

bool toneTesting() { return screen == UiScreen::Detail && detailItem == ITEM_TONE; }

// noTone() wartet im Seeed-Kern auf den Timer von tone(). Laeuft dieser noch nie,
// haengt der Aufruf fuer immer. Deshalb vor dem ersten tone() nur den Pin auf LOW setzen.
void stopTone() {
    if (toneTimerStarted) noTone(WIO_BUZZER);
    else digitalWrite(WIO_BUZZER, LOW);
}

void startTone(unsigned int hz) {
    tone(WIO_BUZZER, hz);
    toneTimerStarted = true;
}

void playToneStep() {
    uint16_t hz = toneTesting() ? testToneHz : settings.toneHz;
    if (TONE_STEP_ON[toneStep]) startTone(hz);
    else stopTone();
}

void serviceBuzzer(uint32_t now) {
    bool wanted = exhaust.toneWanted() || toneTesting();
    if (!wanted) {
        if (tonePlaying) {
            stopTone();
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
    R_TITLE, R_LENGTH, R_DIRECTION, R_VCC, R_BOTTOM, R_EXHAUST, R_LIMIT, R_CLOCK_LABEL, R_CLOCK,
    R_BIG_CLOCK,
    R_ROW0,   // R_ROW0 .. R_ROW0 + 11 fuer Menue- und Infozeilen
    R_DETAIL_CURRENT = R_ROW0 + 12, R_EDITOR, R_MESSAGE, R_STATUS, R_HINT
};

void titleBar(const char* title, bool dots) {
    tft.fillRect(0, 0, 320, 32, pal.bg);
    drawText(title, 8, 4, 4, pal.ink, pal.bg, ALIGN_LEFT);
    if (dots) {
        for (int i = 0; i < PAGE_COUNT; i++) {
            int x = 306 - (PAGE_COUNT - 1 - i) * 16;
            if (i == static_cast<int>(pages.current())) tft.fillCircle(x, 16, 5, pal.ink);
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
    else if (bridgeKnown) snprintf(buf, size, "Der Abgassensor meldet sich nicht.");
    else snprintf(buf, size, "Warte auf Daten auf Port %u.", settings.n2kPort);
    return buf;
}

// Nur wenn die Bridge sendet, aber die Zeit fehlt. Sonst sagt n2kWarning() schon, warum nichts kommt.
const char* clockWarning(uint32_t now, char* buf, size_t size) {
    if (busClock.isFresh(now) || !wifiConnected || !bridgeKnown) return nullptr;
    if (busClock.hasTime()) snprintf(buf, size, "Keine Zeit vom GPS seit %lu s.",
                                     static_cast<unsigned long>(busClock.secondsSinceData(now)));
    else snprintf(buf, size, "Keine Zeit vom GPS.");
    return buf;
}

void alarmLine(char* buf, size_t size) {
    char t[16];
    formatOneDecimal(t, sizeof(t), exhaust.celsius());
    snprintf(buf, size, "Abgas %s °C, über %u °C", t, settings.alarmLimit);
}

// Grosser Wert rechtsbuendig, mit kleinerer Schrift, falls er nicht passt.
// Schrift 7 ist 7-Segment und bleibt der Uhrzeit vorbehalten; Messwerte
// verwenden Schrift 8 (75 Pixel) und als Rueckfall Schrift 6 (48 Pixel).
void bigNumber(const char* text, int right, int y, int height, int font, uint16_t color, int fallbackFont = 7) {
    int f = font;
    if (tft.textWidth(text, f) > right - 4) f = fallbackFont;
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
        if (!exhaust.muted()) snprintf(l2, sizeof(l2), "Taste Mitte: Ton aus");
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
    // Ab 1.9.0 ohne Uhrzeit (eigene Seite) und mit grosser Temperatur in der
    // Schrift der Kettenlaenge, damit sie nicht mit der 7-Segment-Uhr verwechselt wird.
    const int VALUE_Y = 84, VALUE_H = 75;
    if (fullRedraw) {
        titleBar("NMEA2000", true);
        drawText("Abgastemperatur", 8, 38, 2, pal.muted, pal.bg, ALIGN_LEFT);
        drawText("°C", 268, VALUE_Y + VALUE_H - 26, 4, pal.ink, pal.bg, ALIGN_LEFT);
        tft.drawFastHLine(0, 188, 320, pal.rule);
    }
    bool fresh = exhaust.isFresh(now);
    char s[48], sig[120];
    if (exhaust.hasValue()) formatOneDecimal(s, sizeof(s), exhaust.celsius());
    else snprintf(s, sizeof(s), "-");
    snprintf(sig, sizeof(sig), "%s|%d", s, fresh);
    if (changed(R_EXHAUST, sig)) bigNumber(s, 262, VALUE_Y, VALUE_H, 8, fresh ? pal.ink : pal.muted, 6);

    snprintf(s, sizeof(s), "Alarm ab %u °C", settings.alarmLimit);
    if (changed(R_LIMIT, s)) {
        tft.fillRect(160, 36, 160, 18, pal.bg);
        drawText(s, 312, 38, 2, pal.muted, pal.bg, ALIGN_RIGHT);
    }

    // Unten: Alarm hat Vorrang, sonst die Warnung zur Abgastemperatur
    char l1[72] = "", l2[72] = "", w[72];
    bool warning = false;
    if (exhaust.alarmActive()) {
        alarmLine(l1, sizeof(l1));
        if (!exhaust.muted()) snprintf(l2, sizeof(l2), "Taste Mitte: Ton aus");
        warning = true;
    } else if (n2kWarning(now, w, sizeof(w))) {
        snprintf(l1, sizeof(l1), "%s", w);
        warning = true;
    }
    snprintf(sig, sizeof(sig), "%s|%s|%d", l1, l2, warning);
    if (changed(R_BOTTOM, sig)) {
        tft.fillRect(0, 189, 320, 51, pal.bg);
        if (warning) {
            tft.fillRect(0, 192, 320, 48, pal.warning);
            drawText(l1, 8, 196, 2, pal.onWarning, pal.warning, ALIGN_LEFT);
            if (l2[0]) drawText(l2, 8, 216, 2, pal.onWarning, pal.warning, ALIGN_LEFT);
        }
    }
}

// Grosse Uhr fuer die Nachtfahrt in 7-Segment-Schrift (Schrift 7, doppelt gross:
// Ziffern 96 Pixel hoch, groesser als die Kettenlaenge). Ein Abgasalarm
// schaltet zur Seite NMEA2000 (PageSelector), Warnungen stehen unten im Balken.
void renderClockPage(uint32_t now) {
    if (fullRedraw) {
        titleBar("Uhrzeit", true);
        tft.drawFastHLine(0, 188, 320, pal.rule);
    }
    char s[48], sig[120];
    char tz[12];
    formatUtcOffset(tz, sizeof(tz), settings.utcOffset);
    snprintf(s, sizeof(s), "Ortszeit %s", tz);
    if (changed(R_CLOCK_LABEL, s)) {
        tft.fillRect(0, 36, 320, 18, pal.bg);
        drawText(s, 8, 38, 2, pal.muted, pal.bg, ALIGN_LEFT);
    }
    bool clockFresh = busClock.isFresh(now);
    if (busClock.hasTime()) formatClock(s, sizeof(s), busClock.utcMillis(now), settings.utcOffset);
    else snprintf(s, sizeof(s), "--:--");
    snprintf(sig, sizeof(sig), "%s|%d", s, clockFresh);
    if (changed(R_BIG_CLOCK, sig)) {
        tft.fillRect(0, 58, 320, 128, pal.bg);
        tft.setTextDatum(TC_DATUM);
        tft.setTextPadding(0);
        tft.setTextColor(clockFresh ? pal.ink : pal.muted, pal.bg);
        tft.setTextSize(2);
        if (tft.textWidth(s, 7) > 316) tft.setTextSize(1);   // passt nicht: einfache Groesse
        int height = tft.fontHeight(7);
        tft.drawString(s, 160, 58 + (128 - height) / 2, 7);
        tft.setTextSize(1);
    }

    char l1[72] = "", l2[72] = "", w[72];
    bool warning = false;
    if (exhaust.alarmActive()) {
        alarmLine(l1, sizeof(l1));
        if (!exhaust.muted()) snprintf(l2, sizeof(l2), "Taste Mitte: Ton aus");
        warning = true;
    } else {
        if (clockWarning(now, w, sizeof(w))) {
            snprintf(l1, sizeof(l1), "%s", w);
            warning = true;
        }
        if (n2kWarning(now, w, sizeof(w))) {
            snprintf(l1[0] ? l2 : l1, sizeof(l1), "%s", w);
            warning = true;
        }
    }
    snprintf(sig, sizeof(sig), "%s|%s|%d", l1, l2, warning);
    if (changed(R_BOTTOM, sig)) {
        tft.fillRect(0, 189, 320, 51, pal.bg);
        if (warning) {
            tft.fillRect(0, 192, 320, 48, pal.warning);
            drawText(l1, 8, 196, 2, pal.onWarning, pal.warning, ALIGN_LEFT);
            if (l2[0]) drawText(l2, 8, 216, 2, pal.onWarning, pal.warning, ALIGN_LEFT);
        }
    }
}

// Unterste Zeile auf Menue-, Detail- und Infoseiten: Alarm oder Bedienhinweis
void footer(const char* hint) {
    char l[72], sig[96];
    bool alarm = exhaust.alarmActive();
    if (alarm) {
        alarmLine(l, sizeof(l));
        if (!exhaust.muted()) strncat(l, ", Mitte: Ton aus", sizeof(l) - strlen(l) - 1);
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
    // Mehr Eintraege als Platz: die Liste scrollt mit der Auswahl
    const int visible = 11;
    int first = menuIndex - (visible - 1);
    if (first < 0) first = 0;
    for (int row = 0; row < visible && first + row < ITEM_COUNT; row++) {
        int i = first + row;
        bool sel = i == menuIndex;
        char value[12] = "";
        if (i == ITEM_NIGHT) {
            if (helmLevel && settings.helmGroup) snprintf(value, sizeof(value), "Brücke %d", helmLevel);
            else snprintf(value, sizeof(value), "Stufe %u", settings.nightLevel);
        }
        if (i == ITEM_HELM) snprintf(value, sizeof(value), "%s", helmGroupName(settings.helmGroup));
        if (i == ITEM_TONE) snprintf(value, sizeof(value), "%u Hz", settings.toneHz);
        if (i == ITEM_TIMEZONE) formatUtcOffset(value, sizeof(value), settings.utcOffset);
        char sig[64];
        snprintf(sig, sizeof(sig), "%d|%d|%s", i, sel, value);
        if (!changed(R_ROW0 + row, sig)) continue;
        int y = 35 + row * 16;
        uint16_t bg = sel ? pal.ink : pal.bg, fg = sel ? pal.bg : pal.ink;
        tft.fillRect(0, y, 320, 16, bg);
        drawText(ITEM_LABELS[i], 12, y, 2, fg, bg, ALIGN_LEFT);
        if (*value) drawText(value, 308, y, 2, fg, bg, ALIGN_RIGHT);
    }
    footer("Drücken: öffnen   Taste links: zurück");
}

void renderInfo() {
    if (fullRedraw) titleBar("Informationen", false);
    char v[9][40];
    const char* labels[9] = {"Systemstarts", "Laufzeit seit Start", "Laufzeit gesamt", "ChainCounter-Adresse",
                             "Bridge-Adresse", "WLAN", "UTC vom GPS", "Firmware", "Ungültige Sätze"};
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
    else if (settings.ssid[0] == 0) snprintf(v[5], sizeof(v[5]), "nicht eingerichtet");
    else snprintf(v[5], sizeof(v[5]), "nicht verbunden");
    uint32_t now = millis();
    if (busClock.hasTime()) formatUtcDateTime(v[6], sizeof(v[6]), busClock.utcMillis(now));
    else snprintf(v[6], sizeof(v[6]), "-");
    snprintf(v[7], sizeof(v[7]), "%s", FW_VERSION);
    snprintf(v[8], sizeof(v[8]), "%lu", static_cast<unsigned long>(chain.invalidSentences));
    for (int i = 0; i < 9; i++) {
        if (!changed(R_ROW0 + i, v[i])) continue;
        int y = 37 + i * 20;
        tft.fillRect(0, y, 320, 20, pal.bg);
        drawText(labels[i], 8, y + 2, 2, pal.muted, pal.bg, ALIGN_LEFT);
        drawText(v[i], 312, y + 2, 2, pal.ink, pal.bg, ALIGN_RIGHT);
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
    } else if (item == ITEM_TIMEZONE) {
        char tz[12], l[32] = "";
        formatUtcOffset(tz, sizeof(tz), testUtcOffset);
        if (busClock.hasTime()) {
            char t[12];
            formatClock(t, sizeof(t), busClock.utcMillis(now), testUtcOffset);
            snprintf(l, sizeof(l), "Ortszeit %s", t);
        }
        snprintf(sig, sizeof(sig), "%s|%s", tz, l);
        if (changed(R_EDITOR, sig)) {
            tft.fillRect(0, 112, 320, 36, pal.bg);
            int w = drawText(tz, 8, 114, 4, pal.ink, pal.bg, ALIGN_LEFT);
            if (l[0]) drawText(l, 8 + w + 24, 114, 4, pal.muted, pal.bg, ALIGN_LEFT);
        }
    } else if (item == ITEM_NIGHT) {
        snprintf(sig, sizeof(sig), "Stufe %d von %d", testNightLevel, NIGHT_LEVEL_MAX);
        if (changed(R_EDITOR, sig)) {
            tft.fillRect(0, 112, 320, 36, pal.bg);
            drawText(sig, 8, 114, 4, pal.ink, pal.bg, ALIGN_LEFT);
            for (int i = 0; i < NIGHT_LEVEL_MAX; i++) {   // Balken, eine Zelle pro Stufe
                int x = 196 + i * 12;
                if (i < testNightLevel) tft.fillRect(x, 118, 10, 20, pal.ink);
                else tft.drawRect(x, 118, 10, 20, pal.muted);
            }
        }
    } else if (item == ITEM_HELM) {
        char state[24] = "";
        if (testHelmGroup == settings.helmGroup && settings.helmGroup != HELM_GROUP_OFF) {
            if (helmLevel) snprintf(state, sizeof(state), "zuletzt Stufe %d", helmLevel);
            else snprintf(state, sizeof(state), "noch keine Meldung");
        }
        snprintf(sig, sizeof(sig), "%s|%s", helmGroupName(testHelmGroup), state);
        if (changed(R_EDITOR, sig)) {
            tft.fillRect(0, 112, 320, 36, pal.bg);
            int w = drawText(helmGroupName(testHelmGroup), 8, 114, 4, pal.ink, pal.bg, ALIGN_LEFT);
            if (state[0]) drawText(state, 8 + w + 16, 120, 2, pal.muted, pal.bg, ALIGN_LEFT);
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
    else if (item == ITEM_TIMEZONE) footer("Auf/ab: Zeitzone   Drücken: speichern");
    else if (item == ITEM_NIGHT) footer("Auf/ab: Stufe   Drücken: speichern");
    else if (item == ITEM_HELM) footer("Auf/ab: Gruppe   Drücken: speichern");
    else footer(itemIsLocal(item) ? "Drücken: speichern   Taste links: zurück" : "Drücken: senden   Taste links: zurück");
}

void render(uint32_t now) {
    if (fullRedraw) {
        tft.fillScreen(pal.bg);
        invalidateRegions();
    }
    switch (screen) {
        case UiScreen::Main:
            switch (pages.current()) {
                case Page::Chain: renderChainPage(now); break;
                case Page::N2k: renderN2kPage(now); break;
                case Page::Clock: renderClockPage(now); break;
            }
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
enum Key { K_UP, K_DOWN, K_LEFT, K_RIGHT, K_PRESS, K_MENU, K_MUTE, K_NIGHT, K_COUNT };

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
    {KEY_MUTE, false, false, 0, 0},    {KEY_NIGHT, false, false, 0, 0},
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

// Tag: helle Farben, volle Helligkeit. Nacht: rote Farben, eingestellte Stufe.
// Auf der Seite "Nachthelligkeit" wird die gewaehlte Stufe sofort vorgefuehrt.
// displayModeSet: erst nach dem ersten Aufruf sind Farben und Helligkeit
// wirklich gesetzt. Ohne diesen Merker blieb beim Start im Tag-Modus die
// Farbtabelle leer (alles schwarz), weil shownNight schon "Tag" anzeigte.
bool displayModeSet = false;
bool shownNight = false;
uint8_t shownPercent = 0;

void applyDisplayMode() {
    bool preview = screen == UiScreen::Detail && detailItem == ITEM_NIGHT;
    bool night = settings.dark || preview;
    int level = preview ? testNightLevel
                        : (helmLevel && settings.helmGroup ? helmLevel : settings.nightLevel);
    uint8_t percent = night ? nightBacklightPercent(level) : BACKLIGHT_DAY_PERCENT;
    bool first = !displayModeSet;
    displayModeSet = true;
    if (first || night != shownNight) {
        applyPalette(night);
        shownNight = night;
        fullRedraw = true;
    }
    if (first || percent != shownPercent) {
        setBacklight(percent);
        shownPercent = percent;
    }
}

void show(UiScreen s) {
    screen = s;
    fullRedraw = true;
    applyDisplayMode();
}

void toggleNight() {
    settings.dark = !settings.dark;
    saveSettings();
    applyDisplayMode();
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
        case ITEM_TIMEZONE: testUtcOffset = settings.utcOffset; break;
        case ITEM_NIGHT: testNightLevel = helmLevel && settings.helmGroup ? helmLevel : settings.nightLevel; break;
        case ITEM_HELM: testHelmGroup = settings.helmGroup; break;
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
                busClock.forget();
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
        case ITEM_NIGHT:
            settings.nightLevel = static_cast<uint8_t>(testNightLevel);
            helmLevel = 0;   // eigene Einstellung gilt bis zur naechsten Meldung der Gruppe
            saveSettings();
            snprintf(messages[detailItem], sizeof(messages[0]), "Nachtstufe %d gespeichert.", testNightLevel);
            break;
        case ITEM_HELM:
            if (testHelmGroup != settings.helmGroup) helmLevel = 0;
            settings.helmGroup = static_cast<uint8_t>(testHelmGroup);
            saveSettings();
            applyDisplayMode();
            if (settings.helmGroup == HELM_GROUP_OFF)
                snprintf(messages[detailItem], sizeof(messages[0]), "Folgen ausgeschaltet.");
            else
                snprintf(messages[detailItem], sizeof(messages[0]), "Folgt nachts %s.", helmGroupName(settings.helmGroup));
            break;
        case ITEM_TIMEZONE: {
            settings.utcOffset = static_cast<int8_t>(testUtcOffset);
            saveSettings();
            char tz[12];
            formatUtcOffset(tz, sizeof(tz), testUtcOffset);
            snprintf(messages[detailItem], sizeof(messages[0]), "Zeitzone %s gespeichert.", tz);
            break;
        }
        default: break;
    }
}

void handleKey(int key) {
    if (key == K_MUTE) {
        exhaust.mute();
        return;
    }
    if (key == K_NIGHT) {
        toggleNight();
        return;
    }
    switch (screen) {
        case UiScreen::Main:
            if (key == K_LEFT && pages.left()) fullRedraw = true;
            if (key == K_RIGHT && pages.right()) fullRedraw = true;
            if (key == K_MENU) show(UiScreen::Menu);
            break;
        case UiScreen::Menu:
            if (key == K_UP) menuIndex = (menuIndex + ITEM_COUNT - 1) % ITEM_COUNT;
            if (key == K_DOWN) menuIndex = (menuIndex + 1) % ITEM_COUNT;
            if (key == K_MENU || key == K_LEFT) show(UiScreen::Main);
            if (key == K_PRESS || key == K_RIGHT) {
                if (menuIndex == ITEM_INFO) {
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
            if (detailItem == ITEM_NIGHT) {
                if (key == K_UP && testNightLevel < NIGHT_LEVEL_MAX) testNightLevel++;
                if (key == K_DOWN && testNightLevel > NIGHT_LEVEL_MIN) testNightLevel--;
                if (key == K_UP || key == K_DOWN) {
                    messages[ITEM_NIGHT][0] = 0;
                    applyDisplayMode();
                }
                if (key == K_LEFT) show(UiScreen::Menu);
                break;
            }
            if (detailItem == ITEM_HELM) {
                if (key == K_UP && testHelmGroup < HELM_GROUP_MAX) testHelmGroup++;
                if (key == K_DOWN && testHelmGroup > HELM_GROUP_OFF) testHelmGroup--;
                if (key == K_UP || key == K_DOWN) messages[ITEM_HELM][0] = 0;
                if (key == K_LEFT) show(UiScreen::Menu);
                break;
            }
            if (detailItem == ITEM_TIMEZONE) {
                if (key == K_UP && testUtcOffset < UTC_OFFSET_MAX) testUtcOffset++;
                if (key == K_DOWN && testUtcOffset > UTC_OFFSET_MIN) testUtcOffset--;
                if (key == K_UP || key == K_DOWN) messages[ITEM_TIMEZONE][0] = 0;
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

// ---------------------------------------------------------------------------
// USB-Konsole, 115200 Baud: WLAN-Zugangsdaten setzen und Zustand anzeigen
// ---------------------------------------------------------------------------
void consolePrint(const char* text) { Serial.println(text); }

// Sofort anzeigen, bevor Speichern und Neuverbinden das Programm kurz aufhalten
void consolePrintNow(const char* text) {
    Serial.println(text);
    Serial.flush();
}

void reconnectFromConsole() {
    consolePrintNow("Verbinde neu ...");
    uint32_t now = millis();
    startWifi(now);
    wifiConnected = false;
    consoleWifiReport = true;
    consoleWifiReportSince = now;
}

void consoleStatus() {
    char line[120];
    snprintf(line, sizeof(line), "Firmware        : %s", FW_VERSION); consolePrint(line);
    snprintf(line, sizeof(line), "WLAN SSID       : %s", settings.ssid[0] ? settings.ssid : "(nicht eingerichtet)");
    consolePrint(line);
    if (settings.password[0]) {
        snprintf(line, sizeof(line), "WLAN Passwort   : gesetzt (%u Zeichen)",
                 static_cast<unsigned>(strlen(settings.password)));
    } else {
        snprintf(line, sizeof(line), "WLAN Passwort   : leer (offenes Netz)");
    }
    consolePrint(line);
    if (wifiConnected) {
        char ip[20];
        formatIp(ip, sizeof(ip), WiFi.localIP());
        snprintf(line, sizeof(line), "WLAN            : verbunden, %s", ip);
    } else {
        snprintf(line, sizeof(line), "WLAN            : nicht verbunden");
    }
    consolePrint(line);
    if (settings.dark) snprintf(line, sizeof(line), "Anzeige         : Nacht, Stufe %u", settings.nightLevel);
    else snprintf(line, sizeof(line), "Anzeige         : Tag (Nachtstufe %u)", settings.nightLevel);
    consolePrint(line);
    if (settings.helmGroup == HELM_GROUP_OFF) snprintf(line, sizeof(line), "Helligkeit folgt: aus");
    else if (helmLevel) snprintf(line, sizeof(line), "Helligkeit folgt: %s, zuletzt Stufe %d", helmGroupName(settings.helmGroup), helmLevel);
    else snprintf(line, sizeof(line), "Helligkeit folgt: %s, noch keine Meldung", helmGroupName(settings.helmGroup));
    consolePrint(line);
    snprintf(line, sizeof(line), "Port ChainCounter: %u, Port NMEA2000: %u",
             static_cast<unsigned>(settings.chainPort), static_cast<unsigned>(settings.n2kPort));
    consolePrint(line);
}

void handleConsoleLine(char* text) {
    const char* arg;
    char line[120];
    switch (parseConsoleLine(text, &arg)) {
        case ConsoleCommand::Empty:
            break;
        case ConsoleCommand::Help:
            consolePrint("Befehle:");
            consolePrint("  status       Zustand anzeigen");
            consolePrint("  ssid=<name>  WLAN-SSID setzen (1-32 Zeichen), gespeichert, verbindet neu");
            consolePrint("  pass=<pw>    WLAN-Passwort setzen (leer oder 8-64 Zeichen), gespeichert, verbindet neu");
            consolePrint("Nach ssid= oder pass= meldet das Display, ob die Verbindung zustande kommt.");
            break;
        case ConsoleCommand::Status:
            consoleStatus();
            break;
        case ConsoleCommand::Ssid:
            if (!validSsid(arg)) { consolePrint("FEHLER: SSID muss 1-32 Zeichen haben"); break; }
            snprintf(line, sizeof(line), "SSID '%s' wird gespeichert ...", arg);
            consolePrintNow(line);
            snprintf(settings.ssid, sizeof(settings.ssid), "%s", arg);
            saveSettings();
            consolePrintNow("OK: SSID gespeichert.");
            reconnectFromConsole();
            break;
        case ConsoleCommand::Password:
            if (!validPassword(arg)) { consolePrint("FEHLER: Passwort muss leer sein oder 8-64 Zeichen haben"); break; }
            consolePrintNow("Passwort wird gespeichert ...");
            snprintf(settings.password, sizeof(settings.password), "%s", arg);
            saveSettings();
            consolePrintNow("OK: Passwort gespeichert.");
            reconnectFromConsole();
            break;
        case ConsoleCommand::Unknown:
            snprintf(line, sizeof(line), "FEHLER: unbekannter Befehl '%.60s' (help)", arg);
            consolePrint(line);
            break;
    }
}

ConsoleLineReader consoleReader;

void serviceConsole() {
    while (Serial.available()) {
        switch (consoleReader.feed(static_cast<char>(Serial.read()))) {
            case ConsoleLineReader::Event::Line: handleConsoleLine(consoleReader.line()); break;
            case ConsoleLineReader::Event::Overflow: consolePrint("FEHLER: Zeile zu lang"); break;
            case ConsoleLineReader::Event::None: break;
        }
    }
}

}  // namespace

void appSetup() {
    // Ohne DTR-Pruefung: Der USB-Treiber verwirft sonst alle Ausgaben, solange das
    // Terminalprogramm DTR nicht setzt (z. B. HTerm in der Grundeinstellung).
    // Ausgaben gibt es nur als Antwort auf Konsolenbefehle, ohne offenes Terminal
    // wird also nichts gesendet.
    Serial.beginWithoutDTR(115200);
    for (int i = 0; i < K_COUNT; i++) pinMode(keys[i].pin, INPUT_PULLUP);
    pinMode(WIO_BUZZER, OUTPUT);
    digitalWrite(WIO_BUZZER, LOW);   // nicht noTone(), siehe stopTone()
    loadSettings();
    tft.begin();   // schaltet die Beleuchtung ein
    tft.setRotation(3);
    applyDisplayMode();
    for (int i = 0; i < ITEM_COUNT; i++) messages[i][0] = 0;
    startWifi(millis());
    fullRedraw = true;
}

void appLoop() {
    uint32_t now = millis();
    serviceConsole();
    serviceWifi(now);
    pollUdp(now);
    int key = pollKeys(now);
    if (key >= 0) {
        handleKey(key);
        renderNeeded = true;
    }
    chain.onTick(now);
    exhaust.update(now, settings.alarmLimit);
    if (pages.onAlarm(exhaust.alarmActive()) && screen == UiScreen::Main) {
        fullRedraw = true;
        renderNeeded = true;
    }
    serviceBuzzer(now);
    if (renderNeeded || now - lastRender >= 250) {
        render(now);
        lastRender = now;
        renderNeeded = false;
    }
}
