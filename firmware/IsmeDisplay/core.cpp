#include "core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace isme {

namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

bool isSpace(char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; }

// Zahl wie -?\d+(\.\d+)?
bool isDecimal(const char* s) {
    if (*s == '-') s++;
    if (!isDigit(*s)) return false;
    while (isDigit(*s)) s++;
    if (*s == '.') {
        s++;
        if (!isDigit(*s)) return false;
        while (isDigit(*s)) s++;
    }
    return *s == 0;
}

bool isInteger(const char* s) {
    if (*s == '-') s++;
    if (!isDigit(*s)) return false;
    while (isDigit(*s)) s++;
    return *s == 0;
}

// Die Firmware gibt u32 mit %d aus, negative Werte werden deshalb zurueckgerechnet.
bool parseU32(const char* s, uint32_t& out) {
    if (!isInteger(s) || strlen(s) > 11) return false;
    long long v = strtoll(s, nullptr, 10);
    if (v < -2147483648LL || v > 4294967295LL) return false;
    out = static_cast<uint32_t>(v & 0xFFFFFFFFLL);
    return true;
}

uint32_t readLe(const uint8_t* d, int n) {
    uint32_t v = 0;
    for (int i = n - 1; i >= 0; i--) v = (v << 8) | d[i];
    return v;
}

}  // namespace

uint8_t nmeaChecksum(const char* body, size_t len) {
    uint8_t c = 0;
    for (size_t i = 0; i < len; i++) c ^= static_cast<uint8_t>(body[i]);
    return c;
}

size_t nmeaSentence(char* out, size_t size, const char* body) {
    size_t n = strlen(body);
    if (size < n + 5) return 0;
    int w = snprintf(out, size, "$%s*%02X", body, nmeaChecksum(body, n));
    return w > 0 ? static_cast<size_t>(w) : 0;
}

ParseResult parseWindlass(const char* text, size_t len, WindlassData& out) {
    while (len > 0 && isSpace(text[0])) { text++; len--; }
    while (len > 0 && isSpace(text[len - 1])) len--;
    char buf[128];
    if (len == 0 || len >= sizeof(buf) || text[0] != '$') return ParseResult::Invalid;
    memcpy(buf, text, len);
    buf[len] = 0;
    char* star = strrchr(buf, '*');
    if (!star || strlen(star + 1) != 2) return ParseResult::Invalid;
    int hi = hexValue(star[1]), lo = hexValue(star[2]);
    if (hi < 0 || lo < 0) return ParseResult::Invalid;
    if (nmeaChecksum(buf + 1, star - (buf + 1)) != hi * 16 + lo) return ParseResult::Invalid;
    *star = 0;

    char* fields[10];
    int count = 0;
    char* p = buf + 1;
    fields[count++] = p;
    while (*p) {
        if (*p == ',') {
            *p = 0;
            if (count == 10) return ParseResult::Invalid;
            fields[count++] = p + 1;
        }
        p++;
    }
    // Datensatz des ChainCounter (ab FW 1.3.0; frueher $ISLWND)
    if (strcmp(fields[0], "PXISWND") != 0) return ParseResult::OtherSentence;
    if (count != 7) return ParseResult::Invalid;

    WindlassData d;
    if (!isDecimal(fields[1])) return ParseResult::Invalid;
    d.lengthM = static_cast<float>(strtod(fields[1], nullptr));
    if (strcmp(fields[2], "0") == 0) d.directionUp = false;
    else if (strcmp(fields[2], "1") == 0) d.directionUp = true;
    else return ParseResult::Invalid;
    if (!isDecimal(fields[3])) return ParseResult::Invalid;
    d.vccVolt = static_cast<float>(strtod(fields[3], nullptr));
    if (!parseU32(fields[4], d.systemStarts)) return ParseResult::Invalid;
    if (!parseU32(fields[5], d.totalRuntimeS)) return ParseResult::Invalid;
    if (!parseU32(fields[6], d.actualRuntimeS)) return ParseResult::Invalid;
    out = d;
    return ParseResult::Data;
}

