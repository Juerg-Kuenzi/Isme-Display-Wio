#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Zeichnet die RAW-Daten der YDWG-02 (UDP) mit Markierungen auf.

Gedacht, um herauszufinden, welche NMEA2000-Nachrichten beim Verstellen der
Display-Helligkeit gesendet werden. Während der Aufnahme eine Markierung
eintippen und mit Enter bestätigen, zum Beispiel "Bruecke Stufe 3". Sie
landet mit Zeitstempel in der Logdatei, so lassen sich die Nachrichten
danach den Einstellungen zuordnen.

Aufruf (nur Python 3, keine weiteren Pakete):
    python log_n2k_raw.py --tcp 192.168.4.1 --port 1460   # TCP zur YDWG-02 (empfohlen)
    python log_n2k_raw.py                                 # UDP-Broadcast auf Port 1457
    python log_n2k_raw.py --out helligkeit.txt            # eigener Dateiname

Beenden mit "ende" + Enter oder Strg+C.

TCP (empfohlen): Der PC verbindet sich selbst mit der YDWG-02. Dafuer in deren
Weboberflaeche einen freien Server einrichten: Protokoll TCP, Datenformat RAW,
z. B. Port 1460, ohne Filter. --tcp ist die IP der YDWG-02.

UDP: Die YDWG-02 sendet als Broadcast. Kommt nichts an ("Zeilen: 0"), blockiert
meist die Windows-Firewall eingehende Pakete fuer Python, vor allem in einem
WLAN, das Windows als "oeffentlich" einstuft. Dann TCP verwenden.
"""
import argparse
import datetime
import socket
import sys
import threading

# PGNs, die beim Verstellen von Helligkeit/Farbe interessant sind
WATCH = {
    126720: "herstellereigen (Raymarine/Garmin: Helligkeit, Farbe)",
    65288: "Furuno: Remote Backlight",
    65286: "Chetco: Dimmer",
    126208: "Group Function (Befehl/Anfrage)",
}


def pgn_from_can_id(can_id: int) -> int:
    pf = (can_id >> 16) & 0xFF
    ps = (can_id >> 8) & 0xFF
    dp = (can_id >> 24) & 0x01
    return (dp << 16) | (pf << 8) | (ps if pf >= 240 else 0)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tcp", metavar="IP", help="per TCP mit der YDWG-02 unter dieser IP verbinden")
    ap.add_argument("--port", type=int, default=1457, help="Port des RAW-Servers der YDWG-02, Standard 1457")
    ap.add_argument("--out", help="Logdatei (Standard: n2k_log_<Datum_Zeit>.txt)")
    a = ap.parse_args()
    out = a.out or datetime.datetime.now().strftime("n2k_log_%Y%m%d_%H%M%S.txt")

    if a.tcp:
        print(f"Verbinde per TCP mit {a.tcp}:{a.port} ...")
        try:
            sock = socket.create_connection((a.tcp, a.port), timeout=5)
        except OSError as e:
            print(f"FEHLER: keine Verbindung zu {a.tcp}:{a.port} ({e}).")
            print("IP der YDWG-02 und Port des TCP-Servers (Datenformat RAW) pruefen.")
            return 1
        source = f"TCP {a.tcp}:{a.port}"
    else:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(("", a.port))
        source = f"UDP-Port {a.port}"
    sock.settimeout(0.5)

    lock = threading.Lock()
    stop = threading.Event()
    counts = {"lines": 0, "watch": 0, "packets": 0, "senders": set()}
    log = open(out, "w", encoding="utf-8", newline="\n")

    def write(text: str) -> None:
        with lock:
            log.write(text + "\n")
            log.flush()

    def receiver() -> None:
        buf = ""
        while not stop.is_set():
            try:
                if a.tcp:
                    data = sock.recv(4096)
                    if not data:
                        print("Die YDWG-02 hat die TCP-Verbindung beendet.")
                        stop.set()
                        break
                else:
                    data, addr = sock.recvfrom(4096)
                    counts["senders"].add(addr[0])
            except socket.timeout:
                continue
            except OSError as e:
                print(f"Empfangsfehler: {e}")
                stop.set()
                break
            counts["packets"] += 1
            buf += data.decode("ascii", errors="replace")
            *lines, buf = buf.split("\n")
            for line in lines:
                line = line.strip()
                if not line:
                    continue
                now = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
                write(f"{now} {line}")
                counts["lines"] += 1
                parts = line.split()
                if len(parts) >= 3:
                    try:
                        pgn = pgn_from_can_id(int(parts[2], 16))
                    except ValueError:
                        continue
                    if pgn in WATCH:
                        counts["watch"] += 1
                        print(f"  {now}  PGN {pgn} ({WATCH[pgn]}) von Quelle {int(parts[2], 16) & 0xFF}")

    def heartbeat() -> None:
        # alle 10 s ein Lebenszeichen, damit man sofort sieht, ob Daten kommen
        last = -1
        while not stop.wait(10):
            if counts["lines"] == 0:
                hint = "noch keine Daten"
                if not a.tcp:
                    hint += " - Firewall? Sonst --tcp verwenden (siehe Hilfe: -h)"
                print(f"  [{hint}]")
            elif counts["lines"] != last:
                snd = f", von {', '.join(sorted(counts['senders']))}" if counts["senders"] else ""
                print(f"  [Zeilen: {counts['lines']}, Kandidaten: {counts['watch']}{snd}]")
            last = counts["lines"]

    t = threading.Thread(target=receiver, daemon=True)
    t.start()
    threading.Thread(target=heartbeat, daemon=True).start()
    print(f"Aufnahme von {source} nach {out}")
    print('Markierung eintippen + Enter (z. B. "Bruecke Stufe 3"), leere Zeile zeigt den Zaehler, "ende" beendet.')
    write(f"### Aufnahme gestartet {datetime.datetime.now().isoformat(timespec='seconds')}, {source}")
    try:
        while True:
            text = input().strip()
            if text.lower() == "ende":
                break
            now = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
            if text:
                write(f"{now} ### MARKE: {text}")
                print(f"  -> Marke gesetzt: {text}")
            print(f"  Zeilen: {counts['lines']}, davon Helligkeit/Farbe-Kandidaten: {counts['watch']}")
    except (KeyboardInterrupt, EOFError):
        pass
    stop.set()
    t.join(timeout=2)
    sock.close()
    write("### Aufnahme beendet")
    log.close()
    print(f"Gespeichert: {out} ({counts['lines']} Zeilen)")


if __name__ == "__main__":
    sys.exit(main())
