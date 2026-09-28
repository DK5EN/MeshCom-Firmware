# Fehlerbericht: Ping geht nie über HF (T-Beam 1W, MeshCom-Fork v4.35s)

## Kurzfassung

`sendPing()` steigt in der allerersten Zeile aus, wenn der TRACK-Modus aktiv ist
(`bDisplayTrack == true`). Der Aufrufer in der ESP32-Hauptschleife druckt
`[PING]...send Ping to <call> <n>` und zählt `node_pingcount` herunter — **vor
und unabhängig von** diesem vorzeitigen Return. Der Betreiber sieht also einen
"gesendeten" Ping, sieht das Ping-Budget schrumpfen, und trotzdem landet nie
etwas im TX-Ring: kein Frame, keine Fehlermeldung, kein Marker.

Nicht boardspezifisch. Der T-Beam 1W ist schlicht ein Tracker-Board — dort wird
TRACK realistischerweise eingeschaltet.

**Sofort-Workaround am Knoten:** `--track off` (Kontrolle mit `--info`, Zeile
`Track:`, oder im Bootlog die Zeile `[INIT]...Track on - ...`).

## Beweisführung aus dem eingeschickten Log

```
[MC-DBG] RX_TIMEOUT_FIRE ts=171012560 wait=4535 delta=4536
[MC-SM] IDLE -> RX_LISTEN rc=0
[MC-DBG] RX_RESTART src=timeout state=0
17:02:36 [LoRa]...Receive Timeout, startReceive again with sucess
[PING]...send Ping to DB0SD-22 <1>
[MC-DBG] RX_TIMEOUT_FIRE ts=171017106 wait=4535 delta=4537
[MC-SM] IDLE -> RX_LISTEN rc=0
[MC-DBG] RX_RESTART src=timeout state=0
17:02:41 [LoRa]...Receive Timeout, startReceive again with sucess
[MC-DBG] CHANNEL_UTIL rx=0ms tx=0ms util=0%
```

Die Kette, die den Fehler auf das Einreihen festnagelt und nicht auf das Funkteil:

1. `bLORADEBUG` ist an — jede `[MC-DBG]`-Zeile belegt das.
2. Der CSMA-Timeout bei `ts=171017106` setzt `iReceiveTimeOutTime = 0`
   (`src/esp32/esp32_main.cpp:2417`), das TX-Gate bei
   `src/esp32/esp32_main.cpp:2600` läuft also im selben Schleifendurchlauf.
3. Dieses Gate druckt `[MC-DBG] TX_GATE_ENTER ...`, sobald `iWrite != iRead`
   (`src/esp32/esp32_main.cpp:2607`). Die Zeile fehlt. Also war der Ring 4,5 s
   nach der `[PING]`-Zeile leer.
4. Die einzigen anderen Wege, auf denen der Frame hätte verschwinden können,
   protokollieren beide — und keine der Zeilen steht im Log:
   `[MC-DBG] RING_REJECT ...` bei unzulässiger Länge
   (`src/txring_functions.cpp:474`) und `[TX];refuse;unconfigured;...` bei
   Werks-Rufzeichen (`src/txring_functions.cpp:78`, rohes `Serial.printf`, nicht
   abschaltbar).

Damit bleibt zwischen der `[PING]`-Ausgabe und `addTxRingEntry()` genau ein Pfad
übrig: die TRACK-Sperre.

## Ursache

`src/loop_functions.cpp:3310`

```cpp
void sendPing(char msg_call[10])
{
    // no ping within track mode
    if(bDisplayTrack)
        return;
```

`src/esp32/esp32_main.cpp:4042`

```cpp
if(meshcom_settings.node_pingtime > 29 && meshcom_settings.node_pingcall[0] != 0x00 && meshcom_settings.node_pingcount > 0)
{
    if((int32_t)(millis() - (resendPing + meshcom_settings.node_pingtime * 1000)) > 0)
    {
        ...
        printfdeb("[PING]...send Ping to %s <%i>\n", ...);   // wird bedingungslos gedruckt
        sendPing(meshcom_settings.node_pingcall);            // hat keinen Rückgabewert
        meshcom_settings.node_pingcount--;                   // Budget wird trotzdem verbraucht
    }
}
```

Die Unterdrückung selbst ist gewollt und passt zum übrigen TRACK-Verhalten:
`sendMessage()` hält MeshCom-Text im Trackbetrieb ebenfalls vom Mesh fern
(`if(!bDisplayTrack) addTxRingEntry(...)`, `src/loop_functions.cpp:5348`) und
sendet stattdessen LoRa-APRS-Text; `SendPong()` trägt dieselbe Sperre
(`src/loop_functions.cpp:3390`). Der Defekt ist, dass die Entscheidung unsichtbar
bleibt:

- Rückgabetyp `void`, kein Aufrufer kann reagieren;
- die `[PING]`-Zeile steht vor der Sperre, das Log lügt also;
- `bPingSend` wird nie gesetzt, damit kommt auch kein `[PONG]...fail` hinterher —
  der Knoten schweigt vollständig;
- `node_pingcount` zählt bis 0 herunter, nach `--pingmax` Versuchen ist die
  Funktion dauerhaft still und sieht wie ein Einstellungsproblem aus.

## Zweiter Befund (dieselbe Funktion, unabhängig von TRACK)

