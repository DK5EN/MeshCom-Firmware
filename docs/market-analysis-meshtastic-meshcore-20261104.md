# Marktanalyse: Meshtastic und MeshCore gegen MeshCom

**MeshCom muss nicht beide nachbauen. Sein Platz ist das offene Amateurfunk-Netz mit Rufzeichen und Klartext, das beide anderen nur am Rand bedienen. Bei Kanallast, Zustellstatus und Update-Disziplin haben die anderen dagegen Vorsprung, den wir aufholen sollten.**

Stand: 04.11.2026. Die Begründungen in der Spalte "Warum" sind ein Vorschlag von DK5EN zur Diskussion, keine Teamposition. Quellen und Belege je Aussage: [Rechercheprotokoll](market-research-meshtastic-meshcore-20261104.md).

## Kernfakten

- **Amateurfunk ist unser Unterschied:** Meshtastic hat einen Modus für Lizenzierte, aber getrennt vom übrigen Netz und die neuen Amateurfunk-Bänder nur in der Alpha 2.8. MeshCore hat keinen Amateurfunk-Modus und kein Rufzeichenfeld.
- **Beide lernen für Direktnachrichten einen Weg:** erst Flut, dann Pfad, beim letzten Versuch wieder Flut. MeshCom flutet jede PN und jede Quittung.
- **Rollen sind der stärkste Kapazitätshebel:** Bei MeshCore leiten Endgeräte nicht weiter. Bei MeshCom und im Standard von Meshtastic tun sie es.
- **Bei Stau drosselt Meshtastic zuerst Metadaten, nicht Text:** Telemetrie ist aus, Position wird Opt-in, Intervalle und Hops skalieren mit der Last.
- **Eine formale End-of-Life-Regel wurde bei keinem der beiden gefunden:** Meshtastic sperrt über die Apps und ab 2.8 auf der Luft, MeshCore setzt auf Absprachen in der Region.
- **Schneller Zulauf hat anderswo Netze zersplittert:** nach Preset, Frequenz und inzwischen Protokoll. Mehr dazu unter "Was bei schnellem Wachstum passiert ist".

## Vergleich

Spalte MeshCom: `fork-dev` am 04.11.2026. Meshtastic: stabil 2.7.26, neu 2.8.2 Alpha. MeshCore: v1.18.0.

| Thema             | MeshCom                                                                         | Meshtastic                                                                            | MeshCore                                       | Warum wir anders sind, und ob das so bleiben soll                                 |
| ----------------- | ------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ---------------------------------------------- | --------------------------------------------------------------------------------- |
| Rundsprüche       | Flut, Abbruch beim Mithören einer Weiterleitung (`--nbrrelay`)                  | Verwaltete Flut mit Mithören                                                          | Flut nur über Repeater                         | Einfach und robust. Offen: Rollen im HEY, siehe Roadmap                           |
| Hops              | Text 4, Position 2, Obergrenze 7                                                | Standard 3, maximal 7                                                                 | bis 64                                         | Mehr Hops kosten Kanal. Der Wunsch nach mehr als 4 Hops gilt nur für gezielte PNs |
| Direktnachrichten | Flut, Wiederholung bis 3 x 40 s                                                 | Next-Hop seit 2.6                                                                     | Quellroute                                     | **Rückstand.** Kein Grund außer Historie, der Pfad steht schon im Frame           |
| Wer leitet weiter | Jeder außer `--mesh off`                                                        | Client nur wenn sonst keiner, Router immer                                            | Nur Repeater und Room Server                   | **Offene Entscheidung.** Wichtig für dichte Städte                                |
| Kanalprofil       | Eine Einstellung, SF11/BW250, Länderprofile weichen ab                          | Mehrere Presets, Empfehlung zum Wechsel ab etwa 60 Knoten                             | Vorschläge je Region, schmal                   | Alle hören alle. Preis: feste Kapazität, siehe Multi-SF                           |
| Lastregeln        | Slot-Pool mit Verfall je Priorität, keine Drossel nach Kanalauslastung gefunden | Grenzen bei 25 und 40 %, Intervalle ab 40 Knoten skaliert, Limit je Sender als Opt-in | 50 % Airtime-Budget, Regeln durch Betreiber    | **Rückstand.** Rate Limits je Knoten und Nachrichtentyp sind vorgeschlagen        |
| Zustellstatus     | Text-Rückkanal, `0x41` für Server und Empfänger, kein "auf der Luft"            | Fünf Stufen in der Android-App                                                        | Quittungs-Tag im Companion-Protokoll           | **Rückstand.** Echte Statusmeldungen sind vorgeschlagen                           |
| Store and Forward | Store-Knoten mit `mbox` und `sto`, standardmäßig aus; Server-Teil fehlt         | Modul nur ESP32 mit PSRAM                                                             | Room Server                                    | Für EMCOMM wichtig. Vorteil, sobald der Server mitmacht                           |
| Verschlüsselung   | Keine, Klartext                                                                 | Standard, im Lizenz-Modus aus                                                         | Immer                                          | **Bewusst so.** Amateurfunk verlangt offene Inhalte                               |
| Absender-Nachweis | RM1 mit 64-Bit-HMAC nur für Fernwartung, Nachrichten ungezeichnet               | XEdDSA, 66 Byte; Standard signiert seit 2.8.2 nicht mehr                              | Nur Adverts signiert                           | Vorgeschlagen: Ed25519, Vertrauen von OM zu OM                                    |
| Fernwartung       | RM1 über LoRa, standardmäßig aus                                                | Bis zu 3 Admin-Schlüssel                                                              | Passwort, Standard ist `password`              | Tragbar. RM1 ist nicht schlechter als die anderen                                 |
| Internet          | Gateway zum zentralen Server, Teil des Konzepts                                 | MQTT optional, öffentlicher Broker nur Zero-Hop                                       | Keine MQTT-Funktion in der Firmware            | **Bewusst so,** Netz und Karte hängen am Server. Risiko: Ausfallsicherheit        |
| Firmware-Update   | Web Flasher, Auto Update ESP32 über WLAN mit Safeboot, nRF52 per BLE-DFU        | Web Flasher, BLE und WLAN aus der App                                                 | Web Flasher, Hotspot, Nordic DFU               | Selbstständiges Update ist ein **Vorsprung.** Fehlt: BLE aus der App              |
| Rollback          | Nein, ein App-Slot, kein automatischer Rückweg                                  | Nicht dokumentiert                                                                    | Nicht dokumentiert, optional OTAFIX-Bootloader | Auch die anderen haben es nicht                                                   |
| Mindestversion    | Frames unter 4.35 werden beim Dekodieren verworfen                              | App-Sperre, ab 2.8 auf der Luft                                                       | Keine, nur regionale Absprache                 | Vorschlag: 4.40a, siehe Roadmap                                                   |
| Release-Takt      | 20 Tags seit dem 02.10.2026 im Fork                                             | 19 Tags in 12 Monaten, 4 davon zurückgezogen                                          | 11 Versionen in 12 Monaten                     | **Der Fork ist zu schnell.** Takt ist vorgeschlagen                               |
| Governance        | Kleines Kernteam um icssw.org, MIT-Lizenz                                       | Meshtastic LLC mit Marke, Freiwillige                                                 | Fünf Kernleute, Markenstreit seit 04/2026      | Unser Vorteil: kein Markenstreit. Lücke: keine schriftlichen Regeln               |

