# MeshCom bis Ende 2027: Ideen für eine gemeinsame Roadmap

**Dies ist die Ideensammlung von DK5EN für die Zeit bis Ende 2027, noch keine Roadmap des Teams. Jeder kann und soll dazu eine Meinung haben; was wirklich auf die Roadmap kommt, entscheidet die Diskussion mit OE1KBC, OE1KFR und der Community.**

Stand: 04.11.2026, geprüft gegen `fork-dev` und `docs/BACKLOG.md`. Die Reihenfolge der Punkte ist **keine** Priorität: Alles hier ist Backlog, eine Reihenfolge gibt es noch nicht.

## Handlungsbedarf

- **Betreiber und Nutzer:** Heute ist nichts zu tun. Nichts in diesem Papier ist beschlossen.

## Kernfakten

- **Der Kanal ist der Engpass, nicht der Speicher:** Ein 80-Byte-Frame braucht bei SF11/BW250 rechnerisch 1,1 s. Jedes neue Feature kostet Sendezeit.
- **Ein Drittel der Flotte läuft unter 4.40a:** 573 von 1724 meldenden Knoten, davon 330 auf 4.35p (mcmap, 04.11.2026). End of life muss daher diskutiert werden.
- **61 % der Knoten sind klassische ESP32:** Das begrenzt, was jede Funktion an Speicher kosten darf. Details: [Hardware-Leitlinien](hardware-guiding-principles.md).
- **Unser Platz ist der Amateurfunk:** Rufzeichen, Klartext, ein gemeinsames Netz. Vergleich: [Marktanalyse](market-analysis-meshtastic-meshcore-20261104.md).
- **Der Server gehört zum Konzept:** Viele Punkte unten gehen nur mit dem Server. Für den Server ist Kurt zuständig.
- **Die offizielle App gehört auch dazu:** Einige Punkte betreffen auch die App am Handy
- **Das Ökosystem gehört auch dazu:** wir haben viel Chat Clients, die brechen, wenn wir etwas ändern

## Was das Produkt sein soll

- **Chat:** an alle (`*`), an Gruppen, persönlich.
- **Chat an Themen:** Tags wie `#SOTA`.
- **Notfunk (EMCOMM):** Store and Forward im Knoten und am Server, signierte Nachrichten.
- **Daten:** APRS-Wetterdaten und Telemetrie.
- **Infrastruktur:** Das Internet-Netz bleibt auch bei einem Serverausfall verfügbar.

## Was zuerst geklärt werden muss um alte Firmware End-of-Life zu setzen

- **Reicht die Sperre im Server und in der Firmware?** Der Plan: Der Server nimmt Pakete alter Knoten nicht mehr an, neue Firmware verwirft sie.
  - Heute: Frames von Firmware unter 4.35 werden beim Dekodieren verworfen (`src/aprs_functions.cpp:569`).
  - Grenze: Die Prüfung sieht nur die Version des Urhebers. Alte Knoten hören einander weiter, und was sie als Relay weiterleiten, kostet weiter Sendezeit. Der Hop-Fehler alter Builds braucht daher zusätzlich eine Hop-Grenze (IDEE-04).
- **Wann ist das Netz bereit für viele neue Nutzer?** Siehe IDEE-18. Andere Projekte zeigen, dass sich Standardwerte nach dem Zulauf kaum noch ändern lassen.

## Kanal und Zuverlässigkeit

| Id       | Idee                                                                 | Stand heute                                                                       | Zu klären                                                        |
| -------- | -------------------------------------------------------------------- | --------------------------------------------------------------------------------- | ---------------------------------------------------------------- |
| IDEE-01  | Messen, bevor sich das Relay-Verhalten ändert                        | Teilweise: Alters-Logzeilen seit v4.40a.10.23, Histogramme fehlen                 | Was gemessen wird und welche Knoten mitmessen                    |
| erledigt | Sendepuffer als Slot-Pool mit Verfall je Priorität                   | Ausgeliefert in v4.40a.10.23, 93 h auf DK5EN-98 ohne Verluste                     | Nur noch die Verfallszeiten bestätigen                           |
| IDEE-02  | Multi-SF: SF9 trägt das 3,4-Fache von SF11, bei 5 dB weniger Reserve | Nur Konzept                                                                       | Ein Knoten hört nur ein SF: regionales SF oder Backbone-Schicht? |
| IDEE-03  | PN-Priorität: Quittung zuerst, eigener Platz im Puffer, Wiederholung | Nur Konzept; eine PN verliert heute beim ersten Relay die Priorität               | Missbrauchsschutz durch ein Limit je Quelle                      |
| IDEE-04  | Hop-Grenze gegen kreisende Frames alter Firmware                     | Nur Konzept; Ergänzung aus dem Konzeptpapier, nicht von der ursprünglichen Liste  | Passt sie vor die End-of-Life-Sperre?                            |
| IDEE-05  | Rate Limits je Knoten und Nachrichtentyp, im Knoten und am Server    | Nur Idee. Teilstücke: Limits für einzelne Funktionen, QRT-Stopp bei vollem Puffer | Werte erst nach dem Messen; wie schnell, wie viel in 24 h        |
| IDEE-06  | Nutzungsregeln: was auf die QRG gehört, was Spam ist                 | Nur Idee                                                                          | Wer schreibt sie, wer setzt sie durch                            |