`sendPing()` wirft außerdem den Rückgabewert von `addTxRingEntry()` weg
(`src/loop_functions.cpp:3359`). Bei vollem Ring verschwindet der Ping genauso
lautlos, und der Code darunter malt trotzdem `PING nnn / SENT` aufs Display und
setzt `bPingSend = true`. Ein nie eingereihter Ping erzeugt dann ein Intervall
später ein `[PONG]...fail`, was sich liest wie "die Gegenstelle antwortet nicht"
statt "wir haben nie gesendet". Beides behebt derselbe Patch.

## Behebung

Minimal, drei Dateien, keine Verhaltensänderung außerhalb des Ping-Pfads.

### 1. `src/loop_functions.h:68`

```diff
-void sendPing(char msg_call[10]);
+bool sendPing(char msg_call[10]);
```

### 2. `src/loop_functions.cpp:3310`

```diff
-void sendPing(char msg_call[10])
+bool sendPing(char msg_call[10])
 {
-    // no ping within track mode
+    // Kein Ping im Track-Modus -- gewollt und konsistent zu sendMessage(),
+    // das MeshCom-Text im Trackbetrieb ebenfalls vom Mesh fernhaelt (siehe
+    // "if(!bDisplayTrack) addTxRingEntry(...)" weiter unten), weil die eigene
+    // Aussendung dann auf LoRa-APRS-Parametern laeuft. Bisher war das ein
+    // stummes "return": der Aufrufer hatte "[PING]...send Ping" bereits
+    // gedruckt und einen node_pingcount verbraucht, auf der Luft passierte nie
+    // etwas.
     if(bDisplayTrack)
-        return;
+    {
+        printfdeb("[PING]...suppressed: TRACK mode active (--track off to ping)\n");
+        return false;
+    }
```

```diff
     // Master RingBuffer for transmission
     // local messages send to LoRa TX
-    addTxRingEntry(msg_buffer, (uint16_t)aprsmsg.msg_len, 0xFF, "phone_msg"); // 0xFF no retransmission
+    // Gleiche Begruendung wie BP-09 in sendMessage(): ein abgelehntes Einreihen
+    // darf dem Betreiber nicht als gesendeter Ping gemeldet werden und darf
+    // bPingSend nicht scharfschalten -- sonst druckt das naechste Intervall
+    // "[PONG]...fail", was wie "Gegenstelle antwortet nicht" klingt statt wie
+    // "wir haben nie gesendet".
+    if(addTxRingEntry(msg_buffer, (uint16_t)aprsmsg.msg_len, 0xFF, "phone_msg") < 0) // 0xFF no retransmission
+    {
+        printfdeb("[PING]...not queued: TX ring refused the frame\n");
+        return false;
+    }
```

```diff
     meshcom_settings.node_pingduration = millis();
     bPingSend=true;
+
+    return true;
 }
```

### 3. `src/esp32/esp32_main.cpp:4054`

```diff
-            printfdeb("[PING]...send Ping to %s <%i>\n", meshcom_settings.node_pingcall, meshcom_settings.node_pingcount);
-
-            sendPing(meshcom_settings.node_pingcall);
+            bool ping_queued = sendPing(meshcom_settings.node_pingcall);
+
+            printfdeb("[PING]...%s Ping to %s <%i>\n", ping_queued ? "send" : "FAILED",
+                      meshcom_settings.node_pingcall, meshcom_settings.node_pingcount);

             meshcom_settings.node_pingcount--;
```

`node_pingcount` zählt auch bei einem fehlgeschlagenen Versuch weiter herunter,
und zwar mit Absicht: der Betreiber hat N Versuche angefordert, und jeder einzelne
nennt jetzt seinen Grund, statt stumm zu bleiben.

### 4. `src/nrf52/nrf52_main.cpp:2575` (derselbe Defekt, dieselbe Form)

```diff
-            if(meshcom_settings.node_pingcall[0] != 0x00)
-                sendPing(meshcom_settings.node_pingcall);
+            if(meshcom_settings.node_pingcall[0] != 0x00)
+            {
+                if(!sendPing(meshcom_settings.node_pingcall))
+                    printfdeb("[PING]...FAILED Ping to %s\n", meshcom_settings.node_pingcall);
+            }
```

## Außerhalb des Umfangs, fürs Backlog notiert

- Die nRF52-Ping-Schleife (`src/nrf52/nrf52_main.cpp:2565`) ist von der
  ESP32-Variante abgedriftet: kein `node_pingcount`-Budget, kein `PongFail()` bei
  ausbleibendem Pong, und die `[PING]`-Zeile hängt hinter `bDisplayInfo`. Die
  beiden zusammenzuführen ist eine eigene Änderung.
- `src/command_functions.cpp:3699` beschreibt `node_pingcall` mit
  `sizeof(meshcom_settings.node_call)`. Heute harmlos — beide Arrays sind
  `char[10]` (`src/esp32/esp32_flash.h:187`, `src/nrf52/WisBlock-API.h:348`) —
  aber es ist das falsche `sizeof` und bricht, sobald eines der Arrays anders
  dimensioniert wird.

## Nachweis

- Regressionstest: `test/test_txring` deckt die Einreih-Seite ab; die Sperre
  selbst braucht einen Knotentest — `--track on`, `--ping <call>`,
  `--ping start`, und prüfen, dass `[PING]...suppressed: TRACK mode active`
  erscheint statt eines stummen `[PING]...send Ping`, und dass `TX_GATE_ENTER`
  aus einem dokumentierten Grund ausbleibt.
- Positivpfad: `--track off`, Wiederholung, erwartet werden
  `[PING]...send Ping`, `[MC-DBG] TX_GATE_ENTER`, `[MC-DBG] RADIO_TX len=...`
  und ein `tx=` ungleich 0 in der nächsten `CHANNEL_UTIL`-Zeile.
