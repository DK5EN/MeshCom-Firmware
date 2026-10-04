# Regressions-Suite: offene Punkte (abgeschlossen)

> **Archiviert 2026-10-04.** Alle acht Punkte sind abgeschlossen oder entschieden (Statusspalte
> unten). Die Regressions-Suite ist Stand `tools/regression.sh --stage 1,2`: 48 Envs, 1677
> Unity-Fälle, 553 pytest, Golden-Selftest 48 Kommandos; Stufe 3 grün auf RAK, T-Beam, T-Deck
> (`docs/bench-20261003-stage3.md`). Offene Folgearbeit steht in `docs/BACKLOG.md`.

Stand 2026-10-03, nach Commit `cfcfcb3c` auf `fork-dev` (Ende-zu-Ende-Runner
`tools/regression.sh`, Inventur `docs/test-suite-map.md`, Skill `/full-regression`).
Stufen 1 und 2 sind grün: 48 Envs, 1627 Unity-Fälle, 467 pytest, 24 node-Fälle,
Golden-Selftest. Dieses Papier hält fest, was danach noch zu tun ist, in der
Reihenfolge, in der es sinnvoll abgearbeitet wird. Jeder Punkt hat ein
Akzeptanzkriterium, damit "fertig" eindeutig ist.

| #   | Punkt                                   | Aufwand | Braucht        | Status                                                                                                                                            |
| --- | --------------------------------------- | ------- | -------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | Stufe 3 erstmals an Hardware fahren     | 2-3 h   | Bench am USB   | erledigt 2026-10-04, `0968cccf`..`34ea128e` (REG-01, Läufe 6-9); TM-43 `--extudp --soak-seconds 120` am RAK 2026-10-04 nachgeholt, siehe Nachtrag |
| 2   | Rohlogs für topo_shadow doppelt sichern | 15 min  | rpizero        | erledigt 2026-10-03, `6f0e2696` (REG-02): Kopie in `~/meshlog/dk5en-98-nbr/`, Suchreihenfolge Downloads, meshlog, rpizero                         |
| 3   | DR-16 entscheiden, Phase-Flag entfernen | 1-2 h   | nichts         | erledigt 2026-10-04, Weg (a): `both-valid`, `test_drift_dr16_gates`, `--phase` raus, Lint sauber                                                  |
| 4   | webgui_badge_test in Stufe 3 aufnehmen  | 1 h     | ESP32 mit WiFi | erledigt 2026-10-03, `6f0e2696` (REG-04); Badge PASS auf beiden WiFi-Boards in Lauf 8/9                                                           |
| 5   | TM-26 Mesh-Exchange als Werkzeug        | 3-4 h   | 2+ Nodes       | erledigt 2026-10-03, `6f0e2696` (REG-05); 6/6 Paare mit drei Nodes am 2026-10-04                                                                  |
| 6   | Skill versionieren                      | 30 min  | nichts         | erledigt 2026-10-03, `6f0e2696` (REG-06): `.gitignore`-Allowlist, Datei getrackt                                                                  |
| 7   | CI auf den Runner umstellen             | 1 h     | GitHub Actions | erledigt 2026-10-03, `6f0e2696` (REG-07); Workflow bleibt aus, Operator-Entscheid 2026-10-04                                                      |
| 8   | Zähl-Heuristik im Runner schärfen       | 30 min  | nichts         | erledigt 2026-10-03, `6f0e2696` (REG-08): echte Zahlen aus `selftest.sh` und den nbrlog-Skripten                                                  |

---

## 1. Stufe 3 erstmals an Hardware fahren

**Warum.** `tools/bench/bench_suite.py` ist bisher nur per `--dry-run`, Fake-Runner
und 16 pytest-Fällen geprüft. Die Aufrufe der Harnesse (`tdeck_harness`,
`oled_harness`, `rak_harness`, `ota_regression`, `identity_guard`) sind aus deren
argparse abgeleitet, nie gegen ein Gerät gelaufen. Ein Fehler in der Reihenfolge
(Identity-Guard vor Harness, Peer-Port für den RAK) oder im Port-Matching zeigt
sich erst am Tisch.