// Kommandos $PXISWNS (ChainCounter ab FW 1.3.0)
size_t buildCommand(char* out, size_t size, Command command, long value) {
    char body[40];
    switch (command) {
        case Command::ZeroCounter: snprintf(body, sizeof(body), "PXISWNS,C"); break;
        case Command::Restart: snprintf(body, sizeof(body), "PXISWNS,R"); break;
        case Command::SetLength: snprintf(body, sizeof(body), "PXISWNS,C,%ld", value); break;
        case Command::SetDebounce: snprintf(body, sizeof(body), "PXISWNS,S,%ld", value); break;
        case Command::SetFactor: {
            char f[16];
            formatFactor(f, sizeof(f), value);
            snprintf(body, sizeof(body), "PXISWNS,K,%s", f);
            break;
        }
    }
    return nmeaSentence(out, size, body);
}

uint32_t pgnOf(uint32_t canId) {
    uint32_t pf = (canId >> 16) & 0xFF;
    uint32_t pgn = (canId >> 8) & 0x3FFFF;
    return pf < 240 ? (pgn & 0x3FF00) : pgn;
}

bool parseYdRawLine(const char* line, size_t len, CanFrame& out) {
    while (len > 0 && isSpace(line[0])) { line++; len--; }
    while (len > 0 && isSpace(line[len - 1])) len--;
    // hh:mm:ss.mmm R XXXXXXXX
    if (len < 23) return false;
    static const char pattern[] = "dd:dd:dd.ddd";
    for (int i = 0; i < 12; i++) {
        if (pattern[i] == 'd' ? !isDigit(line[i]) : line[i] != pattern[i]) return false;
    }
    if (line[12] != ' ' || (line[13] != 'R' && line[13] != 'T') || line[14] != ' ') return false;
    uint32_t id = 0;
    for (int i = 15; i < 23; i++) {
        int h = hexValue(line[i]);
        if (h < 0) return false;
        id = (id << 4) | static_cast<uint32_t>(h);
    }
    size_t pos = 23;
    uint8_t n = 0;
    while (pos < len) {
        if (n == 8 || pos + 3 > len || line[pos] != ' ') return false;
        int h = hexValue(line[pos + 1]), l = hexValue(line[pos + 2]);
        if (h < 0 || l < 0) return false;
        out.data[n++] = static_cast<uint8_t>(h * 16 + l);
        pos += 3;
    }
    out.length = n;
    out.pgn = pgnOf(id);
    out.source = id & 0xFF;
    out.priority = (id >> 26) & 0x7;
    return true;
}

bool decodeTemperature(const uint8_t* d, size_t len, Temperature& out) {
    if (len < 5) return false;
    uint32_t raw = readLe(d + 3, 2);
    if (raw >= 0xFFFD) return false;
    out.instance = d[1];
    out.source = d[2];
    out.celsius = static_cast<float>(raw * 0.01 - 273.15);
    return true;
}

bool decodeTemperatureExtended(const uint8_t* d, size_t len, Temperature& out) {
    if (len < 6) return false;
    uint32_t raw = readLe(d + 3, 3);
    if (raw >= 0xFFFFFD) return false;
    out.instance = d[1];
    out.source = d[2];
    out.celsius = static_cast<float>(raw * 0.001 - 273.15);
    return true;
}

TemperatureSelector::TemperatureSelector(uint8_t source, uint8_t instance, uint32_t preferExtendedMs)
    : source_(source), instance_(instance), preferMs_(preferExtendedMs) {}

bool TemperatureSelector::onFrame(const CanFrame& f, uint32_t now, float& celsius) {
    Temperature t;
    bool extended = f.pgn == PGN_TEMPERATURE_EXTENDED;
    if (extended) {
        if (!decodeTemperatureExtended(f.data, f.length, t)) return false;
    } else if (f.pgn == PGN_TEMPERATURE) {
        if (!decodeTemperature(f.data, f.length, t)) return false;
    } else {
        return false;
    }
    if (t.source != source_ || t.instance != instance_) return false;
    if (extended) {
        haveExtended_ = true;
        extendedAt_ = now;
    } else if (haveExtended_ && now - extendedAt_ < preferMs_) {
        return false;
    }
    celsius = t.celsius;
    return true;
}

bool decodeSystemTime(const uint8_t* d, size_t len, uint32_t& days, uint32_t& tenthMillis) {
    if (len < 8) return false;
    uint32_t dd = readLe(d + 2, 2), t = readLe(d + 4, 4);
    if (dd >= 0xFFFD || t >= 864000000u) return false;   // nicht verfuegbar oder ausserhalb eines Tages
    days = dd;
    tenthMillis = t;
    return true;
}