## Was bei schnellem Wachstum passiert ist

Wichtig für die Frage USA und Japan. Die Belege sind überwiegend Berichte von Betreibern, keine kontrollierten Messungen.

- **Last kommt vor allem von Metadaten:** In Wellington lag LongFast in Spitzen über 65 % Kanalauslastung, überwiegend NodeInfo, Position und Telemetrie.
- **Neue Nutzer bleiben bei den Standardwerten:** Eine Änderung des Standards erreicht vorhandene Knoten nicht. Zwischen der ersten Erwägung bei Meshtastic und der Alpha lagen etwa 17 Monate.
- **Die Antworten zersplittern das Netz:** Lokale Netze trennen sich nach Preset, nach Frequenz und nach Protokoll. Philly Mesh hat am 02.09.2026 den Wechsel der Infrastruktur zu MeshCore angekündigt.
- **Die FCC-Frage ist offen:** 47 CFR 15.247 nennt 500 kHz Mindestbandbreite für digitale Modulation in 902 bis 928 MHz. Ob das hier gilt, ist umstritten, eine Aussage der FCC wurde nicht gefunden.
- **Für Lizenzierte kann anderes gelten:** Ob Part 97 auf 33 cm einen anderen Weg öffnet, braucht eine rechtliche Prüfung.
- **Abgeleitet für MeshCom:** Vor einer Öffnung für große Regionen sollten Rate Limits, Standardintervalle und Rollen stehen, weil sich Standards später kaum noch ändern lassen.

## Was wir übernehmen könnten

Das sind Kandidaten für die Diskussion, keine Empfehlung zur Reihenfolge.

- **Metadaten vor Text drosseln:** Verhalten von Meshtastic, passt zu den vorgeschlagenen Rate Limits.
- **Next-Hop vor Quellroute prüfen:** Kostet ein Feld statt eines ganzen Pfads pro Frame. Konzept: `mesh-evolution-concept-20261021.md`, Abschnitt 4.7.
- **Rollen statt "jeder leitet weiter":** MeshCore zeigt den Hebel, Meshtastic den Schaden falsch gesetzter Rollen.
- **Stufen für Hardware:** Siehe [Hardware-Leitlinien](hardware-guiding-principles.md).
- **Nicht übernehmen:** Passwort-Standardwerte, geschlossene Apps, bezahlte Freischaltungen.

## Was nicht geprüft ist

- **Absolute Größen:** Die Kartenzähler von Meshtastic und MeshCore sind nicht vergleichbar und keine Nutzerzahlen. Eine Zeitreihe für das Wachstum in den USA wurde nicht gefunden.
- **Japan:** Der Text von ARIB T108 wurde nicht gelesen, die Werte stammen aus einem MeshCore-Issue. Der Stand der Community dort ist unbekannt. Regeln für Amateurfunk in Japan wurden nicht untersucht.
- **Zitate aus Blogs:** Teile von Blogs und Presse wurden über eine Zusammenfassung gelesen. Vor einem Zitat nach außen nachprüfen. Stichproben an den Quellen: `SECURITY.md`, Release 2.8.2, `airtime.h`, MeshCore-FAQ bestätigen die Aussagen.
- **MeshCom-Spalte:** Das Fehlen einer Drossel nach Kanalauslastung ist ein Suchergebnis im Code, kein Audit.
- **Rechtliches:** Nichts in diesem Papier ist eine rechtliche Bewertung.