**Vorgehen.**

1. Einen einzelnen Node anstecken, am besten den T-Beam DK5EN-92 (CH9102, kein
   Native-USB-Reset-Problem). `python3 tools/bench/bench_suite.py --dry-run` muss
   ihn per USB-Seriennummer finden und drei Schritte planen: identity, oled harness,
   ota regression.
2. `/full-regression 3` ohne `--flash`. Erwartung: identity OK, Harness PASS,
   OTA PASS, `bench-summary.json` mit drei Einträgen.
3. Dann RAK DK5EN-90 dazu: der Plan muss `--peer-port` auf den T-Beam setzen, der
   `mheard`-Fall des RAK muss den T-Beam sehen.
4. Erst danach `--flash` an einem Node ausprobieren und das esptool-Kommando
   (460800, nur App bei 0xC0000) im Log gegenlesen.
5. Zum Schluss `--extudp --soak-seconds 120` am RAK (TM-43).

**Akzeptanz.** Ein kompletter `/full-regression all`-Lauf mit mindestens zwei
Nodes endet mit `RESULT: PASS`, die Laufzeit steht im Papier, Abweichungen zu den
erwarteten Zahlen aus `docs/automation-runner-runbook.md` 2.3 sind notiert.

**Bekannte Fallen.** Port-Öffnen rebootet jeden ESP32; der RAK braucht DTR; ein von
`serial_capture.py` gehaltener Port wird per `lsof` erkannt und übersprungen; kein
Testverkehr außer Gruppe `TEST`; TX-Leistung der Bench-Nodes höchstens 2 dBm.

## 2. Rohlogs für topo_shadow doppelt sichern

**Warum.** Der Vollfenster-Fall in `test_topo_shadow` liest vier Rohlogs
(DK5EN-98, 21.-24.09.2026, 54 MB) aus `~/Downloads/dk5en-98-nbr/`. Die
Masterkopie liegt nur auf `rpizero:~/meshlog/dk5en-98/`. Fällt die SD-Karte des
Pi aus, ist der Fall dauerhaft ein `TEST_IGNORE`, und niemand merkt es, weil
IGNORE nicht rot ist.

**Vorgehen.** Kopie nach `~/meshlog/dk5en-98-nbr/` legen (dort liegen die anderen
Mitschnitte schon), optional eine dritte auf die externe Platte oder in die
iCloud-Ablage. `tools/regression.sh` so ergänzen, dass `ensure_topo_raw` erst
`~/meshlog/dk5en-98-nbr/` prüft, dann `~/Downloads/`, dann rpizero.

**Akzeptanz.** `rm -rf ~/Downloads/dk5en-98-nbr && tools/regression.sh --stage 1`
meldet `topo_shadow raw window OK` ohne Netzzugriff, und der Lauf zeigt
`0 skipped`.

## 3. DR-16 entscheiden, Phase-Flag entfernen

**Warum.** `drift_matrix_lint.py --phase implementation` in `selftest.sh` ist laut
eigenem Kommentar temporär. Seit der Inventur hält genau eine Zeile das Flag am
Leben: DR-16, die nRF52-Portierung der `bAllStarted`/`extra_hey_time`-Gates im
Hauptloop, Verdict `nrf52-changes`. Der Code-Kommentar in
`src/nrf52/nrf52_main.cpp` (bei `bTeleFirst`) sagt aber, die Portierung sei
bewusst unterblieben. Verdict und Code widersprechen sich.

**Vorgehen.** Zwei Wege, einer ist zu wählen:

- **a) Re-Entscheidung.** Verdict auf `both-valid` oder `documented non-change`
  setzen, `after_expect` mit der Begründung aus dem Code-Kommentar füllen,
  `asserting_test` bleibt leer, weil das Lint für diese Verdicts keinen Test
  verlangt (prüfen: `drift_matrix_lint.py` Regelteil für `both-valid`).