void BusClock::onTime(uint32_t days, uint32_t tenthMillis, uint32_t now) {
    baseMs_ = static_cast<uint64_t>(days) * 86400000ULL + tenthMillis / 10;
    at_ = now;
    has_ = true;
}

void formatUtcOffset(char* out, size_t size, int offset) {
    snprintf(out, size, "UTC%c%d", offset < 0 ? '-' : '+', offset < 0 ? -offset : offset);
}

void formatClock(char* out, size_t size, uint64_t utcMillis, int offset) {
    int64_t s = static_cast<int64_t>(utcMillis / 1000) + static_cast<int64_t>(offset) * 3600;
    int64_t day = ((s % 86400) + 86400) % 86400;
    snprintf(out, size, "%02d:%02d", static_cast<int>(day / 3600), static_cast<int>(day % 3600 / 60));
}

void formatUtcDateTime(char* out, size_t size, uint64_t utcMillis) {
    uint64_t s = utcMillis / 1000;
    // Tage seit 1970 in ein Kalenderdatum (Algorithmus von Howard Hinnant)
    int64_t z = static_cast<int64_t>(s / 86400) + 719468;
    int64_t era = z / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    int month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    int year = static_cast<int>(yoe + era * 400 + (month <= 2 ? 1 : 0));
    unsigned sec = static_cast<unsigned>(s % 86400);
    snprintf(out, size, "%04d-%02d-%02d %02u:%02u:%02u", year, month, day, sec / 3600, sec % 3600 / 60, sec % 60);
}

bool DuplicateFilter::isDuplicate(const char* data, size_t len, uint32_t now) {
    uint32_t h = 2166136261u;   // FNV-1a
    for (size_t i = 0; i < len; i++) h = (h ^ static_cast<uint8_t>(data[i])) * 16777619u;
    bool dup = have_ && h == hash_ && len == len_ && now - at_ <= windowMs_;
    have_ = true;
    hash_ = h;
    len_ = static_cast<uint32_t>(len);
    at_ = now;
    return dup;
}

void ExhaustMonitor::onReading(float c, uint32_t now) {
    celsius_ = c;
    lastAt_ = now;
    has_ = true;
}

bool ExhaustMonitor::isFresh(uint32_t now) const { return has_ && now - lastAt_ < STALE_AFTER_MS; }

void ExhaustMonitor::update(uint32_t now, float limit, bool allowed) {
    bool active = allowed && has_ && isFresh(now) &&
                  (celsius_ > limit || (alarm_ && celsius_ > limit - 1.0f));
    if (!active) muted_ = false;
    alarm_ = active;
}

void ExhaustMonitor::forget() {
    has_ = alarm_ = muted_ = false;
}

void ChainState::onData(const WindlassData& d, uint32_t now) {
    data_ = d;
    has_ = true;
    lastAt_ = now;
    if (lost_) {
        lost_ = false;
        restored_ = true;
    }
}

bool ChainState::isFresh(uint32_t now) const { return has_ && now - lastAt_ < STALE_AFTER_MS; }

void ChainState::onTick(uint32_t now) {
    if (has_ && !isFresh(now)) {
        lost_ = true;
        restored_ = false;
    }
}

void ChainState::forget() {
    has_ = lost_ = restored_ = false;
    invalidSentences = 0;
}

void formatOneDecimal(char* out, size_t size, float value) {
    long tenths = static_cast<long>(value * 10.0f + (value < 0 ? -0.5f : 0.5f));
    bool negative = tenths < 0;
    if (negative) tenths = -tenths;
    snprintf(out, size, "%s%ld.%ld", negative ? "-" : "", tenths / 10, tenths % 10);
}

void formatDuration(char* out, size_t size, uint32_t s) {
    unsigned long days = s / 86400UL;
    unsigned long h = s % 86400UL / 3600UL, m = s % 3600UL / 60UL, sec = s % 60UL;
    if (days > 0) snprintf(out, size, "%lu d %02lu:%02lu:%02lu", days, h, m, sec);
    else snprintf(out, size, "%02lu:%02lu:%02lu", h, m, sec);
}

void formatFactor(char* out, size_t size, long v) {
    long whole = v / 10000, frac = v % 10000;
    if (frac == 0) {
        snprintf(out, size, "%ld", whole);
        return;
    }
    char digits[8];
    snprintf(digits, sizeof(digits), "%04ld", frac);
    int n = 4;
    while (n > 0 && digits[n - 1] == '0') n--;
    digits[n] = 0;
    snprintf(out, size, "%ld.%s", whole, digits);
}

