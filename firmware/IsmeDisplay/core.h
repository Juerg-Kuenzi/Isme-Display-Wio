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

// ---------- Zustand ----------

class ExhaustMonitor {
public:
    static const uint32_t STALE_AFTER_MS = 6000;
    void onReading(float celsius, uint32_t now);
    bool hasValue() const { return has_; }
    float celsius() const { return celsius_; }
    bool isFresh(uint32_t now) const;
    uint32_t secondsSinceData(uint32_t now) const { return (now - lastAt_) / 1000; }
    // Alarm ueber dem Grenzwert, Ende erst 1 Grad darunter
    void update(uint32_t now, float limit);
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

}  // namespace isme