- **b) Portieren und testen.** Nur, wenn der fehlende `bAllStarted`-Schutz auf
  nRF52 ein reales Fehlverhalten hat (HEY/Telemetrie vor Netzbereitschaft). Dann
  gehört ein Fall in `test_hey_policy` oder eine neue Suite, und die Zeile nennt ihn.

Danach in `selftest.sh` die Zeile ohne `--phase` fahren und den Kommentarblock
kürzen.

**Akzeptanz.** `python3 test/golden/drift_matrix_lint.py` ohne Flag meldet
`0 violation(s)`, `selftest.sh` enthält kein `--phase` mehr, Stufe 1 grün.

## 4. webgui_badge_test in Stufe 3 aufnehmen

**Warum.** `tools/webgui_badge_test.js` prüft die Unread-Badge-Logik der Web-GUI
gegen das echte Scaffold eines laufenden Nodes (Changelog-Item 195). Er braucht
jsdom und einen Live-Node und hängt deshalb an keinem Gate. jsdom gibt es seit
heute im Cache `~/.cache/meshcom-jsdom`.

**Vorgehen.** In `bench_suite.py` einen Schritt `webgui badge <node>` für jeden
ESP32-Node mit `host` in `fleet.json` planen:
`NODE_PATH=... node --insecure-http-parser tools/webgui_badge_test.js http://<host>/`.
Nach dem OTA-Schritt einordnen, weil der Node danach sicher wieder erreichbar ist.
Test in `test_bench_suite.py` ergänzen (Schritt nur bei `host`, nie für den RAK).

**Akzeptanz.** Bei angestecktem T-Beam erscheint der Schritt im Plan und endet
mit `OK`; ohne `host` fehlt er.

## 5. TM-26 Mesh-Exchange als Werkzeug

**Warum.** Der Cross-Node-LoRa-Austausch (jeder Node sendet `--sendpos`, alle
anderen müssen ihn in `--mheard` zeigen) ist der einzige Bench-Schritt, der die
Funkstrecke zwischen den Boards beweist. Er existiert nur als Skizze in
`docs/automation-runner-runbook.md` 2.4 und läuft von Hand.

**Vorgehen.** `tools/bench/mesh_exchange.py`: alle Ports aus `fleet.json` öffnen
(RAK mit DTR), 50 s warten, `--loradebug on`, reihum `--sendpos` mit 25 s
Abstand, `--mheard` je Node, Matrix "wer hat wen gehört" als JSON, Exit 1, wenn
ein Paar fehlt. In `bench_suite.py` als letzter Schritt, wenn mindestens zwei
Nodes angesteckt sind. Die Sende-Teile des Harnesses müssen `identity_guard`
passieren und bleiben bei 2 dBm.

**Akzeptanz.** Mit drei Nodes ergibt die Matrix 3x2 Treffer, der Schritt steht im
`bench-summary.json`, Laufzeit unter fünf Minuten.

## 6. Skill versionieren

**Warum.** `.claude/commands/full-regression.md` liegt in einem gitignorten
Verzeichnis. In einem zweiten Checkout, einem Worktree oder nach einem
Festplattenwechsel gibt es den Skill nicht, nur `tools/regression.sh`.

**Vorgehen.** Entweder die Ignore-Regel für `.claude/commands/` aufheben und die
Skills committen (dann auch `flash-rak`, `release-firmware` usw. prüfen, ob sie
Geheimnisse oder private Pfade enthalten), oder den Skill-Text als
`docs/skills/full-regression.md` ablegen und per Symlink einbinden. Erste
Variante ist einfacher, zweite hält `.claude/` sauber.

**Akzeptanz.** `git clone` in ein frisches Verzeichnis, `claude` starten,
`/full-regression --list` druckt den Plan.

## 7. CI auf den Runner umstellen