// ---------- Motorsignal ----------

void EngineGate::onState(EngineState s, uint32_t now) {
    if (s == EngineState::Running && state_ == EngineState::Off) {
        // Start erkannt: Schonfrist ab der aktuellen Temperatur
        grace_ = true;
        startAt_ = now;
        startKnown_ = hasLast_;
        startCelsius_ = lastCelsius_;
    }
    if (s != EngineState::Running) grace_ = false;
    state_ = s;
}

void EngineGate::onReading(float celsius) {
    lastCelsius_ = celsius;
    hasLast_ = true;
    if (grace_ && !startKnown_) {
        startKnown_ = true;
        startCelsius_ = celsius;
    }
}

bool EngineGate::inGrace(uint32_t now) const {
    return state_ == EngineState::Running && grace_ && now - startAt_ < GRACE_MS;
}

bool EngineGate::monitorAllowed(float celsius, uint32_t now) const {
    switch (state_) {
        case EngineState::Unknown: return true;
        case EngineState::Off: return false;
        case EngineState::Running:
            if (inGrace(now)) return startKnown_ && celsius > startCelsius_ + GRACE_RISE;
            return true;
    }
    return true;
}

// ---------- Seiten der Hauptanzeige ----------

bool PageSelector::left() {
    if (page_ == Page::Chain) return false;
    page_ = static_cast<Page>(static_cast<int>(page_) - 1);
    autoSwitched_ = false;
    return true;
}

bool PageSelector::right() {
    if (static_cast<int>(page_) == PAGE_COUNT - 1) return false;
    page_ = static_cast<Page>(static_cast<int>(page_) + 1);
    autoSwitched_ = false;
    return true;
}

bool PageSelector::onAlarm(bool alarmActive) {
    bool rising = alarmActive && !alarmBefore_;
    alarmBefore_ = alarmActive;
    if (rising && page_ == Page::Clock) {
        page_ = Page::N2k;
        autoSwitched_ = true;
        return true;
    }
    if (!alarmActive && autoSwitched_) {
        autoSwitched_ = false;
        if (page_ == Page::N2k) {
            page_ = Page::Clock;
            return true;
        }
    }
    return false;
}

// ---------- Hintergrundbeleuchtung ----------

uint8_t nightBacklightPercent(int level) {
    // etwa gleiche Verhaeltnisse von Stufe zu Stufe (x 1.67), Stufe 10 voll hell
    static const uint8_t PERCENT[NIGHT_LEVEL_MAX] = {1, 2, 3, 5, 8, 13, 22, 36, 60, 100};
    if (level < NIGHT_LEVEL_MIN) level = NIGHT_LEVEL_MIN;
    if (level > NIGHT_LEVEL_MAX) level = NIGHT_LEVEL_MAX;
    return PERCENT[level - 1];
}

// ---------- Raymarine: Helligkeit einer Displaygruppe ----------

const char* helmGroupName(uint8_t group) {
    static const char* const NAMES[HELM_GROUP_MAX + 1] = {
        "Aus", "Helm 1", "Helm 2", "Cockpit", "Flybridge", "Mast",
        "Gruppe 1", "Gruppe 2", "Gruppe 3", "Gruppe 4", "Gruppe 5"};
    return group <= HELM_GROUP_MAX ? NAMES[group] : "?";
}

bool RaymarineBrightness::onFrame(const CanFrame& f, uint8_t group, uint8_t& percent) {
    if (f.pgn != PGN_PROPRIETARY_FAST || f.length != 8 || group == HELM_GROUP_OFF) return false;
    uint8_t sequence = f.data[0] >> 5, index = f.data[0] & 0x1F;
    if (index == 0) {
        // Rahmen 0: Laenge 9, Raymarine (1851, Marine), Display 140/12, Gruppe
        bool match = f.data[1] == 9 && f.data[2] == 0x3B && f.data[3] == 0x9F &&
                     f.data[4] == 0x8C && f.data[5] == 0x0C && f.data[6] == group;
        for (Pending& p : pending_)
            if (p.active && p.source == f.source) p.active = false;
        if (match) {
            pending_[next_] = {true, f.source, sequence};
            next_ = (next_ + 1) % SLOTS;
        }
        return false;
    }
    if (index != 1) return false;
    for (Pending& p : pending_) {
        if (!p.active || p.source != f.source || p.sequence != sequence) continue;
        p.active = false;
        // Rahmen 1: Befehl (0 = Helligkeit, 1 = Farbe), Wert
        if (f.data[1] != 0 || f.data[2] > 100) return false;
        percent = f.data[2];
        return true;
    }
    return false;
}

