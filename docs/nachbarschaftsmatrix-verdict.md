# Nachbarschaftsmatrix, Welle 1+2: Fable-Verdict (2026-09-20)

Advisor-Durchgang ueber den Diff vor dem ersten Commit. Kein kritischer Befund.

## Finding M1: Verdraengung bei Altersgleichstand im selben Frame

- **File:** src/nbr_matrix.cpp nbrTouchRow / nbrNoteFrame
- **Severity:** medium
- **Failure scenario:** Tabelle voll, alle Fremdzeilen in derselben Minute beruehrt, Frame mit zwei
  unbekannten Rufzeichen: `age > oldest_age` ist strikt, jedes Token verdraengt Zeile 1, das
  fruehere Token desselben Frames wird ueberschrieben, Treffer landen auf der Diagonale.
- **Fix:** Schutzmaske der im aktuellen Aufruf vergebenen Zeilen; ohne Opfer -3 und Frame verwerfen.
- **Status:** Nacharbeit dispatcht, Test gefordert.

## Finding M2: 16-Bit-Geister nach 45 Tagen Laufzeit

- **File:** src/nbr_matrix.cpp nbrFresh / Verdraengung
- **Severity:** medium
- **Failure scenario:** Zelle zuletzt bei Minute 100 getroffen, nie wieder: ab 65636 wieder "frisch"
  mit alten Zaehlern; die Verdraengung haelt den Geist fuer die juengste Zeile.
- **Fix:** Sweep alle 1024 Minuten, alles mit Alter >= 32768 nullen.
- **Status:** Nacharbeit dispatcht, Test gefordert.

## Finding L1: stille Kappung im Kommando

- **File:** src/command_functions.cpp `--neighbours`
- **Severity:** low
- **Fix:** Puffer 300 Byte, " ..." bei snprintf-Rueckgabe >= Puffer. **Erledigt.**

## Finding L2: freier Platz nullt Zeile/Spalte nicht

- **File:** src/nbr_matrix.cpp nbrTouchRow
- **Severity:** low
- **Fix:** nbrZeroRowAndColumn auch im Frei-Platz-Zweig. Nacharbeit dispatcht.

## Finding L3: eigene Position wird nur einmal aus den Settings gesetzt

- **File:** src/lora_functions.cpp Haken
- **Severity:** low
- **Status:** akzeptiert, notiert; GPS-bewegte Knoten aktualisieren ihre Zeile 0 nicht. Offener Punkt.

## Finding L4: Testluecken

- **File:** test/test_nbr_matrix/test_nbr_matrix.cpp
- **Severity:** low
- **Fix:** Spaltenzelle beim Verdraengen pruefen, >8 Tokens, nbrReset, MESH-Ruecknahme, Hoerer-Gesamtzahl.
  Nacharbeit dispatcht.

## Refuted claims (do not re-investigate)

- Haken hinter Dedup: refutiert, `is_new_packet` setzt oben nur `rx_is_new`, kein Return davor ausser ACK/ungueltig/unkonfiguriert.
- `is_equ(msg_destination_path,"HG")` unzuverlaessig: refutiert, sendHey setzt exakt "H"/"HG", Relais aendern das Feld nicht, mheard_functions.cpp:655 nutzt denselben Test.
- OOB in nbrHearers/nbrExclusive: refutiert, Schreiben nur bei n < max, Aufrufer uebergeben NBR_MAX_ROWS-Arrays.
- Frisch-bei-Boot fuer genullte Zellen: refutiert, alle Leser gaten auf nbrCellSet / USED.
- printf-Formatfehler auf nRF52: refutiert, alle Argumente gecastet, `%.1f` wie auf den bestehenden Seiten.
- Stack im nRF52-LORA-Task: refutiert, ~230 B zusaetzlich neben struct aprsMessage.
