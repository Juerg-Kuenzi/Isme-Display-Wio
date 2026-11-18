# Ismé Display (Wio Terminal)

Firmware für das Seeed Wio Terminal als fest eingebaute Anzeige an Bord der Ismé. Sie kann dasselbe wie die Android-App [Ismé](https://github.com/Juerg-Kuenzi/Isme), läuft aber dauernd und eignet sich deshalb für die Überwachung der Abgastemperatur.

## Funktionen

- **Seite Ankerkette:** Kettenlänge, Richtung (Auf oder Ab) und Speisung vom [ChainCounter](https://github.com/Juerg-Kuenzi/ChainCounter). Kommt 3 s lang kein gültiger Satz, wird die Länge abgeblendet und unten erscheint eine Warnung.
- **Seite Abgastemperatur** (bis 1.9.0 „NMEA2000“): Abgastemperatur von der YDWG-02 (PGN 130316, ersatzweise 130312, Quelle 14) groß in der Schrift der Kettenlänge, dazu der Alarmwert. Die Uhrzeit hat seit Firmware 1.9.0 eine eigene Seite. 7-Segment-Schrift ist der Uhr vorbehalten, damit Temperatur und Zeit beim flüchtigen Hinschauen nicht verwechselt werden. Kommt 6 s lang keine Temperatur, wird der Wert abgeblendet und unten erscheint eine Warnung.
- **Seite Uhrzeit:** die Ortszeit groß in 7-Segment-Schrift (96 Pixel hoch) für die Nachtfahrt. Die Abgastemperatur wird im Hintergrund weiter überwacht: Bei einem Abgasalarm springt die Anzeige zur Seite NMEA2000 und nach dem Alarm wieder zurück zur Uhr, sofern man inzwischen nicht selbst geblättert hat. Warnungen (keine Abgastemperatur, keine Zeit vom GPS) erscheinen unten im Warnbalken, ohne die Seite zu wechseln.
- **Motorsignal (ab 2.0.0, optional):** Ein nasser Auspuff heizt sich nach dem Abstellen auf die Motorraumtemperatur auf (~64 °C) und würde den Abgasalarm auslösen. Mit dem Signal „Motor läuft“ von einem Digitaleingang des Cerbo GX gilt: *Motor aus* – kein Abgasalarm; *nach dem Start* – 10 Minuten Schonfrist, Alarm nur, wenn die Temperatur mehr als 2 °C über den Wert beim Start steigt (ein Warmstart fällt von ~64 °C, ein Impellerausfall steigt); *danach* – Überwachung wie gewohnt. Ist das Motorsignal ausgeschaltet, fehlt die Verbindung zum Cerbo oder meldet er nichts, überwacht das Display wie ohne Motorsignal. Einrichtung siehe unten.
- **Ortszeit:** UTC aus PGN 126992 „System Time“ vom GPS, plus die eingestellte Zeitzone (UTC−12 bis UTC+14, ganze Stunden, Standard UTC+0). Zwischen zwei Meldungen läuft die Uhr mit der internen Zeit weiter.
- **Abgasalarm:** Über dem Alarmwert (Standard 50 °C) erscheint ein Alarmbalken auf jeder Seite, und der Summer gibt alle 2 s vier kurze Töne auf der eingestellten Tonhöhe. Die mittlere Taste schaltet den Ton bis zum nächsten Alarm aus (bis Firmware 1.5.1 die rechte Taste). Der Alarm endet 1 °C unter dem Alarmwert.
- **Ton testen:** Ein Piezo-Summer ist bei seiner Resonanzfrequenz am lautesten. Unter *Einstellungen > Ton testen* spielt der Alarmton, und mit dem Joystick auf/ab lässt sich die Tonhöhe in Schritten von 100 Hz zwischen 500 und 5000 Hz verstellen (Standard 2700 Hz). Drücken speichert die lauteste Einstellung.
- **Einstellungen:** Nachthelligkeit, Helligkeit folgt, Zähler nullen, Länge setzen, Port ChainCounter, Entprellzeit, Umrechnungsfaktor, Restart, Port NMEA2000, Alarmwert Abgas, Ton testen, Zeitzone, Informationen. Die Liste scrollt mit der Auswahl. Factory Reset ist bewusst nicht enthalten.
- **Tag/Nacht:** Die rechte Taste schaltet direkt um. Tag: helle Farben, volle Helligkeit. Nacht: rote Anzeige auf Schwarz, damit die Nachtsicht erhalten bleibt, und die Hintergrundbeleuchtung gedimmt auf die eingestellte Nachtstufe.
- **Nachthelligkeit:** Unter *Einstellungen > Nachthelligkeit* lässt sich die Nachtstufe mit dem Joystick auf/ab in 10 Stufen einstellen (Standard 4). Die gewählte Stufe wird sofort mit der Nachtdarstellung vorgeführt, Drücken speichert sie. Taste links verlässt die Seite ohne zu speichern. Die Stufen entsprechen 1, 2, 3, 5, 8, 13, 22, 36, 60 und 100 % Helligkeit, weil das Auge kleine Helligkeiten feiner unterscheidet.
- **Helligkeit folgt der Brücke:** Nachts übernimmt das Display die Helligkeitsstufe einer Raymarine-Displaygruppe (Standard *Helm 1*), sobald sie an einem Instrument dieser Gruppe verstellt wird: Stufe n am Instrument = Nachtstufe n am Display. Tagsüber bleibt das Display voll hell. Die Instrumente senden nur bei einer Änderung; bis dahin und nach dem Einschalten gilt die eigene Nachtstufe. Wer die eigene Nachtstufe speichert, überschreibt die gemeldete Stufe bis zur nächsten Meldung. Die Gruppe wird unter *Einstellungen > Helligkeit folgt* gewählt (Aus, Helm 1, Helm 2, Cockpit, Flybridge, Mast, Gruppe 1–5); dort steht auch die zuletzt gemeldete Stufe.

Ports, Alarmwert, Tonhöhe, Zeitzone, Tag/Nacht, Nachtstufe, Displaygruppe und die WLAN-Zugangsdaten werden im Flash gespeichert und überleben einen Neustart und ab Firmware 1.5.0 auch ein Update per UF2. Standardwerte: ChainCounter 1458, NMEA2000 1457, Alarm 50 °C (`config.h`).

## Bedienung

Die drei Tasten oben sind von links nach rechts C, B und A.

| Wo | Bedienelement | Wirkung |
|---|---|---|
| überall | Taste Mitte (B) | Alarmton aus |
| überall | Taste rechts (A) | Tag/Nacht umschalten |
| Hauptanzeige | Joystick links/rechts | Seiten Ankerkette, Abgastemperatur, Uhrzeit |
| Hauptanzeige | Taste links (C) | Einstellungen öffnen |
| Einstellungen | Joystick auf/ab | Eintrag wählen |
| Einstellungen | Joystick drücken | öffnen |
| Einstellungen | Taste links (C) | zurück zur Hauptanzeige |
| Wert ändern | Joystick links/rechts | Ziffer wählen |
| Wert ändern | Joystick auf/ab | Ziffer ändern |
| Wert ändern | Joystick drücken | senden bzw. speichern |
| Wert ändern | Taste links (C) | zurück zu den Einstellungen |
| Ton testen | Joystick auf/ab | Tonhöhe ändern |
| Ton testen | Joystick drücken | Tonhöhe speichern |
| Zeitzone | Joystick auf/ab | eine Stunde mehr oder weniger, mit Vorschau der Ortszeit |
| Zeitzone | Joystick drücken | Zeitzone speichern |

| Nachthelligkeit | Joystick auf/ab | Stufe ändern, sofort sichtbar |
| Nachthelligkeit | Joystick drücken | Stufe speichern |

Die Tastenbelegung lässt sich in `app.cpp` mit `KEY_MENU`, `KEY_MUTE` und `KEY_NIGHT` ändern.

Das Gerät quittiert keine Kommandos. Kommandos gehen per Unicast an die Adresse, von der die Datensätze des ChainCounter kommen. Auf den Seiten „Zähler nullen“ und „Länge setzen“ steht die Länge, die das Gerät gerade meldet.

## Protokoll

Das Display liest die Datensätze `$PXISWND` und sendet Kommandos als `$PXISWNS` (ChainCounter ab Firmware 1.3.0, Spezifikation V1.9). Die früheren Sätze `$ISLWND`/`$ISLWNS` werden ab Display-Firmware 1.4.0 nicht mehr verwendet.

## Einrichten

1. **WLAN-Chip aktualisieren:** Das WLAN des Wio Terminal läuft über einen eigenen Chip (RTL8720DN). Er braucht einmalig die aktuelle Firmware von Seeed, die mit dem `ambd_flash_tool` aufgespielt wird. Anleitung: [Wio Terminal Network Overview](https://wiki.seeedstudio.com/Wio-Terminal-Network-Overview/).
2. **Arduino IDE 2.x:** Unter *Datei > Einstellungen > Zusätzliche Boardverwalter-URLs* `https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json` eintragen. In der Boardverwaltung „Seeed SAMD Boards“ installieren (gebaut wird mit 1.8.6) und das Board „Seeeduino Wio Terminal“ wählen.
3. **Bibliotheken:** „Seeed Arduino rpcWiFi“ (installiert die übrigen WLAN-Bibliotheken mit), und „FlashStorage_SAMD“. Die Anzeige-Bibliothek ist im Boardpaket enthalten.
4. **Hochladen:** `firmware/IsmeDisplay/IsmeDisplay.ino` öffnen und hochladen.
5. **Zugangsdaten** über die USB-Konsole setzen (siehe unten).

Die Arduino IDE ist nur zum Weiterentwickeln nötig. Für ein Update genügt die fertige Datei von GitHub.

### Firmware aufspielen ohne Arduino IDE

GitHub baut die Firmware bei jedem Push. Bei jedem veröffentlichten Release hängt GitHub die Datei `IsmeDisplay-<Version>.uf2` automatisch an das [Release](https://github.com/Juerg-Kuenzi/Isme-Display-Wio/releases) an. Aufspielen geht ohne zusätzliche Programme:

1. Die `.uf2`-Datei herunterladen.
2. Das Wio Terminal per USB-C an den PC anschließen.
3. Den Ein-Schalter **zweimal schnell nach unten** schieben. Die blaue LED pulsiert, und auf dem PC erscheint ein Laufwerk **„Arduino“**.
4. Die `.uf2`-Datei auf dieses Laufwerk kopieren. Das Wio Terminal startet danach von selbst mit der neuen Firmware.

Ab Firmware 1.5.0 bleiben Einstellungen und Zugangsdaten dabei erhalten. Sie liegen im letzten Block des Flash-Speichers, außerhalb des Programms. Ob ein Hochladen aus der Arduino IDE diesen Block ebenfalls verschont, ist nicht geprüft. Im Zweifel danach mit `status` kontrollieren.

### WLAN-Zugangsdaten (USB-Konsole)

Ab Firmware 1.5.0 werden SSID und Passwort nicht mehr einkompiliert, sondern über die USB-Konsole gesetzt und im Flash gespeichert. Deshalb funktioniert die Datei von GitHub direkt an Bord.

Jedes serielle Terminal mit **115200 Baud** reicht, zum Beispiel der serielle Monitor der Arduino IDE, PuTTY oder im Browser (Chrome/Edge) der Bereich *Console* auf <https://espressif.github.io/esptool-js/>:

```
ssid=MeinBordnetz
pass=MeinPasswort
status
```

| Befehl | Funktion |
|---|---|
| `status` | Firmware, SSID, ob ein Passwort gesetzt ist (es wird nie angezeigt), WLAN-Verbindung, Tag/Nacht mit Nachtstufe, Displaygruppe mit zuletzt gemeldeter Stufe, Ports |
| `ssid=<name>` | SSID setzen (1–32 Zeichen), speichern, neu verbinden. Meldet sofort „wird gespeichert …“, dann „gespeichert“ und „Verbinde neu …“, danach „WLAN verbunden, IP …“ oder nach 30 s einen Hinweis, falls keine Verbindung zustande kommt |
| `pass=<pw>` | Passwort setzen (leer = offenes Netz, sonst 8–64 Zeichen), speichern, neu verbinden, Meldungen wie bei `ssid=` |
| `cerbo=<ip>` | Adresse des Cerbo GX für das Motorsignal |
| `motor=<n>` | Motorsignal: `0` aus, sonst Geräte-ID (Modbus Unit-ID) des Digitaleingangs |
| `tcptest=<ip>[:port]` | Fehlersuche: TCP-Verbindung testen (Standard Port 502) |
| `udp=aus` / `udp=ein` | Fehlersuche: UDP-Empfang anhalten, nur bis zum Neustart |
| `log=ein` / `log=aus` | Protokoll für die Fehlersuche: jede Sekunde ein Lebenszeichen, jeder Schritt der Cerbo-Verbindung mit Dauer, Tasten, langsame Abschnitte der Hauptschleife. Nur bis zum nächsten Neustart. |
| `help` | Befehlsliste |

Hinweise zum Terminalprogramm:

- **Zeilenende:** Enter muss CR, LF oder CR+LF senden. In **HTerm** unter *Send on enter* einen dieser Werte wählen, die Grundeinstellung *None* sendet kein Zeilenende. Tera Term und der serielle Monitor der Arduino IDE senden passend.
- **Echo:** Das Display sendet die getippten Zeichen nicht zurück. In Tera Term sieht man die Eingabe deshalb nur mit *Setup > Terminal > Local echo*.
- **Rücktaste** korrigiert die Eingabe wie gewohnt. Eine zu lange Zeile (über 127 Zeichen) wird mit „FEHLER: Zeile zu lang“ verworfen, danach geht es normal weiter.
- **DTR** muss ab Firmware 1.5.1 nicht mehr gesetzt sein. Vorher kamen die Antworten nur an, wenn das Terminal DTR setzte (Tera Term ja, HTerm nur mit der Taste *DTR*).

Solange keine SSID gesetzt ist, zeigt *Einstellungen > Informationen* bei WLAN „nicht eingerichtet“.

**Update von Firmware 1.4.0 oder älter:** Bis 1.4.0 hat jedes Update alle Einstellungen gelöscht. Beim ersten Start von 1.5.0 gelten deshalb einmalig die Standardwerte. Danach müssen Ports, Alarmwert, Tonhöhe und Zeitzone neu eingestellt und die Zugangsdaten über die Konsole gesetzt werden. Nur wenn man 1.5.0 selbst mit einer `secrets.h` kompiliert, übernimmt das Display die Zugangsdaten von dort. `secrets.h` ist nur noch optional, die Vorlage `secrets.h.example` bleibt für eigene Builds.

## Stromversorgung

Das Wio Terminal braucht 5 V über USB-C. Für den Dauerbetrieb an Bord ist ein Wandler von 12 bzw. 24 V auf USB nötig.

## Aufbau

```
firmware/IsmeDisplay/
  IsmeDisplay.ino      Einstieg, ruft appSetup() und appLoop() auf
  app.cpp              Anzeige, Tasten, WLAN/UDP, Summer, Einstellungen im Flash
  core.h, core.cpp     Kernlogik ohne Arduino: NMEA, YDWG-02-RAW, Temperatur-PGNs,
                       Raymarine-Helligkeit, Duplikatfilter, Abgasalarm, Datenausfall,
                       Formatierung, Konsole, Motorsignal (EngineGate), Seiten
  cerbo.h, cerbo.cpp   Motorsignal vom Cerbo GX über Modbus TCP
  config.h             Version, Standardwerte, Zeiten
  secrets.h.example    optionale Startwerte für die WLAN-Zugangsdaten
tests/host/            PC-Tests der Kernlogik (g++), mit denselben Testdaten wie die App
tools/log_n2k_raw.py   Aufnahme der YDWG-02-RAW-Daten mit Markierungen (PC, Python 3)
```

```
make -C tests/host
```

## Motorsignal vom Cerbo GX

Das Display liest den Zustand eines Digitaleingangs des Cerbo GX über **Modbus TCP** im lokalen Netz: Port 502, Register 3422 (`/State` von `com.victronenergy.digitalinput`), Unit-ID = Geräte-ID des Eingangs. Jede Sekunde eine Abfrage über eine offen gehaltene Verbindung.

Warum nicht MQTT: Mit den Sicherheitsprofilen *Secured* und *Weak* verlangt der Cerbo für MQTT TLS. Der TLS-Client des WLAN-Chips im Wio (RTL8720DN) scheitert am Handshake mit dem Cerbo (Abbruch durch den Cerbo, Windows handelt ECDHE/X25519 mit AES-256-GCM aus). Unverschlüsseltes MQTT gibt es nur im Profil *Unsecured*, dann aber ohne jedes Passwort für den ganzen Cerbo.

Einrichtung:

1. Cerbo: *Einstellungen > Integrationen > Digitale E/A*: den Eingang auf **Bilgepumpe** stellen (der Typ *Generic I/O* steht im Menü nicht zur Wahl), Alarm aus, Name z. B. „Motor“. *ein* muss „Motor läuft“ bedeuten, sonst *Invertieren*. **Nicht „Generator“** wählen: dieser Typ meldet dem Multi/Quattro einen Generator am AC-Eingang und steuert die Generator-Start/Stopp-Logik.
2. Cerbo: *Einstellungen > Integrationen > Modbus TCP* einschalten. Unter *Verfügbare Dienste* steht der Eingang mit seiner Geräte-ID, z. B. „Motor – com.victronenergy.digitalinput | Geräte-ID: 1“. Kontrolle vom PC: `Test-NetConnection <IP des Cerbo> -Port 502`.
3. Display, USB-Konsole: `cerbo=<IP des Cerbo>`.
4. Display: *Einstellungen > Motorsignal* auf die Geräte-ID stellen (oder Konsole `motor=1`). Die Seite zeigt den Zustand der Verbindung, ebenso `status` an der Konsole. `motor=0` schaltet das Motorsignal aus.

Verstanden werden die Zustände low/high (Generic I/O), aus/ein (Bilgepumpe), nein/ja und läuft/gestoppt (Generator); Alarmtypen ergeben „Typ des Eingangs passt nicht“.

Hinweis Sicherheit: Modbus TCP hat kein Passwort. Jedes Gerät im selben Netz kann Werte lesen und einige Einstellungen schreiben (z. B. ESS-Sollwerte). Das Sicherheitsprofil für Weboberfläche und MQTT bleibt davon unberührt.

Ohne Relais testen: Im Cerbo *Invertieren* des Eingangs umschalten – das Display muss zwischen „Motor aus“ und „Motor läuft“ wechseln. Auf der Seite Abgastemperatur steht unten „Motor aus: Abgasalarm ruht“ bzw. kurz nach dem Start „Motor gestartet: Schonfrist“.

Der Verbindungsaufbau läuft über den WLAN-Chip und hält das Display dabei an (Tasten, Anzeige, Konsole), meist unter 1 s. Ist der Cerbo ausgeschaltet oder nicht im Netz, bis etwa 30 s, denn der WLAN-Chip kennt dafür keine Zeitgrenze (ein eigener nicht-blockierender Aufbau führte zu Abstürzen). Deshalb beginnt er erst 20 s nach der WLAN-Verbindung oder einer Änderung und nur, wenn die Hauptseite angezeigt wird und seit 10 s keine Taste gedrückt wurde. Nach einem Fehlschlag folgt der nächste Versuch nach 30 s, danach in wachsenden Abständen bis 5 min. Fehlersuche: Konsole `log=ein`.

## Raymarine-Helligkeit (PGN 126720)

Raymarine-Geräte melden eine Helligkeitsänderung ihrer Displaygruppe einmalig als herstellereigene Nachricht in zwei CAN-Rahmen ([canboat](https://github.com/canboat/canboat): „Seatalk1: Display Brightness“). Aufgezeichnet an Bord, Brücke (*Helm 1*), Stufe 3:

```
R 1DEFFF70 40 09 3B 9F 8C 0C 01 01   Länge 9, Raymarine (1851), 140/12, Gruppe 1, shared
R 1DEFFF70 41 00 16 00 FF FF FF FF   Befehl 0 = Helligkeit, 0x16 = 22 %
```

Die Stufen 1 bis 10 kommen als 0, 11, 22, 33, 45, 56, 67, 78, 89 und 100 %. Tag/Nacht am Plotter wurde in den Aufnahmen nicht gesendet.

Zum Aufzeichnen eigener Daten: `tools/log_n2k_raw.py` (Python 3, keine weiteren Pakete) speichert die RAW-Daten der YDWG-02 mit Markierungen, die man während der Aufnahme eintippt. Am zuverlässigsten per TCP zu einem eigenen Server der YDWG-02 (TCP, RAW, ohne Filter): `python log_n2k_raw.py --tcp <IP> --port <Port>`.

## Grenzen

- **Dimmen der Beleuchtung:** Der Pin der Hintergrundbeleuchtung hat kein PWM. Ein Timer (TC1) erzeugt das Signal, die Logikzelle CCL und das Ereignissystem schalten damit den Pin (nach dem Beispiel von Seeed, dort mit TC0, der hier für den Alarmton gebraucht wird). Tagsüber ist die Beleuchtung einfach eingeschaltet, sie bleibt also hell, auch wenn das Dimmen auf einer Hardware nicht funktionieren sollte.
- Die eingebauten Schriften kennen nur ASCII. Die Punkte von ä, ö, ü und das Gradzeichen werden deshalb von Hand gezeichnet.
- Der eingebaute Summer ist klein. Für den Motorraum oder laufenden Motor ist ein externer Summer über einen Grove-Anschluss geplant.