## Lebenszyklus der Firmware

| Id        | Idee                                                                         | Stand heute                                                                          | Zu klären                                                          |
| --------- | ---------------------------------------------------------------------------- | ------------------------------------------------------------------------------------ | ------------------------------------------------------------------ |
| IDEE-07   | End of Life für alles vor 4.40a: Server nimmt nicht mehr an, Firmware sperrt | Sperre unter 4.35 besteht. Datum 31.12.2026 nur Arbeitsdatum, Ankündigung durch Kurt | Datum; Hinweis per DM vorab; ein Drittel der Flotte ist betroffen  |
| IDEE-08   | Ablaufdatum in künftigen Builds, mit Riegel                                  | Nur Idee für später. Bestehende Knoten erreicht es nie                               | Wer verlängert; Uhrzeit fehlt manchem Knoten; Versionsfeld zu grob |
| IDEE-09   | Fester, langsamerer Release-Takt                                             | Nicht abgestimmt; 20 Tags im Fork seit 02.10.2026. Der Fork soll langsamer werden    | Welcher Takt; gilt er nur für den Fork oder auch für das Projekt?  |
| `SB-05`   | Rollback: zweiter App-Slot für Auto Update                                   | Offen; heute ein App-Slot; ändert die Partitionstabelle auf jedem Board              | Passt das auf 4-MB-Boards?                                         |
| erledigt  | Manuelles Update ohne vorherigen Download                                    | Web GUI auf ESP32; nRF52 hat keinen autonomen Weg                                    | nRF52 bleibt außen vor                                             |
| IDEE-10   | Update aus der App über BLE                                                  | nRF52 per Nordic DFU (`NRF-BLEDFU`), Dienst ohne Anmeldung. ESP32 fehlt              | Lohnt sich ESP32? Absicherung des Dienstes                         |
| `WF-05`   | Web Flasher mit Board-Erkennung                                              | Kampagne läuft; CI-Prüfung und Windows-Lauf offen                                    | Nur noch Abschluss                                                 |
| auf dev   | Web-Seiten als gzip im Flash                                                 | Auf `fork-dev`, nicht released. Spart etwa 205 KB auf ESP32 und 63 KB auf dem RAK    | Upstream-PR                                                        |
| `DEF-ARD` | Migration auf Arduino 3.x                                                    | Offen; vier Boards stehen auf 2.0.14                                                 | Kosten an Speicher auf klassischem ESP32                           |

## Nachrichten und Anwendungen

