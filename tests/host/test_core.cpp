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
    // Alter Satz (bis ChainCounter FW 1.2.1) wird nicht mehr ausgewertet
    CHECK(parse("$ISLWND,12.4,0,24.1,17,86400,3605*07", d) == ParseResult::OtherSentence);

    CHECK(parse("$PXISWND,12.4,0,24.1,17,86400,3605*43", d) == ParseResult::Data);
    NEAR(d.lengthM, 12.4, 1e-4); CHECK(!d.directionUp); NEAR(d.vccVolt, 24.1, 1e-4);
    CHECK(d.systemStarts == 17 && d.totalRuntimeS == 86400 && d.actualRuntimeS == 3605);
    CHECK(parse("$PXISWND,12.4,0,24.1,17,86400,3605*07", d) == ParseResult::Invalid);
    CHECK(parse("$PXISWNS,C*37", d) == ParseResult::OtherSentence);

    char s[128];
    sentence(s, "PXISWND,-1.2,1,12.0,1,0,0");
    strcat(s, "\r\n");
    CHECK(parse(s, d) == ParseResult::Data); NEAR(d.lengthM, -1.2, 1e-4); CHECK(d.directionUp);

    char lower[64] = "$PXISWND,1.0,0,12.0,1,0,0*";
    char cs[4]; snprintf(cs, sizeof(cs), "%02x", nmeaChecksum("PXISWND,1.0,0,12.0,1,0,0", 24)); strcat(lower, cs);
    CHECK(parse(lower, d) == ParseResult::Data);

    sentence(s, "PXISWND,1.0,0,12.0,1,-1,0"); CHECK(parse(s, d) == ParseResult::Data); CHECK(d.totalRuntimeS == 4294967295u);
    sentence(s, "PXISWND,1.0,0,12.0,1,4294967295,0"); CHECK(parse(s, d) == ParseResult::Data); CHECK(d.totalRuntimeS == 4294967295u);

    const char* bad[] = {
        "PXISWND,12.4,0,24.1,17,86400,3605*07", "$PXISWND,12.4,0,24.1,17,86400,3605",
        "$PXISWND,12.4,0,24.1,17,86400,3605*08", "$PXISWND,12.4,0,24.1,17,86400,3605*7",
    };
    for (const char* b : bad) CHECK(parse(b, d) == ParseResult::Invalid);
    const char* badBodies[] = {
        "PXISWND,12.4,0,24.1,17,86400", "PXISWND,12.4,0,24.1,17,86400,3605,1", "PXISWND,12.4,2,24.1,17,86400,3605",
        "PXISWND,NaN,0,24.1,17,86400,3605", "PXISWND,1e3,0,24.1,17,86400,3605", "PXISWND,12.4,0,24.1,1.5,86400,3605",
        "PXISWND,12.4,0,24.1,17,4294967296,3605", "PXISWND,,0,24.1,17,86400,3605",
    };
    for (const char* b : badBodies) { sentence(s, b); CHECK(parse(s, d) == ParseResult::Invalid); }

    char c[48];
    buildCommand(c, sizeof(c), Command::ZeroCounter, 0); CHECK(parse(c, d) == ParseResult::OtherSentence);
}