**Warum.** `.github/workflows/ci-build.yml` fährt nur `native` und `native_aprs`,
zwei von 48 Envs. Beide Workflows sind derzeit deaktiviert (Fork-Releases sind
manuell), aber falls CI wieder aktiv wird, soll sie dasselbe prüfen wie der Tisch.

**Vorgehen.** Job `Unit-Tests (nativ)` durch `tools/regression.sh --stage 1,2`
ersetzen. Vorbedingungen im Runner: `uv`, `node` 20+, `npm`, Python 3.11 mit
`pyserial` (nur für den Import in `bench_suite.py`). Der scp nach rpizero
schlägt in CI fehl und muss als `SKIP` durchgehen, das tut er schon. Laufzeit
auf einem GitHub-Runner messen; die 48 Envs bauen dort kalt.

**Akzeptanz.** Ein manuell ausgelöster Workflow-Lauf endet grün und zeigt die
Unity-Zeile mit 48 Envs im Log.

## 8. Zähl-Heuristik im Runner schärfen

**Warum.** `step_detail` in `tools/regression.sh` rät die Fallzahl aus dem Log.
Beim Golden-Selftest meldet es "15 checks", tatsächlich sind es 115 Selbsttests
plus 17 Lints. Die Zahl ist nicht falsch im Sinne von rot/grün, aber sie lässt
den Leser die Abdeckung kleiner einschätzen, als sie ist.

**Vorgehen.** `selftest.sh` eine Schlusszeile `selftest: N scripts, M checks`
ausgeben lassen, die der Runner direkt übernimmt. Analog für die nbrlog-Skripte,
die heute nur "ok" liefern.

**Akzeptanz.** Die Summentabelle nennt für jeden Schritt eine echte Zahl, keine
geratene.

---

## Reihenfolge-Empfehlung

Punkt 2 und 8 sind kleine Pflegearbeiten für einen ruhigen Abend. Punkt 3 ist
eine Entscheidung, keine Programmierung. Punkt 1 braucht den Tisch und sollte
vor 4 und 5 kommen, weil beide auf einem funktionierenden Stufe-3-Treiber
aufsetzen. Punkt 6 und 7 sind unabhängig und können jederzeit laufen.

Erledigte Punkte bekommen in der Tabelle oben den Status `erledigt <Datum>` und
einen Commit-Hash; der Abschnitt bleibt als Begründung stehen.

---

## Nachtrag 2026-10-04: TM-43 Extern-UDP-Soak am RAK

`rak_harness.py --scenario extudp --soak-seconds 120` lief erstmals an DK5EN-90 (Ethernet,
192.168.68.77, Host 192.168.68.58). Senden (`--sendpos`, eigene Nachricht), Empfang
(Nachricht in den TX-Ring, Telemetrie angenommen), alle sieben Reject-Vektoren, Liveness
nach der Vektor-Salve, Soak 122 s mit 40 Datagrammen hinein (10 gültige Nachrichten) und 18
heraus, Stack-HWM rx 463..504 / tx 463..504 Wörter, Uptime monoton, kein Crash.

Der erste Lauf war trotzdem rot: der Restore-Schritt des Harnesses schickte `--extudpip` mit
leerem Argument, weil der Vorzustand der Node ein leeres Feld (nicht `none`) war. Die Firmware
kopierte dann hinter dem Terminator alten Pufferinhalt in `node_extern` (`EXT IP 5 t=219029`).
Harness-Fix: `_restore_ip_arg()` (leer und `none` heißen beide `--extudpip none`), Test in
`tools/bench/test_rak_harness.py`. Die Firmware-Seite (argumentloses `--extudpip` liest über
das Zeilenende) steht als EXT-UDP-Robustheitspunkt im BACKLOG. Zweiter Lauf 2026-10-04 15:00 mit dem Fix: PASS, Restore identisch zum Vorzustand, Soak 122 s mit 40 Datagrammen hinein und 19 heraus.
