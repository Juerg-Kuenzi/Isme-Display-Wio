// PC-Tests der Kernlogik. Die Testdaten sind dieselben wie in den Tests der Android-App.
#include "../../firmware/IsmeDisplay/core.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

using namespace isme;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FEHLER %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)
#define NEAR(a, b, eps) CHECK(fabs((double)(a) - (double)(b)) <= (eps))

static ParseResult parse(const char* s, WindlassData& d) { return parseWindlass(s, strlen(s), d); }

static void sentence(char* out, const char* body) { nmeaSentence(out, 128, body); }

static void testWindlass() {
    WindlassData d;
    CHECK(parse("$ISLWND,12.4,0,24.1,17,86400,3605*07", d) == ParseResult::Data);
    NEAR(d.lengthM, 12.4, 1e-4); CHECK(!d.directionUp); NEAR(d.vccVolt, 24.1, 1e-4);
    CHECK(d.systemStarts == 17 && d.totalRuntimeS == 86400 && d.actualRuntimeS == 3605);

    char s[128];
    sentence(s, "ISLWND,-1.2,1,12.0,1,0,0");
    strcat(s, "\r\n");
    CHECK(parse(s, d) == ParseResult::Data); NEAR(d.lengthM, -1.2, 1e-4); CHECK(d.directionUp);

    char lower[64] = "$ISLWND,1.0,0,12.0,1,0,0*";
    char cs[4]; snprintf(cs, sizeof(cs), "%02x", nmeaChecksum("ISLWND,1.0,0,12.0,1,0,0", 23)); strcat(lower, cs);
    CHECK(parse(lower, d) == ParseResult::Data);

    sentence(s, "ISLWND,1.0,0,12.0,1,-1,0"); CHECK(parse(s, d) == ParseResult::Data); CHECK(d.totalRuntimeS == 4294967295u);
    sentence(s, "ISLWND,1.0,0,12.0,1,4294967295,0"); CHECK(parse(s, d) == ParseResult::Data); CHECK(d.totalRuntimeS == 4294967295u);

    const char* bad[] = {
        "ISLWND,12.4,0,24.1,17,86400,3605*07", "$ISLWND,12.4,0,24.1,17,86400,3605",
        "$ISLWND,12.4,0,24.1,17,86400,3605*08", "$ISLWND,12.4,0,24.1,17,86400,3605*7",
    };
    for (const char* b : bad) CHECK(parse(b, d) == ParseResult::Invalid);
    const char* badBodies[] = {
        "ISLWND,12.4,0,24.1,17,86400", "ISLWND,12.4,0,24.1,17,86400,3605,1", "ISLWND,12.4,2,24.1,17,86400,3605",
        "ISLWND,NaN,0,24.1,17,86400,3605", "ISLWND,1e3,0,24.1,17,86400,3605", "ISLWND,12.4,0,24.1,1.5,86400,3605",
        "ISLWND,12.4,0,24.1,17,4294967296,3605", "ISLWND,,0,24.1,17,86400,3605",
    };
    for (const char* b : badBodies) { sentence(s, b); CHECK(parse(s, d) == ParseResult::Invalid); }

    char c[48];
    buildCommand(c, sizeof(c), Command::ZeroCounter, 0); CHECK(parse(c, d) == ParseResult::OtherSentence);
}

static void testCommands() {
    char c[48], e[48];
    buildCommand(c, sizeof(c), Command::SetFactor, 4200); CHECK(strcmp(c, "$ISLWNS,K,0.42*4F") == 0);
    buildCommand(c, sizeof(c), Command::ZeroCounter, 0); CHECK(strcmp(c, "$ISLWNS,C*73") == 0);
    buildCommand(c, sizeof(c), Command::Restart, 0); CHECK(strcmp(c, "$ISLWNS,R*62") == 0);
    buildCommand(c, sizeof(c), Command::SetLength, 25); sentence(e, "ISLWNS,C,25"); CHECK(strcmp(c, e) == 0);
    buildCommand(c, sizeof(c), Command::SetDebounce, 10); sentence(e, "ISLWNS,S,10"); CHECK(strcmp(c, e) == 0);
    char f[16];
    formatFactor(f, sizeof(f), 4000); CHECK(strcmp(f, "0.4") == 0);
    formatFactor(f, sizeof(f), 10); CHECK(strcmp(f, "0.001") == 0);
    formatFactor(f, sizeof(f), 1000000); CHECK(strcmp(f, "100") == 0);
    formatFactor(f, sizeof(f), 12345); CHECK(strcmp(f, "1.2345") == 0);
}

