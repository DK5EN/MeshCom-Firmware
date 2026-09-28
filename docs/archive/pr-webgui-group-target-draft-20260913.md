# PR-Entwurf: Web-GUI: Gruppennummer bleibt nach dem Senden im Zielfeld

Eingereicht 2026-09-13 als [PR #1144](https://github.com/icssw-org/MeshCom-Firmware/pull/1144) (Branch `pr-webgui-group-target-20260913`, ein Commit auf `dev`
`893cffd0`).

Ziel: `icssw-org/MeshCom-Firmware`, Branch `dev`. Ein Commit, nur
`src/web_functions/web_functions.cpp`.

## Symptom

Feldmeldung DJ8MEH, 2026-09-11. In der Web-GUI waehlt man ueber die Reiter eine Gruppe; die
Reiterleiste schreibt die Gruppennummer in das Zielfeld `sendcall`. Nach dem Senden leert der
`sendmessage ok`-Handler das Zielfeld zusammen mit dem Nachrichtentext. Wer direkt eine zweite
Nachricht tippt und abschickt, sendet sie mit leerem Ziel, und ein leeres Ziel ist auf dem
Knoten der Broadcast an `*`. Die Nachricht geht ungewollt an alle.

## Ursache

`deliver_scaffold()` liefert die Funktion `sendMessage()` als Inline-JavaScript aus. Der
Erfolgszweig setzt `sendcall` und `messagetext` bedingungslos auf `""`. Das war fuer den
Direktnachrichten-Fall gedacht (Rufzeichen einmal eingeben, senden, Feld frei), passt aber
nicht zum Gruppen-Reiter, der das Ziel als Zustand vorgibt.

## Aenderung

- `src/web_functions/web_functions.cpp`, `deliver_scaffold()`: der Erfolgszweig leert `sendcall`
  nur noch, wenn der Inhalt **nicht** rein numerisch ist (`/^[0-9]+$/`). Eine Gruppennummer
  bleibt stehen, ein Rufzeichen wird wie bisher geleert. Der Nachrichtentext wird weiterhin
  geleert, der Zeichenzaehler aktualisiert. Sonst keine Aenderung, kein zusaetzlicher Request.

## Nachweis

Build auf `dev` `893cffd0` + Commit: `heltec_wifi_lora_32_V3`, `wiscore_rak4631` sauber.

Funktionsnachweis mit jsdom gegen einen laufenden Knoten (Heltec V3 DK5EN-98, Fork-Image mit
genau dieser `sendMessage()`-Zeile): das echte Scaffold-JS wird vom Knoten geladen, der
XHR-Erfolgspfad gestubbt. 30 Pruefungen bestanden, darunter:

```
PASS sendcall follows group tab
PASS send to group keeps sendcall  [{"sc":"9","mt":""}]
PASS send to DM call clears sendcall
```

Gruppe 9 bleibt nach dem Senden im Zielfeld, der Text ist geleert; ein Rufzeichen als Ziel wird
wie bisher geleert.

Im Browser gegen denselben Knoten (Meshcom 4.35t, Build Sep 12 2026): Reiter `9` gewaehlt, das
Zielfeld zeigt `9`; kein Testversand ins Netz, der Sendepfad ist ueber den Harness abgedeckt.
