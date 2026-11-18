// Kernlogik ohne Arduino-Abhaengigkeit, damit sie auf dem PC getestet werden kann.
// Entspricht der Logik der Android-App Isme (protocol/, ExhaustMonitor, SessionState).
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace isme {

// ---------- ChainCounter, NMEA0183 ----------

struct WindlassData {
    float lengthM;
    bool directionUp;   // ChainDirection 1 = up
    float vccVolt;
    uint32_t systemStarts;
    uint32_t totalRuntimeS;
    uint32_t actualRuntimeS;
};

enum class ParseResult { Data, OtherSentence, Invalid };

uint8_t nmeaChecksum(const char* body, size_t len);
// Schreibt "$<body>*XX" ohne Zeilenende. Liefert die Laenge, 0 wenn der Puffer zu klein ist.
size_t nmeaSentence(char* out, size_t size, const char* body);
ParseResult parseWindlass(const char* text, size_t len, WindlassData& out);

enum class Command { ZeroCounter, SetLength, SetDebounce, SetFactor, Restart };
// value: Meter (SetLength), Millisekunden (SetDebounce), Zehntausendstel m/U (SetFactor)
size_t buildCommand(char* out, size_t size, Command command, long value);

// Erlaubte Bereiche wie in der App
const long LENGTH_MAX = 9999;
const long DEBOUNCE_MIN = 1, DEBOUNCE_MAX = 1000;
const long FACTOR_MIN = 10, FACTOR_MAX = 1000000;   // 0.001 ... 100 m/U in 1e-4
const long PORT_MIN = 1, PORT_MAX = 65535;
const long ALARM_MIN = 1, ALARM_MAX = 800;

// ---------- NMEA2000 ueber YDWG-02, RAW-Format ----------

struct CanFrame {
    uint32_t pgn;
    uint8_t source;
    uint8_t priority;
    uint8_t length;
    uint8_t data[8];
};

uint32_t pgnOf(uint32_t canId);
// Eine Zeile "hh:mm:ss.mmm R 15FD0C74 FF 00 0E 7A BD 04 FF FF"
bool parseYdRawLine(const char* line, size_t len, CanFrame& out);

const uint32_t PGN_TEMPERATURE = 130312;
const uint32_t PGN_TEMPERATURE_EXTENDED = 130316;
const uint8_t SOURCE_EXHAUST_GAS = 14;

struct Temperature {
    uint8_t instance;
    uint8_t source;
    float celsius;
};
bool decodeTemperature(const uint8_t* data, size_t len, Temperature& out);          // 130312
bool decodeTemperatureExtended(const uint8_t* data, size_t len, Temperature& out);  // 130316

// Waehlt die Temperatur einer Quelle. 130316 hat Vorrang, 130312 gilt nur ohne aktuelle 130316.
class TemperatureSelector {
public:
    TemperatureSelector(uint8_t source, uint8_t instance, uint32_t preferExtendedMs);
    bool onFrame(const CanFrame& frame, uint32_t now, float& celsius);
private:
    uint8_t source_, instance_;
    uint32_t preferMs_;
    bool haveExtended_ = false;
    uint32_t extendedAt_ = 0;
};

// Verwirft ein Paket, das innerhalb kurzer Zeit identisch noch einmal ankommt.
class DuplicateFilter {
public:
    explicit DuplicateFilter(uint32_t windowMs) : windowMs_(windowMs) {}
    bool isDuplicate(const char* data, size_t len, uint32_t now);
private:
    uint32_t windowMs_;
    bool have_ = false;
    uint32_t hash_ = 0, len_ = 0, at_ = 0;
};

// ---------- Uhrzeit vom NMEA2000-Bus ----------

const uint32_t PGN_SYSTEM_TIME = 126992;
const int UTC_OFFSET_MIN = -12, UTC_OFFSET_MAX = 14;

// PGN 126992: Tage seit 1.1.1970 und Zeit seit Mitternacht in 0,0001 s (UTC)
bool decodeSystemTime(const uint8_t* data, size_t len, uint32_t& days, uint32_t& tenthMillis);

// Haelt die zuletzt empfangene UTC-Zeit und schreibt sie mit millis() fort.
class BusClock {
public:
    static const uint32_t STALE_AFTER_MS = 10000;
    void onTime(uint32_t days, uint32_t tenthMillis, uint32_t now);
    bool hasTime() const { return has_; }
    bool isFresh(uint32_t now) const { return has_ && now - at_ < STALE_AFTER_MS; }
    uint32_t secondsSinceData(uint32_t now) const { return (now - at_) / 1000; }
    // Millisekunden seit 1.1.1970 UTC
    uint64_t utcMillis(uint32_t now) const { return baseMs_ + (now - at_); }
    void forget() { has_ = false; }
private:
    bool has_ = false;
    uint64_t baseMs_ = 0;
    uint32_t at_ = 0;
};

void formatUtcOffset(char* out, size_t size, int offsetHours);                    // UTC+2, UTC-3, UTC+0
void formatClock(char* out, size_t size, uint64_t utcMillis, int offsetHours);   // 21:56
void formatUtcDateTime(char* out, size_t size, uint64_t utcMillis);              // 2026-11-09 01:37:47

// ---------- Zustand ----------

class ExhaustMonitor {
public:
    static const uint32_t STALE_AFTER_MS = 6000;
    void onReading(float celsius, uint32_t now);
    bool hasValue() const { return has_; }
    float celsius() const { return celsius_; }
    bool isFresh(uint32_t now) const;
    uint32_t secondsSinceData(uint32_t now) const { return (now - lastAt_) / 1000; }
    // Alarm ueber dem Grenzwert, Ende erst 1 Grad darunter. allowed = false
    // (Motor aus oder Schonfrist nach dem Start, siehe EngineGate) unterdrueckt ihn.
    void update(uint32_t now, float limit, bool allowed = true);
    bool alarmActive() const { return alarm_; }
    bool muted() const { return muted_; }
    void mute() { if (alarm_) muted_ = true; }
    bool toneWanted() const { return alarm_ && !muted_; }
    void forget();
private:
    bool has_ = false, alarm_ = false, muted_ = false;
    float celsius_ = 0;
    uint32_t lastAt_ = 0;
};

class ChainState {
public:
    static const uint32_t STALE_AFTER_MS = 3000;
    void onData(const WindlassData& data, uint32_t now);
    bool hasData() const { return has_; }
    const WindlassData& data() const { return data_; }
    bool isFresh(uint32_t now) const;
    uint32_t secondsSinceData(uint32_t now) const { return (now - lastAt_) / 1000; }
    void onTick(uint32_t now);
    bool receptionRestored() const { return restored_; }
    void clearRestoredNotice() { restored_ = false; }
    void forget();
    uint32_t invalidSentences = 0;
private:
    WindlassData data_{};
    bool has_ = false, lost_ = false, restored_ = false;
    uint32_t lastAt_ = 0;
};

// ---------- Formatierung ohne printf-Gleitkomma ----------

void formatOneDecimal(char* out, size_t size, float value);
void formatDuration(char* out, size_t size, uint32_t seconds);   // 01:00:05, 1 d 00:00:00
void formatFactor(char* out, size_t size, long tenThousandths);  // 4000 -> 0.4

// ---------- Motorsignal ----------
//
// Ein nasser Auspuff heizt sich nach dem Abstellen auf die Motorraumtemperatur
// (~64 C) auf und wuerde den Abgasalarm ausloesen. Mit dem Signal "Motor laeuft"
// (Cerbo-Digitaleingang ueber Zuendung) gilt:
//   unbekannt (aus, keine Verbindung, keine Meldung): Ueberwachung wie bisher
//   Motor aus: kein Abgasalarm
//   nach dem Start: Schonfrist GRACE_MS. Alarm nur, wenn die Temperatur ueber
//     den Wert beim Start + GRACE_RISE steigt (Warmstart faellt von ~64 C)
//   danach: Ueberwachung wie bisher
enum class EngineState : uint8_t { Unknown, Off, Running };

class EngineGate {
public:
    static const uint32_t GRACE_MS = 10UL * 60 * 1000;
    static constexpr float GRACE_RISE = 2.0f;
    void onState(EngineState s, uint32_t now);
    void onReading(float celsius);            // jede Abgastemperatur
    EngineState state() const { return state_; }
    bool inGrace(uint32_t now) const;
    bool monitorAllowed(float celsius, uint32_t now) const;

private:
    EngineState state_ = EngineState::Unknown;
    bool grace_ = false, startKnown_ = false;
    uint32_t startAt_ = 0;
    float startCelsius_ = 0, lastCelsius_ = 0;
    bool hasLast_ = false;
};

// ---------- Seiten der Hauptanzeige ----------

// Joystick links/rechts blaettert. Auf der Uhrseite schaltet ein neuer
// Abgasalarm zur Seite NMEA2000 um. Endet der Alarm und hat inzwischen
// niemand geblaettert, geht es zur Uhr zurueck.
enum class Page : uint8_t { Chain = 0, N2k = 1, Clock = 2 };
const int PAGE_COUNT = 3;

class PageSelector {
public:
    Page current() const { return page_; }
    bool left();                        // true, wenn die Seite gewechselt hat
    bool right();
    bool onAlarm(bool alarmActive);     // regelmaessig aufrufen; true bei Seitenwechsel

private:
    Page page_ = Page::Chain;
    bool autoSwitched_ = false;
    bool alarmBefore_ = false;
};

// ---------- Hintergrundbeleuchtung ----------

// Nachtstufen 1 bis 10. Das Auge empfindet Helligkeit etwa logarithmisch,
// deshalb wachsen die Stufen nach oben in groesseren Schritten.
const int NIGHT_LEVEL_MIN = 1, NIGHT_LEVEL_MAX = 10, NIGHT_LEVEL_DEFAULT = 4;
const uint8_t BACKLIGHT_DAY_PERCENT = 100;
// Einschaltdauer der Beleuchtung in Prozent; Werte ausserhalb werden begrenzt.
// Stufe 10 ist voll hell, wie Stufe 10 an den Raymarine-Instrumenten.
uint8_t nightBacklightPercent(int level);

// ---------- Raymarine: Helligkeit einer Displaygruppe (PGN 126720) ----------
//
// Raymarine-Geraete melden eine Helligkeitsaenderung ihrer Gruppe einmalig als
// herstellereigene Nachricht (canboat: "Seatalk1: Display Brightness"), in zwei
// CAN-Rahmen (Fast Packet), z. B. Bruecke (Helm 1) Stufe 3 = 22 %:
//   1DEFFF70 40 09 3B 9F 8C 0C 01 01   Laenge 9, Raymarine, 140/12, Gruppe 1, shared
//   1DEFFF70 41 00 16 00 FF FF FF FF   Befehl 0 = Helligkeit, 22 %
// Die Instrumente senden 10 Stufen: 0, 11, 22, 33, 45, 56, 67, 78, 89, 100 %.
const uint32_t PGN_PROPRIETARY_FAST = 126720;
const uint8_t HELM_GROUP_OFF = 0;      // Raymarine-Gruppe 0 = "None", hier: nicht folgen
const uint8_t HELM_GROUP_MAX = 10;     // 1 Helm 1 ... 5 Mast, 6-10 Group 1-5
const char* helmGroupName(uint8_t group);

class RaymarineBrightness {
public:
    // Liefert true, wenn mit diesem Rahmen eine Helligkeitsmeldung der Gruppe
    // vollstaendig ist; percent ist dann 0 bis 100.
    bool onFrame(const CanFrame& f, uint8_t group, uint8_t& percent);

private:
    struct Pending { bool active; uint8_t source; uint8_t sequence; };
    static const int SLOTS = 4;   // mehrere Absender gleichzeitig
    Pending pending_[SLOTS] = {};
    int next_ = 0;
};

// Raymarine-Prozentwert -> Stufe 1 bis 10 (0 % = Stufe 1, 100 % = Stufe 10)
int raymarineLevel(uint8_t percent);

// ---------- USB-Konsole (WLAN-Zugangsdaten) ----------

const size_t SSID_MAX = 32;
const size_t PASSWORD_MIN = 8, PASSWORD_MAX = 64;

enum class ConsoleCommand { Empty, Help, Status, Ssid, Password, CerboHost, Portal, MqttPassword, EngineInput, Log, Unknown };

// Wertet eine Zeile der USB-Konsole aus. Leerzeichen am Rand werden entfernt
// (die Zeile wird dabei veraendert). arg zeigt bei Ssid/Password auf den Wert
// hinter dem '=', sonst auf die bereinigte Zeile.
ConsoleCommand parseConsoleLine(char* line, const char** arg);

// Setzt die Zeichen der USB-Konsole zu Zeilen zusammen, unabhaengig davon, wie
// das Terminalprogramm sendet (Zeichen fuer Zeichen oder ganze Zeile, CR, LF
// oder CR+LF). Ruecktaste (BS/DEL) loescht das letzte Zeichen, andere
// Steuerzeichen werden ignoriert. Eine zu lange Zeile wird einmal gemeldet und
// bis zum Zeilenende verworfen, damit danach wieder normal weitergeht.
class ConsoleLineReader {
public:
    enum class Event { None, Line, Overflow };
    Event feed(char c);
    char* line() { return buf_; }

private:
    static const size_t SIZE = 128;
    char buf_[SIZE] = {};
    size_t len_ = 0;
    bool overflow_ = false;
};
bool validSsid(const char* s);       // 1 bis 32 Zeichen
bool validPassword(const char* s);   // leer (offenes Netz) oder 8 bis 64 Zeichen

// Zugang zum Cerbo GX (MQTT, Profil "Secured": Port 8883, TLS, Passwort)
const size_t CERBO_HOST_MAX = 39, PORTAL_ID_MAX = 15, MQTT_PASSWORD_MAX = 64;
bool validCerboHost(const char* s);      // 1-39 Zeichen ohne Leerzeichen, z. B. 192.168.5.20
bool validPortalId(const char* s);       // 1-15 Buchstaben/Ziffern, z. B. c0619ab68636
bool validMqttPassword(const char* s);   // 1-64 Zeichen
bool parseEngineInput(const char* s, uint8_t& input);   // "0" (aus) bis "4"

// Wert aus einer Victron-MQTT-Nachricht {"value": 1}. Liefert false bei leerer
// Nachricht (Geraet weg) oder {"value": null}.
bool parseVictronInt(const char* payload, size_t length, long& value);

}  // namespace isme