static void testCommands() {
    char c[48], e[48];
    buildCommand(c, sizeof(c), Command::SetFactor, 4200); CHECK(strcmp(c, "$PXISWNS,K,0.42*0B") == 0);
    buildCommand(c, sizeof(c), Command::ZeroCounter, 0); CHECK(strcmp(c, "$PXISWNS,C*37") == 0);
    buildCommand(c, sizeof(c), Command::Restart, 0); CHECK(strcmp(c, "$PXISWNS,R*26") == 0);
    buildCommand(c, sizeof(c), Command::SetLength, 25); sentence(e, "PXISWNS,C,25"); CHECK(strcmp(c, e) == 0);
    buildCommand(c, sizeof(c), Command::SetDebounce, 10); sentence(e, "PXISWNS,S,10"); CHECK(strcmp(c, e) == 0);
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

static void testConsole() {
    const char* arg;
    char a[] = "  help\r\n"; CHECK(parseConsoleLine(a, &arg) == ConsoleCommand::Help);
    char b[] = "status"; CHECK(parseConsoleLine(b, &arg) == ConsoleCommand::Status);
    char c[] = "ssid=Mein Bordnetz\r"; CHECK(parseConsoleLine(c, &arg) == ConsoleCommand::Ssid);
    CHECK(strcmp(arg, "Mein Bordnetz") == 0);
    char d[] = "pass=geheim123"; CHECK(parseConsoleLine(d, &arg) == ConsoleCommand::Password);
    CHECK(strcmp(arg, "geheim123") == 0);
    char e[] = "pass="; CHECK(parseConsoleLine(e, &arg) == ConsoleCommand::Password); CHECK(*arg == 0);
    char f[] = " \r\n"; CHECK(parseConsoleLine(f, &arg) == ConsoleCommand::Empty);
    char g[] = "SSID=x"; CHECK(parseConsoleLine(g, &arg) == ConsoleCommand::Unknown);
    CHECK(strcmp(arg, "SSID=x") == 0);

    // Zeilen zusammensetzen
    ConsoleLineReader r;
    auto feed = [&](const char* text, int& lines, int& overflows, char* last) {
        for (const char* p = text; *p; p++) {
            ConsoleLineReader::Event ev = r.feed(*p);
            if (ev == ConsoleLineReader::Event::Line) { lines++; strcpy(last, r.line()); }
            if (ev == ConsoleLineReader::Event::Overflow) overflows++;
        }
    };
    char last[200]; int lines = 0, over = 0;
    feed("status\r\nhelp\n", lines, over, last); CHECK(lines == 2 && strcmp(last, "help") == 0);
    lines = 0; feed("\r\n\r\n", lines, over, last); CHECK(lines == 0);
    feed("ssix\bd=Bor\x7f" "ot\r", lines, over, last); CHECK(lines == 1 && strcmp(last, "ssid=Boot") == 0);
    lines = 0; feed("\b\b\x1bstatus\r", lines, over, last); CHECK(lines == 1 && strcmp(last, "status") == 0);
    // zu lange Zeile: einmal melden, Rest verwerfen, danach geht es normal weiter
    char longLine[300]; memset(longLine, 'x', 299); longLine[299] = 0;
    lines = 0; over = 0; feed(longLine, lines, over, last); feed("yyy\r\n", lines, over, last);
    CHECK(over == 1 && lines == 0);
    feed("status\r", lines, over, last); CHECK(lines == 1 && strcmp(last, "status") == 0);

    char h1[] = "cerbo=192.168.5.20"; CHECK(parseConsoleLine(h1, &arg) == ConsoleCommand::CerboHost);
    CHECK(!strcmp(arg, "192.168.5.20") && validCerboHost(arg));
    char h2[] = "portal=c0619ab68636"; CHECK(parseConsoleLine(h2, &arg) == ConsoleCommand::Portal);
    CHECK(validPortalId(arg));
    char h3[] = "mqttpass=geheim"; CHECK(parseConsoleLine(h3, &arg) == ConsoleCommand::MqttPassword);
    CHECK(!strcmp(arg, "geheim") && validMqttPassword(arg));
    CHECK(!validCerboHost("") && !validCerboHost("192.168 .5.20"));
    CHECK(!validPortalId("c0619ab6-8636") && !validPortalId(""));
    CHECK(!validMqttPassword(""));
    char l1[] = "log=1"; CHECK(parseConsoleLine(l1, &arg) == ConsoleCommand::Log && !strcmp(arg, "1"));
    char l0[] = " log=0\r"; CHECK(parseConsoleLine(l0, &arg) == ConsoleCommand::Log && !strcmp(arg, "0"));
    char le[] = "log=ein"; CHECK(parseConsoleLine(le, &arg) == ConsoleCommand::Log && !strcmp(arg, "1"));
    char la[] = "log=aus"; CHECK(parseConsoleLine(la, &arg) == ConsoleCommand::Log && !strcmp(arg, "0"));
    char l2[] = "log=2"; CHECK(parseConsoleLine(l2, &arg) == ConsoleCommand::Unknown);
    char t1[] = "tcptest=192.168.5.12:80"; CHECK(parseConsoleLine(t1, &arg) == ConsoleCommand::TcpTest);
    char hb[40]; uint16_t hp = 0;
    CHECK(parseHostPort(arg, hb, sizeof(hb), hp, 8883) && !strcmp(hb, "192.168.5.12") && hp == 80);
    CHECK(parseHostPort("192.168.5.12", hb, sizeof(hb), hp, 8883) && hp == 8883);
    CHECK(!parseHostPort("192.168.5.12:", hb, sizeof(hb), hp, 8883) && !parseHostPort(":80", hb, sizeof(hb), hp, 8883));
    CHECK(!parseHostPort("a:70000", hb, sizeof(hb), hp, 1) && !parseHostPort("a:8x", hb, sizeof(hb), hp, 1));
    CHECK(!parseHostPort("a:0", hb, sizeof(hb), hp, 1) && !parseHostPort("abcdef", hb, 4, hp, 1));
    char t2[] = "tcptest2=x"; CHECK(parseConsoleLine(t2, &arg) == ConsoleCommand::TcpTestClassic);
    char u0[] = "udp=aus"; CHECK(parseConsoleLine(u0, &arg) == ConsoleCommand::Udp && !strcmp(arg, "0"));
    char m1[] = "motor=0"; CHECK(parseConsoleLine(m1, &arg) == ConsoleCommand::EngineInput);
    uint8_t in = 9;
    CHECK(parseEngineInput(arg, in) && in == 0);
    CHECK(parseEngineInput("4", in) && in == 4);
    CHECK(parseEngineInput("247", in) && in == 247);
    CHECK(!parseEngineInput("248", in) && !parseEngineInput("", in) && !parseEngineInput("1x", in) && in == 247);

    // Modbus TCP
    uint8_t rq[12];
    modbusReadRequest(rq, 0x1234, 1, MODBUS_REG_DIGITALINPUT_STATE, 1);
    const uint8_t expRq[12] = {0x12, 0x34, 0, 0, 0, 6, 1, 3, 0x0D, 0x5E, 0, 1};
    CHECK(memcmp(rq, expRq, 12) == 0);
    const uint8_t ok[11] = {0x12, 0x34, 0, 0, 0, 5, 1, 3, 2, 0, 3};
    uint16_t val = 0; uint8_t exc = 0; size_t used = 0;
    CHECK(parseModbusReadResponse(ok, 5, 0x1234, 1, val, exc, used) == ModbusResult::Incomplete);
    CHECK(parseModbusReadResponse(ok, 10, 0x1234, 1, val, exc, used) == ModbusResult::Incomplete);
    CHECK(parseModbusReadResponse(ok, 11, 0x1234, 1, val, exc, used) == ModbusResult::Value && val == 3 && used == 11);
    CHECK(parseModbusReadResponse(ok, 11, 0x1235, 1, val, exc, used) == ModbusResult::Invalid);
    CHECK(parseModbusReadResponse(ok, 11, 0x1234, 2, val, exc, used) == ModbusResult::Invalid);
    const uint8_t ex[9] = {0x12, 0x34, 0, 0, 0, 3, 1, 0x83, 0x0B};
    CHECK(parseModbusReadResponse(ex, 9, 0x1234, 1, val, exc, used) == ModbusResult::Exception && exc == 0x0B && used == 9);
    const uint8_t bad[9] = {0x12, 0x34, 0, 1, 0, 3, 1, 0x83, 0x0B};
    CHECK(parseModbusReadResponse(bad, 9, 0x1234, 1, val, exc, used) == ModbusResult::Invalid);
    CHECK(engineFromDigitalInputState(3) == EngineState::Running && engineFromDigitalInputState(2) == EngineState::Off);
    CHECK(engineFromDigitalInputState(1) == EngineState::Running && engineFromDigitalInputState(0) == EngineState::Off);
    CHECK(engineFromDigitalInputState(10) == EngineState::Running && engineFromDigitalInputState(11) == EngineState::Off);
    CHECK(engineFromDigitalInputState(8) == EngineState::Unknown && engineFromDigitalInputState(7) == EngineState::Unknown);
    long v = -1;
    CHECK(parseVictronInt("{\"value\": 1}", 12, v) && v == 1);
    CHECK(parseVictronInt("{\"value\":0}", 11, v) && v == 0);
    CHECK(!parseVictronInt("{\"value\": null}", 15, v));
    CHECK(!parseVictronInt("", 0, v));
    CHECK(parseVictronInt("{\"value\": 20.43}", 15, v) && v == 20);

    CHECK(!validSsid("")); CHECK(validSsid("a"));
    CHECK(validSsid("12345678901234567890123456789012"));
    CHECK(!validSsid("123456789012345678901234567890123"));
    CHECK(validPassword("")); CHECK(!validPassword("1234567")); CHECK(validPassword("12345678"));
    CHECK(validPassword("1234567890123456789012345678901234567890123456789012345678901234"));
    CHECK(!validPassword("12345678901234567890123456789012345678901234567890123456789012345"));
}

// Faelle aus dem Betrieb (18.11.2026), Alarmwert 60 C
static void testEngineGate() {
    const uint32_t MIN = 60000;
    ExhaustMonitor ex;
    EngineGate g;
    auto step = [&](float c, uint32_t t) {
        ex.onReading(c, t);
        g.onReading(c);
        ex.update(t, 60, g.monitorAllowed(c, t));
        return ex.alarmActive();
    };
    // Ohne Motorsignal (unbekannt): wie bisher, Nachheizen loest Alarm aus
    CHECK(step(64, 0));
    CHECK(!step(44.5f, MIN));

    // 1) Kaltstart: 28 -> 44.5, kein Alarm
    g.onState(EngineState::Off, 0);
    CHECK(!step(28, 0));
    g.onState(EngineState::Running, MIN);
    CHECK(g.inGrace(MIN));
    for (int i = 0; i <= 40; i++) CHECK(!step(28 + i * 0.4f, MIN + i * 30000));
    CHECK(!g.inGrace(MIN + 11 * MIN));

    // 2) Motor aus: Nachheizen auf 64 C, kein Alarm
    g.onState(EngineState::Off, 30 * MIN);
    for (int i = 0; i <= 40; i++) CHECK(!step(44.5f + i * 0.5f, 30 * MIN + i * MIN));

    // 3) Warmstart bei 64 C: faellt in 3 min auf 44.5, kein Alarm
    g.onState(EngineState::Running, 80 * MIN);
    for (int i = 0; i <= 6; i++) CHECK(!step(64 - i * 3.25f, 80 * MIN + i * 30000));
    CHECK(!step(44.5f, 95 * MIN));

    // Impellerausfall beim Warmstart: steigt statt zu fallen -> Alarm in der Schonfrist
    g.onState(EngineState::Off, 100 * MIN);
    CHECK(!step(64, 100 * MIN));
    g.onState(EngineState::Running, 120 * MIN);
    CHECK(!step(65, 120 * MIN + 30000));
    CHECK(step(66.5f, 120 * MIN + 60000));

    // Nach der Schonfrist: normaler Alarm ueber 60 C
    g.onState(EngineState::Off, 200 * MIN);
    step(40, 200 * MIN);
    g.onState(EngineState::Running, 201 * MIN);
    CHECK(!step(44.5f, 212 * MIN));
    CHECK(step(61, 213 * MIN));
    // Motor aus beendet den Alarm sofort
    g.onState(EngineState::Off, 214 * MIN);
    CHECK(!step(62, 214 * MIN));
    // Verbindung verloren -> unbekannt -> wieder wie bisher
    g.onState(EngineState::Unknown, 215 * MIN);
    CHECK(step(62, 215 * MIN));
}

static void testPages() {
    PageSelector p;
    CHECK(p.current() == Page::Chain && !p.left());
    CHECK(p.right() && p.current() == Page::N2k);
    CHECK(p.right() && p.current() == Page::Clock && !p.right());
    // Alarm auf der Uhrseite: zur Seite NMEA2000 und nach dem Alarm zurueck
    CHECK(!p.onAlarm(false));
    CHECK(p.onAlarm(true) && p.current() == Page::N2k);
    CHECK(!p.onAlarm(true) && p.current() == Page::N2k);
    CHECK(p.onAlarm(false) && p.current() == Page::Clock);
    // Waehrend des Alarms selbst zur Uhr zurueck: kein erneuter Sprung, solange der Alarm anhaelt
    CHECK(p.onAlarm(true) && p.current() == Page::N2k);
    CHECK(p.right() && p.current() == Page::Clock);
    CHECK(!p.onAlarm(true) && p.current() == Page::Clock);
    CHECK(!p.onAlarm(false) && p.current() == Page::Clock);
    // Nach dem Sprung selbst geblaettert: nach dem Alarm nicht zurueck
    CHECK(p.onAlarm(true) && p.current() == Page::N2k);
    CHECK(p.left() && p.current() == Page::Chain);
    CHECK(!p.onAlarm(false) && p.current() == Page::Chain);
    // Alarm auf einer anderen Seite: kein Wechsel
    CHECK(!p.onAlarm(true) && p.current() == Page::Chain);
    CHECK(!p.onAlarm(false));
}

static void testBacklight() {
    CHECK(nightBacklightPercent(NIGHT_LEVEL_MIN) == 1);
    CHECK(nightBacklightPercent(NIGHT_LEVEL_MAX) == 100);
    CHECK(nightBacklightPercent(0) == 1 && nightBacklightPercent(99) == 100);
    for (int l = NIGHT_LEVEL_MIN; l < NIGHT_LEVEL_MAX; l++)
        CHECK(nightBacklightPercent(l) < nightBacklightPercent(l + 1));
    CHECK(nightBacklightPercent(NIGHT_LEVEL_MAX) == BACKLIGHT_DAY_PERCENT);
}

// Rahmen aus dem Log vom 15.11.2026: Bruecke (Helm 1), Instrument Adresse 112
static void testRaymarineBrightness() {
    // gesendete Prozentwerte der Stufen 1 bis 10 -> gleiche Stufennummer
    const uint8_t sent[10] = {0, 11, 22, 33, 45, 56, 67, 78, 89, 100};
    for (int i = 0; i < 10; i++) CHECK(raymarineLevel(sent[i]) == i + 1);

    RaymarineBrightness rb;
    uint8_t pct = 255;
    CHECK(!rb.onFrame(frame("11:45:23.471 R 1DEFFF70 40 09 3B 9F 8C 0C 01 01"), 1, pct));
    CHECK(rb.onFrame(frame("11:45:23.472 R 1DEFFF70 41 00 16 00 FF FF FF FF"), 1, pct) && pct == 22);
    // andere Gruppe oder "Aus": ignoriert
    CHECK(!rb.onFrame(frame("11:45:23.471 R 1DEFFF70 40 09 3B 9F 8C 0C 01 01"), 2, pct));
    CHECK(!rb.onFrame(frame("11:45:23.472 R 1DEFFF70 41 00 16 00 FF FF FF FF"), 2, pct));
    CHECK(!rb.onFrame(frame("11:45:23.471 R 1DEFFF70 40 09 3B 9F 8C 0C 01 01"), HELM_GROUP_OFF, pct));
    // Farbe (Befehl 1) ist keine Helligkeit
    CHECK(!rb.onFrame(frame("11:00:00.000 R 1DEFFF70 60 09 3B 9F 8C 0C 01 01"), 1, pct));
    CHECK(!rb.onFrame(frame("11:00:00.001 R 1DEFFF70 61 01 03 00 FF FF FF FF"), 1, pct));
    // Folgerahmen ohne passenden ersten Rahmen oder mit anderer Sequenz
    CHECK(!rb.onFrame(frame("11:00:00.002 R 1DEFFF70 21 00 16 00 FF FF FF FF"), 1, pct));
    CHECK(!rb.onFrame(frame("11:00:00.003 R 1DEFFF70 80 09 3B 9F 8C 0C 01 01"), 1, pct));
    CHECK(!rb.onFrame(frame("11:00:00.004 R 1DEFFF70 A1 00 16 00 FF FF FF FF"), 1, pct));
    // anderes Raymarine-Fast-Packet derselben PGN (Plotter, Adresse 204)
    CHECK(!rb.onFrame(frame("11:45:12.169 R 1DEFFFCC 00 09 3B 9F F0 81 AE 02"), 1, pct));
    CHECK(!rb.onFrame(frame("11:45:12.170 R 1DEFFFCC 01 00 16 00 FF FF FF FF"), 1, pct));
    // Stufe 1 = 0 %
    CHECK(!rb.onFrame(frame("11:45:21.871 R 1DEFFF70 E0 09 3B 9F 8C 0C 01 01"), 1, pct));
    CHECK(rb.onFrame(frame("11:45:21.872 R 1DEFFF70 E1 00 00 00 FF FF FF FF"), 1, pct) && pct == 0);
    CHECK(!strcmp(helmGroupName(1), "Helm 1") && !strcmp(helmGroupName(0), "Aus"));
}

int main() {
    testWindlass(); testCommands(); testYdRaw(); testExhaust(); testChain(); testClock(); testFormat();
    testConsole(); testBacklight(); testRaymarineBrightness(); testPages();
    testEngineGate();
    if (failures) { printf("%d Fehler\n", failures); return 1; }
    printf("ALLE TESTS OK\n");
    return 0;
}