| Id          | Idee                                                      | Stand heute                                                                           | Zu klären                                                                   |
| ----------- | --------------------------------------------------------- | ------------------------------------------------------------------------------------- | --------------------------------------------------------------------------- |
| `HASH-TAG`  | Tags: erst als Anzeigefilter, dann `#TAG` im Zielfeld     | User Story, nichts entschieden                                                        | Tags sparen keine Sendezeit, sie ändern die Bedienung. App und Server?      |
| IDEE-11     | Echte Statusmeldungen für BLE- und Web-Clients            | Clients vergleichen heute Text; "auf der Luft" gibt es gar nicht                      | Format; Voraussetzung für Tags, EMCOMM und S&F in der App                   |
| IDEE-12     | EMCOMM: Prioritätsflag und signierte Nachrichten          | Nur Konzept. Signatur rechnerisch 0,8 s pro Frame, also nur für EMCOMM                | Rechtliche Prüfung für DL und OE; Prüfaufwand auf klassischem ESP32         |
| `SNF-GW`    | Store and Forward am Server                               | Firmware-Seite ist upstream (#1238), standardmäßig aus. Der Server muss PNs zustellen | Kurt: Soll der Server PNs gezielt an Store-Knoten geben?                    |
| IDEE-13     | Pfadsuche und mehr als 4 Hops für persönliche Nachrichten | Nur Konzept                                                                           | Next-Hop oder Quellroute; hilft erst, wenn genug Relays neue Firmware haben |
| IDEE-14     | APRS: echter WX-Frame, Position auf mehreren Zeilen       | Sensoren UV und Wind sind upstream (#1251). Offen ist das Format auf der Luft         | Wandelt das Gateway oder der Server um?                                     |
| `RM-01..07` | Fernwartung RM1 ausbauen                                  | RM1 ist upstream, standardmäßig aus                                                   | Welche Befehle fehlen wirklich?                                             |

**Vertrauen bei EMCOMM ist Sache der OMs, nicht einer Zentrale.** Zwei OMs sprechen miteinander und bestätigen, dass ein Hash zum Rufzeichen gehört und echt ist. Es gibt kein zentrales Verzeichnis. Der Knoten speichert die bestätigten Schlüssel, in der Web GUI sichtbar.

## Server und Infrastruktur

Für den Server ist Kurt zuständig. Die Punkte hier sind Wünsche an ihn, keine Zusagen.

| Id       | Idee                                                          | Stand heute                                                                           | Zu klären                                                  |
| -------- | ------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ---------------------------------------------------------- |
| IDEE-15  | Hochverfügbarkeit: zwei Master-Server und Hub-Server pro Land | Nur Idee                                                                              | Machbarkeit und Betrieb, Entscheidung bei Kurt             |
| IDEE-16  | KEEP parallel an zwei Hub-Server                              | Nur Idee; braucht Firmware und Server                                                 | Doppelte Pakete am Server; Mehrkosten im Gateway           |
| erledigt | Links zwischen Gateways im INTERLINK-Feed                     | In der Firmware gelöst (`HEY-UP-01`, #1243). Wirkt, sobald Gateways aktualisiert sind | Ursprünglich als Änderung am Hub geplant, nicht mehr nötig |

## Topologie

| Id         | Idee                                                                                 | Stand heute                         | Zu klären                                                       |
| ---------- | ------------------------------------------------------------------------------------ | ----------------------------------- | --------------------------------------------------------------- |
| `MC5-TOPO` | Nachbarschaftsmatrix als Grundlage für Pfadsuche, Routing und Karte                  | Stufe 1 läuft, der Rest ist Konzept | Keine weitere Arbeit vor einer Entscheidung                     |
| IDEE-17    | Rollen im HEY: Gateway, Höhenstandort, mobil, nur Client; gewählte Relays in Städten | Nur Konzept                         | Voraussetzung für Quellrouten und Zellen; wer legt Rollen fest? |

## Wachstum und Abgrenzung

| Id      | Idee                                                                        | Stand heute                                                              | Zu klären                                                              |
| ------- | --------------------------------------------------------------------------- | ------------------------------------------------------------------------ | ---------------------------------------------------------------------- |
| IDEE-18 | Ausbau USA und Japan                                                        | Kontakte in die USA laufen über Kurt. Australien ist zu dünn besiedelt   | Wie verkraftet das Netz viele neue OMs in kurzer Zeit? Band und Regeln |
| IDEE-19 | Hardware-Roadmap nach festen Leitlinien                                     | Entwurf: [Hardware-Leitlinien](hardware-guiding-principles.md)           | Stufen, Abkündigung, Umgang mit klassischem ESP32                      |
| IDEE-20 | Gleichstand mit Meshtastic und MeshCore: wo und warum wir uns unterscheiden | Entwurf: [Marktanalyse](market-analysis-meshtastic-meshcore-20261104.md) | Welche Unterschiede sind gewollt, welche ein Rückstand?                |

## Was nicht geprüft ist

- **Rechenwerte:** Sendezeiten und Kapazitätsfaktoren sind berechnet, nicht gemessen.
- **Flottenzahlen:** Momentaufnahme der mcmap vom 04.11.2026; länger stille Knoten fehlen.
- **Aufwand:** Dieses Papier nennt bewusst keine Aufwände und Termine. Schätzungen für einen Teil der Punkte stehen im Konzeptpapier.
- **Rechtliches:** Signaturen auf 70 cm in DL und OE und die Regeln in den USA und Japan sind nicht geprüft.

Hintergrund: [Konzeptpapier Mesh Evolution](mesh-evolution-concept-20261021.md), [Rückstau und Statusmeldungen](backpressure-protocol.md), [Backlog](BACKLOG.md).