int raymarineLevel(uint8_t percent) {
    if (percent > 100) percent = 100;
    return (percent * 9 + 50) / 100 + 1;
}

// ---------- USB-Konsole ----------

ConsoleCommand parseConsoleLine(char* line, const char** arg) {
    char* s = line;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    char* e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    *arg = s;
    if (*s == 0) return ConsoleCommand::Empty;
    if (strcmp(s, "help") == 0) return ConsoleCommand::Help;
    if (strcmp(s, "status") == 0) return ConsoleCommand::Status;
    if (strncmp(s, "ssid=", 5) == 0) { *arg = s + 5; return ConsoleCommand::Ssid; }
    if (strncmp(s, "pass=", 5) == 0) { *arg = s + 5; return ConsoleCommand::Password; }
    if (strncmp(s, "cerbo=", 6) == 0) { *arg = s + 6; return ConsoleCommand::CerboHost; }
    if (strncmp(s, "portal=", 7) == 0) { *arg = s + 7; return ConsoleCommand::Portal; }
    if (strncmp(s, "mqttpass=", 9) == 0) { *arg = s + 9; return ConsoleCommand::MqttPassword; }
    if (strncmp(s, "motor=", 6) == 0) { *arg = s + 6; return ConsoleCommand::EngineInput; }
    return ConsoleCommand::Unknown;
}

ConsoleLineReader::Event ConsoleLineReader::feed(char c) {
    if (c == '\r' || c == '\n') {
        if (overflow_) {
            overflow_ = false;
            len_ = 0;
            return Event::None;
        }
        if (len_ == 0) return Event::None;   // Leerzeile, z. B. LF nach CR
        buf_[len_] = 0;
        len_ = 0;
        return Event::Line;
    }
    if (overflow_) return Event::None;
    if (c == '\b' || c == 0x7F) {
        if (len_ > 0) len_--;
        return Event::None;
    }
    if (static_cast<unsigned char>(c) < 0x20) return Event::None;
    if (len_ < SIZE - 1) {
        buf_[len_++] = c;
        return Event::None;
    }
    overflow_ = true;
    return Event::Overflow;
}

bool validSsid(const char* s) {
    size_t n = strlen(s);
    return n >= 1 && n <= SSID_MAX;
}

bool validCerboHost(const char* s) {
    size_t n = strlen(s);
    if (n < 1 || n > CERBO_HOST_MAX) return false;
    for (const char* p = s; *p; p++)
        if (*p == ' ' || static_cast<unsigned char>(*p) < 0x21) return false;
    return true;
}

bool validPortalId(const char* s) {
    size_t n = strlen(s);
    if (n < 1 || n > PORTAL_ID_MAX) return false;
    for (const char* p = s; *p; p++)
        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))) return false;
    return true;
}

bool validMqttPassword(const char* s) {
    size_t n = strlen(s);
    return n >= 1 && n <= MQTT_PASSWORD_MAX;
}

bool parseEngineInput(const char* s, uint8_t& input) {
    if (s[0] < '0' || s[0] > '4' || s[1] != 0) return false;
    input = static_cast<uint8_t>(s[0] - '0');
    return true;
}

bool parseVictronInt(const char* payload, size_t length, long& value) {
    char buf[64];
    if (length == 0 || length >= sizeof(buf)) return false;
    memcpy(buf, payload, length);
    buf[length] = 0;
    const char* p = strstr(buf, "\"value\"");
    if (!p) return false;
    p = strchr(p + 7, ':');
    if (!p) return false;
    p++;
    while (*p == ' ') p++;
    if (*p == 'n') return false;   // null
    char* end;
    double v = strtod(p, &end);
    if (end == p) return false;
    value = static_cast<long>(v);
    return true;
}

bool validPassword(const char* s) {
    size_t n = strlen(s);
    return n == 0 || (n >= PASSWORD_MIN && n <= PASSWORD_MAX);
}

}  // namespace isme
