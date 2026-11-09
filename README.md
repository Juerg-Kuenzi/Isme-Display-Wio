# Ismé Display (Wio Terminal)

Firmware für das Seeed Wio Terminal als fest eingebaute Anzeige an Bord der Ismé. Sie kann dasselbe wie die Android-App [Ismé](https://github.com/Juerg-Kuenzi/Isme), läuft aber dauernd und eignet sich deshalb für die Überwachung der Abgastemperatur.

## Funktionen

- **Seite Ankerkette:** Kettenlänge, Richtung (Auf oder Ab) und Speisung vom [ChainCounter](https://github.com/Juerg-Kuenzi/ChainCounter). Kommt 3 s lang kein gültiger Satz, wird die Länge abgeblendet und unten erscheint eine Warnung.
- **Seite NMEA2000:** Abgastemperatur von der YDWG-02 (PGN 130316, ersatzweise 130312, Quelle 14) und der Alarmwert. Kommt 6 s lang kein Wert, erscheint eine Warnung.
- **Abgasalarm:** Über dem Alarmwert (Standard 50 °C) erscheint ein Alarmbalken auf jeder Seite, und der Summer ertönt alle 2 s für 1 s. Die Taste rechts schaltet den Ton bis zum nächsten Alarm aus. Der Alarm endet 1 °C unter dem Alarmwert.
- **Einstellungen:** Hell/Dunkel, Zähler nullen, Länge setzen, Port ChainCounter, Entprellzeit, Umrechnungsfaktor, Restart, Port NMEA2000, Alarmwert Abgas, Informationen. Factory Reset ist bewusst nicht enthalten.
- **Dunkel:** rote Anzeige auf Schwarz, damit die Nachtsicht erhalten bleibt.

Ports, Alarmwert und Hell/Dunkel werden im Flash gespeichert und überleben einen Neustart. Standardwerte: ChainCounter 1458, NMEA2000 1457, Alarm 50 °C (`config.h`).

## Bedienung

Die drei Tasten oben sind von links nach rechts C, B und A.

| Wo | Bedienelement | Wirkung |
|---|---|---|
| überall | Taste rechts (A) | Alarmton aus |
| Hauptanzeige | Joystick links/rechts | Seite Ankerkette bzw. NMEA2000 |
| Hauptanzeige | Taste links (C) | Einstellungen öffnen |
| Einstellungen | Joystick auf/ab | Eintrag wählen |
| Einstellungen | Joystick drücken | öffnen, bei Hell/Dunkel umschalten |
| Einstellungen | Taste links (C) | zurück zur Hauptanzeige |
| Wert ändern | Joystick links/rechts | Ziffer wählen |
| Wert ändern | Joystick auf/ab | Ziffer ändern |
| Wert ändern | Joystick drücken | senden bzw. speichern |
| Wert ändern | Taste links (C) | zurück zu den Einstellungen |

Die Tastenbelegung lässt sich in `app.cpp` mit `KEY_MENU` und `KEY_MUTE` ändern.

Das Gerät quittiert keine Kommandos. Kommandos gehen per Unicast an die Adresse, von der die `$ISLWND`-Sätze kommen. Auf den Seiten „Zähler nullen“ und „Länge setzen“ steht die Länge, die das Gerät gerade meldet.

## Einrichten

1. **WLAN-Chip aktualisieren:** Das WLAN des Wio Terminal läuft über einen eigenen Chip (RTL8720DN). Er braucht einmalig die aktuelle Firmware von Seeed, die mit dem `ambd_flash_tool` aufgespielt wird. Anleitung: [Wio Terminal Network Overview](https://wiki.seeedstudio.com/Wio-Terminal-Network-Overview/).
2. **Arduino IDE 2.x:** Unter *Datei > Einstellungen > Zusätzliche Boardverwalter-URLs* `https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json` eintragen. In der Boardverwaltung „Seeed SAMD Boards“ installieren (gebaut wird mit 1.8.6) und das Board „Seeeduino Wio Terminal“ wählen.
3. **Bibliotheken:** „Seeed Arduino rpcWiFi“ (installiert die übrigen WLAN-Bibliotheken mit) und „FlashStorage_SAMD“. Die Anzeige-Bibliothek ist im Boardpaket enthalten.
4. **Zugangsdaten:** `firmware/IsmeDisplay/secrets.h.example` nach `secrets.h` kopieren und SSID und Passwort eintragen. `secrets.h` steht in `.gitignore`.
5. **Hochladen:** `firmware/IsmeDisplay/IsmeDisplay.ino` öffnen und hochladen.

Die Zugangsdaten sind fest in der Firmware. Wer sie ändern will, muss neu kompilieren.

### Ohne Arduino IDE

Der Workflow [`firmware.yml`](.github/workflows/firmware.yml) baut bei jedem Push und stellt `IsmeDisplay.uf2` als Artefakt bereit. Diese Datei enthält aber nur die Platzhalter-Zugangsdaten aus der Vorlage und verbindet sich deshalb nicht mit dem Bordnetz. Sie eignet sich, um Anzeige und Tasten auszuprobieren. Zum Aufspielen den Ein-Schalter zweimal schnell nach unten schieben: Das Wio Terminal erscheint dann als Laufwerk „Arduino“, auf das man die UF2-Datei kopiert.

## Stromversorgung

Das Wio Terminal braucht 5 V über USB-C. Für den Dauerbetrieb an Bord ist ein Wandler von 12 bzw. 24 V auf USB nötig.

## Aufbau

```
firmware/IsmeDisplay/
  IsmeDisplay.ino      Einstieg, ruft appSetup() und appLoop() auf
  app.cpp              Anzeige, Tasten, WLAN/UDP, Summer, Einstellungen im Flash
  core.h, core.cpp     Kernlogik ohne Arduino: NMEA, YDWG-02-RAW, Temperatur-PGNs,
                       Duplikatfilter, Abgasalarm, Datenausfall, Formatierung
  config.h             Version, Standardwerte, Zeiten
  secrets.h.example    Vorlage für die WLAN-Zugangsdaten
tests/host/            PC-Tests der Kernlogik (g++), mit denselben Testdaten wie die App
```

```
make -C tests/host
```

## Grenzen

- Die eingebauten Schriften kennen nur ASCII. Die Punkte von ä, ö, ü und das Gradzeichen werden deshalb von Hand gezeichnet.
- Die Firmware ist kompiliert, aber noch nicht auf einem Gerät getestet. Am Gerät zu prüfen sind vor allem: Empfang der UDP-Broadcasts über den WLAN-Chip, Lage und Lesbarkeit der Texte, Tastenbelegung und Lautstärke des Summers (`BUZZER_DUTY` in `config.h`).