// Frames aus dem Mitschnitt der YDWG-02 (Abgassensor Quelle 14, Adresse 0x74)
static const char* EXT = "01:36:48.770 R 15FD0C74 FF 00 0E 7A BD 04 FF FF";
static const char* STD = "01:36:48.769 R 15FD0874 FF 00 0E 59 79 FF FF FF";
static const char* SEA = "01:36:47.650 R 15FD086E 00 00 00 F7 76 FF FF FF";

static CanFrame frame(const char* s) { CanFrame f{}; CHECK(parseYdRawLine(s, strlen(s), f)); return f; }

static void testYdRaw() {
    CanFrame f = frame(EXT);
    CHECK(f.pgn == 130316 && f.source == 0x74 && f.priority == 5 && f.length == 8 && f.data[2] == 0x0E);
    CHECK(pgnOf(0x1DEFFF01) == 126720);
    const char* bad[] = { "", "garbage", "01:36:48.770 R 15FD0C7 FF", "01:36:48.770 X 15FD0C74 FF",
                          "01:36:48.770 R 15FD0C74 FF 00 0E 7A BD 04 FF FF 00" };
    for (const char* b : bad) { CanFrame x; CHECK(!parseYdRawLine(b, strlen(b), x)); }
    char crlf[80]; snprintf(crlf, sizeof(crlf), "%s\r\n", STD); CanFrame g; CHECK(parseYdRawLine(crlf, strlen(crlf), g) && g.pgn == 130312);

    Temperature t;
    CHECK(decodeTemperatureExtended(f.data, f.length, t)); CHECK(t.source == 14 && t.instance == 0); NEAR(t.celsius, 37.5, 0.001);
    CanFrame s = frame(STD); CHECK(decodeTemperature(s.data, s.length, t)); NEAR(t.celsius, 37.5, 0.006);
    CanFrame w = frame(SEA); CHECK(decodeTemperature(w.data, w.length, t)); NEAR(t.celsius, 31.4, 0.006);
    const uint8_t na[8] = {0xFF, 0, 14, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    CHECK(!decodeTemperatureExtended(na, 8, t)); CHECK(!decodeTemperature(na, 5, t));

    TemperatureSelector sel(SOURCE_EXHAUST_GAS, 0, 6000);
    float c = 0;
    CHECK(!sel.onFrame(w, 0, c));
    CHECK(sel.onFrame(s, 0, c)); NEAR(c, 37.5, 0.006);
    CHECK(sel.onFrame(f, 1, c));
    CHECK(!sel.onFrame(s, 2000, c));
    CHECK(sel.onFrame(s, 7001, c));

    DuplicateFilter dup(50);
    CHECK(!dup.isDuplicate("a", 1, 0)); CHECK(dup.isDuplicate("a", 1, 1));
    CHECK(!dup.isDuplicate("a", 1, 200)); CHECK(!dup.isDuplicate("b", 1, 201));
}

static void testExhaust() {
    ExhaustMonitor m;
    m.onReading(50.0f, 0); m.update(0, 50); CHECK(!m.alarmActive());
    m.onReading(50.1f, 100); m.update(100, 50); CHECK(m.alarmActive() && m.toneWanted());
    m.onReading(49.5f, 200); m.update(200, 50); CHECK(m.alarmActive());
    m.onReading(49.0f, 300); m.update(300, 50); CHECK(!m.alarmActive());

    ExhaustMonitor n;
    n.mute(); n.onReading(60, 0); n.update(0, 50); CHECK(!n.muted());
    n.mute(); CHECK(!n.toneWanted() && n.alarmActive());
    n.onReading(40, 100); n.update(100, 50); n.onReading(60, 200); n.update(200, 50); CHECK(n.toneWanted());

    ExhaustMonitor o;
    o.onReading(60, 0); o.update(0, 50); CHECK(o.isFresh(5999));
    o.update(6000, 50); CHECK(!o.isFresh(6000) && !o.alarmActive()); CHECK(o.secondsSinceData(6000) == 6);

    // Ueberlauf von millis() nach rund 49 Tagen
    ExhaustMonitor p;
    p.onReading(60, 0xFFFFFF00u); CHECK(p.isFresh(0x00000100u));
}

static void testChain() {
    WindlassData d{1, false, 24, 1, 1, 1};
    ChainState s;
    CHECK(!s.isFresh(0));
    s.onData(d, 1000); CHECK(s.isFresh(3999)); CHECK(!s.isFresh(4000)); CHECK(s.secondsSinceData(5000) == 4);
    ChainState r;
    r.onData(d, 0); r.onTick(1000); r.onData(d, 1000); CHECK(!r.receptionRestored());
    r.onTick(5000); r.onData(d, 9000); CHECK(r.receptionRestored());
    r.onTick(9500); CHECK(r.receptionRestored()); r.clearRestoredNotice(); CHECK(!r.receptionRestored());
    ChainState f;
    f.onData(d, 0); f.onTick(5000); f.forget(); f.onData(d, 6000); CHECK(!f.receptionRestored());
}

static void testClock() {
    // Frame aus dem Mitschnitt: GPS an Adresse 0x96, 9.11.2026 01:37:47.799 UTC
    CanFrame f = frame("01:36:47.799 R 0DF01096 FF FF 1E 51 E6 5A 7F 03");
    CHECK(f.pgn == PGN_SYSTEM_TIME && f.source == 0x96);
    uint32_t days = 0, t = 0;
    CHECK(decodeSystemTime(f.data, f.length, days, t));
    CHECK(days == 20766 && t == 58677990u);
    const uint8_t na[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    CHECK(!decodeSystemTime(na, 8, days, t));

    BusClock c;
    CHECK(!c.hasTime() && !c.isFresh(0));
    c.onTime(20766, 58677990u, 1000);
    char b[32];
    formatUtcDateTime(b, sizeof(b), c.utcMillis(1000)); CHECK(strcmp(b, "2026-11-09 01:37:47") == 0);
    formatClock(b, sizeof(b), c.utcMillis(1000), 0); CHECK(strcmp(b, "01:37") == 0);
    formatClock(b, sizeof(b), c.utcMillis(1000), -3); CHECK(strcmp(b, "22:37") == 0);    // Vortag
    formatClock(b, sizeof(b), c.utcMillis(1000), 14); CHECK(strcmp(b, "15:37") == 0);
    formatClock(b, sizeof(b), c.utcMillis(1000 + 13 * 60000), 2); CHECK(strcmp(b, "03:50") == 0);   // fortgeschrieben
    CHECK(c.isFresh(10999) && !c.isFresh(11000));
    formatUtcDateTime(b, sizeof(b), 951782400000ULL); CHECK(strcmp(b, "2000-02-29 00:00:00") == 0);
    formatUtcDateTime(b, sizeof(b), 0); CHECK(strcmp(b, "1970-01-01 00:00:00") == 0);
    formatUtcOffset(b, sizeof(b), 2); CHECK(strcmp(b, "UTC+2") == 0);
    formatUtcOffset(b, sizeof(b), -12); CHECK(strcmp(b, "UTC-12") == 0);
    formatUtcOffset(b, sizeof(b), 0); CHECK(strcmp(b, "UTC+0") == 0);
}

static void testFormat() {
    char b[24];
    formatOneDecimal(b, sizeof(b), 12.4f); CHECK(strcmp(b, "12.4") == 0);
    formatOneDecimal(b, sizeof(b), -1.25f); CHECK(strcmp(b, "-1.3") == 0);
    formatOneDecimal(b, sizeof(b), -0.04f); CHECK(strcmp(b, "0.0") == 0);
    formatOneDecimal(b, sizeof(b), 37.75f); CHECK(strcmp(b, "37.8") == 0);
    formatDuration(b, sizeof(b), 0); CHECK(strcmp(b, "00:00:00") == 0);
    formatDuration(b, sizeof(b), 3605); CHECK(strcmp(b, "01:00:05") == 0);
    formatDuration(b, sizeof(b), 86400); CHECK(strcmp(b, "1 d 00:00:00") == 0);
}

int main() {
    testWindlass(); testCommands(); testYdRaw(); testExhaust(); testChain(); testClock(); testFormat();
    if (failures) { printf("%d Fehler\n", failures); return 1; }
    printf("ALLE TESTS OK\n");
    return 0;
}
