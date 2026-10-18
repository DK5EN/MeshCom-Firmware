# RM1: HMAC-Signatur berechnen und prüfen

## Deutsch

RM1 ist die authentisierte Fernwartung eines MeshCom-Knotens per Direktnachricht (DM). Diese
Seite beschreibt, wie ein eigener Client (App, Skript, Gateway) Befehle signiert und die Antworten
des Knotens prüft. Die Befehlsliste steht in [rm1-commands.md](rm1-commands.md). Referenzimplementierung
in Python: `tools/remote_cmd.py`, gemeinsame Testvektoren: `tools/tests/remote_cmd_vectors.json`.

### Überblick

```
Client (Absender, z. B. DK5EN-1)                    Knoten (Ziel, z. B. DK5EN-90)
  |  DM an DK5EN-90:                                   |
  |  "RM1 <ctr> <cmd>[ <args>] <tag>"  ---- LoRa --->  |  prüft Format, Zähler, Tag
  |                                                    |  führt den Befehl aus
  |  <--- LoRa ----  "RM1 <ctr> ok|err <result> <rtag>" |  Antwort als DM an den Absender
  |  prüft rtag
```

Beide Richtungen tragen ein 64-Bit-Tag (HMAC-SHA256, auf 8 Byte gekürzt). Der Text ist im Klartext
lesbar, nur die Echtheit wird gesichert.

### Schritt für Schritt

1. **Schlüssel.** `K = SHA-256(passwort)` (32 Byte). Das Passwort ist das `node_passwd` des Knotens,
   höchstens 14 Zeichen; Leerzeichen am Ende werden vor dem Hashen entfernt. Ein leeres Passwort
   schaltet RM1 am Knoten ab.
2. **Zähler** `ctr`: Dezimalzahl 1..4294967295, ohne führende Nullen, bei jedem Befehl größer als
   alle bisher akzeptierten. `0` ist nur für `sync` erlaubt.
3. **Kanonischer String** (genau so, mit senkrechten Strichen, ohne Zeilenende):
   `RM1|<dst>|<src>|<ctr>|<cmd>` bzw. `RM1|<dst>|<src>|<ctr>|<cmd> <args>`.
   - `dst`: Rufzeichen-SSID des Zielknotens, genau wie dort als Rufzeichen konfiguriert (Großbuchstaben,
     z. B. `DK5EN-90`).
   - `src`: Rufzeichen-SSID des Absenders, genau wie es der Knoten im Absenderfeld der DM empfängt
     (Großbuchstaben, z. B. `DK5EN-1`). Der Knoten nimmt beide Rufzeichen unverändert, es gibt keine
     Normalisierung auf seiner Seite: ein Client muss vor dem Signieren selbst auf Großbuchstaben bringen.
   - `cmd` und `args` sind genau der Text, der später in der DM steht (ein Leerzeichen dazwischen, keine
     doppelten Leerzeichen, kein Leerzeichen am Anfang oder Ende; `cmd` klein geschrieben).
4. **Tag** = `HMAC-SHA256(K, kanonischer_String)`, die ersten 8 Byte, als 16 kleingeschriebene
   Hex-Zeichen.
5. **DM-Text:** `RM1 <ctr> <cmd>[ <args>] <tag>`. Die DM geht an das Rufzeichen des Zielknotens. Der Text
   trägt kein `{NNN`-Suffix (RM1-Frames werden nicht per Nachrichtennummer bestätigt).
6. **Antwort prüfen.** Der Knoten antwortet mit `RM1 <ctr> ok|err <result> <rtag>`. Der Antwort-Tag
   wird über `RM1R|<dst>|<src>|<ctr>|<result>` berechnet, mit demselben Schlüssel. Wichtig:
   `dst` ist hier der **Knoten** (der Antwortende), `src` der **ursprüngliche Absender**, also dieselbe
   Reihenfolge wie im Befehl, nicht vertauscht. `result` ist alles zwischen `<ctr>` und `<rtag>`
   einschließlich `ok`/`err`, z. B. `ok rebooting` (also `RM1R|DK5EN-90|DK5EN-1|1|ok rebooting`).
   Zähler der Antwort muss dem gesendeten entsprechen. Tags in konstanter Zeit vergleichen.

Antworten ohne gültigen Tag verwerfen. Bei Fehlern, die der Knoten nicht beantworten darf (falsches
Tag, Zähler zu klein, falsches Format, Sperre), schweigt er; ein Ausbleiben der Antwort bedeutet also
nicht automatisch Funkverlust.

### Grenzen und Zeichen

- `args` höchstens 39 Zeichen, `result` höchstens 108 Zeichen (die ganze Antwort höchstens 140 Zeichen).
- `cmd` klein geschrieben; `args` klein, außer bei Befehlen mit freiem Text oder Rufzeichen.
- Nicht erlaubt sind `--`, `;`, `{`, `%` und nicht druckbare oder Nicht-ASCII-Zeichen in Befehlen.
  Der Knoten ersetzt in Antworten alles außer Buchstaben, Ziffern, Leerzeichen und `- . / = + _ @ ? ( ) , * #`
  durch `?`; der signierte Text ist der bereits bereinigte Text, genau wie er gesendet wird.
- Es gibt keine Wiederholungsleiter für die Antwort: sie geht genau einmal in die Luft. Ein Befehl
  trägt kein `{NNN`-Suffix und wird nicht per ACK bestätigt; die Bestätigung ist die signierte Antwort.

### Zähler im Client

- Zuerst `sync` senden (`ctr` 0). Die Antwort `ok ctr=<hwm> v=<version>` nennt den höchsten
  akzeptierten Zähler (`hwm`) des Knotens; `sync` verändert ihn nicht. Danach `hwm + 1` oder höher
  verwenden. Befehle mit `ctr <= hwm` werden als Wiederholung verworfen (stumm).
- Eigenen Zähler dauerhaft speichern, und zwar **vor** dem Senden. Nie einen Zähler für einen anderen
  Befehl wiederverwenden.
- Der Knoten speichert den neuen Höchstwert vor der Ausführung. Geht die Antwort verloren, denselben
  Befehl mit **demselben** `ctr` und demselben Tag noch einmal senden: innerhalb von 10 Minuten
  liefert der Knoten die gespeicherte Antwort erneut, ohne den Befehl noch einmal auszuführen.
  Danach (oder bei geändertem Befehl) einen neuen Zähler verwenden. Zuvor besser `sync`, um den Stand
  zu klären.
- Raten- und Sperrgrenzen am Knoten (ein Befehl je 10 s, `sync` je 60 s, 3 Fehlversuche in 90 s sperren
  den Absender 5 Minuten) gelten nur im Strict-Modus (`node_rmstrict`, Standard: aus). Auch ohne Strict
  beantwortet der Knoten `sync` und erneute Cache-Antworten höchstens einmal je 10 s. Eine Pause von mindestens
  10 s zwischen Befehlen ist daher ratsam.

### Sicherheitshinweise

- Das Passwort ist das Geheimnis. Wer es kennt (oder einen Frame mitliest und es per Wörterbuch
  errät), kann Befehle signieren. Das Tag erlaubt Offline-Raten; ein langes, zufälliges Passwort (bis 14
  Zeichen) wählen und nirgends im Klartext ablegen.
- Das Tag hat nur 64 Bit. Es schützt gegen Fälschen im Funk, ist aber kein Ersatz für ein starkes Passwort.
- Replay-Schutz: der streng steigende Zähler. Ein mitgeschnittener Frame ist wertlos, sobald ein
  Zähler größer oder gleich akzeptiert wurde. Der Zähler des Knotens überlebt Neustarts.
- Keine Vertraulichkeit: Befehle und Antworten sind auf Funk im Klartext lesbar (Amateurfunk-konform).
  Keine Geheimnisse in `args` oder `result` übertragen.
- Befehle sind an Ziel und Absender gebunden (`dst`, `src` im Tag); eine Antwort ist an Befehlszähler und
  Absender gebunden und kann nicht als Antwort auf einen anderen Befehl durchgehen.

### Rechenbeispiel (aus den Testvektoren)

Passwort `secret`, Ziel `DK5EN-90`, Absender `DK5EN-1`, `ctr` 1, Befehl `reboot` ohne Argumente:

- Schlüssel (Hex): `2bb80d537b1da3e38bd30361aa855686bde0eacd7162fef6a25fe97bf527a25b`
- Kanonischer String: `RM1|DK5EN-90|DK5EN-1|1|reboot`
- Tag: `18f287b79c551022`
- DM-Text: `RM1 1 reboot 18f287b79c551022`
- Antwort-Kanonisch: `RM1R|DK5EN-90|DK5EN-1|1|ok rebooting`
- Antwort-Tag: `facf04f881d89cde`
- Antwort-DM: `RM1 1 ok rebooting facf04f881d89cde`

Weitere Vektoren (mit Argumenten, `sync`, maximaler Zähler): `tools/tests/remote_cmd_vectors.json`.

## English

RM1 is the authenticated remote management of a MeshCom node over direct messages (DM). This
section shows how your own client (app, script, gateway) signs commands and verifies the node's replies.
The command list is in [rm1-commands.md](rm1-commands.md). Python reference implementation:
`tools/remote_cmd.py`; shared test vectors: `tools/tests/remote_cmd_vectors.json`.

### Overview

```
Client (sender, e.g. DK5EN-1)                       Node (target, e.g. DK5EN-90)
  |  DM to DK5EN-90:                                   |
  |  "RM1 <ctr> <cmd>[ <args>] <tag>"  ---- LoRa --->  |  checks format, counter, tag
  |                                                    |  executes the command
  |  <--- LoRa ----  "RM1 <ctr> ok|err <result> <rtag>" |  reply DM to the sender
  |  verifies rtag
```

Both directions carry a 64-bit tag (HMAC-SHA256 truncated to 8 bytes). The text stays readable on air;
only authenticity is protected.

### Step by step

1. **Key.** `K = SHA-256(password)` (32 bytes). The password is the node's `node_passwd`, at most 14
   characters; trailing spaces are stripped before hashing. An empty password disables RM1 on the node.
2. **Counter** `ctr`: decimal 1..4294967295, no leading zeros, greater than every counter the node has
   accepted so far. `0` is allowed for `sync` only.
3. **Canonical string** (exactly this, with vertical bars, no line ending):
   `RM1|<dst>|<src>|<ctr>|<cmd>` or `RM1|<dst>|<src>|<ctr>|<cmd> <args>`.
   - `dst`: callsign-SSID of the target node, exactly as configured there (upper case, e.g. `DK5EN-90`).
   - `src`: callsign-SSID of the sender, exactly as the node receives it in the DM's source field
     (upper case, e.g. `DK5EN-1`). The node uses both calls verbatim and does no normalisation, so your
     client must upper-case them before signing.
   - `cmd` and `args` are exactly the text that goes into the DM (one space between, no double spaces, no
     leading or trailing space; `cmd` in lower case).
4. **Tag** = `HMAC-SHA256(K, canonical string)`, first 8 bytes, as 16 lower-case hex characters.
5. **DM text:** `RM1 <ctr> <cmd>[ <args>] <tag>`, sent as a DM to the target node's callsign. The text has no
   `{NNN` message-number suffix (RM1 frames are not acknowledged by message number).
6. **Verify the reply.** The node answers `RM1 <ctr> ok|err <result> <rtag>`. The reply tag is computed
   over `RM1R|<dst>|<src>|<ctr>|<result>` with the same key. Note: `dst` is still the **node** (the
   replier) and `src` the **original sender**, the same order as in the command, not swapped. `result` is
   everything between `<ctr>` and `<rtag>` including `ok`/`err`, e.g. `ok rebooting` (so
   `RM1R|DK5EN-90|DK5EN-1|1|ok rebooting`). The reply's counter must equal the one you sent. Compare tags
   in constant time.

Discard replies with an invalid tag. For frames the node must not answer (wrong tag, counter too low,
bad format, lockout) it stays silent, so no reply does not necessarily mean radio loss.

### Limits and characters

- `args` at most 39 characters, `result` at most 108 characters (whole reply at most 140 characters).
- `cmd` lower case; `args` lower case, except for commands with free text or a callsign.
- Not allowed in commands: `--`, `;`, `{`, `%` and non-printable or non-ASCII characters.
  The node replaces everything except letters, digits, space and `- . / = + _ @ ? ( ) , * #` in a
  reply with `?`; the signed text is the already-sanitised text, exactly as sent.
- A reply goes on air exactly once, with no retry ladder. A command carries no `{NNN` suffix and is not
  ACKed; the signed reply is the confirmation.

### Counter handling in the client

- Send `sync` first (`ctr` 0). The reply `ok ctr=<hwm> v=<version>` names the node's highest accepted
  counter (`hwm`); `sync` never moves it. Then use `hwm + 1` or higher. A command with `ctr <= hwm` is
  dropped silently as a replay.
- Persist your own counter, and do so **before** sending. Never reuse a counter for a different command.
- The node stores the new high-water mark before executing. If the reply is lost, resend the same
  command with the **same** `ctr` and tag: within 10 minutes the node re-sends the stored reply
  without executing again. After that (or for a changed command) use a new counter; better run `sync`
  first to learn the state.
- Rate and lockout limits on the node (one command per 10 s, `sync` per 60 s, 3 rejects in 90 s lock the
  sender for 5 minutes) apply only in strict mode (`node_rmstrict`, default: off). Even without strict
  mode the node answers `sync` and repeated cached replies at most once per 10 s. Leave at least 10 s
  between commands.

### Security notes

- The password is the secret. Anyone who knows it (or overhears a frame and guesses it offline) can sign
  commands. The tag allows offline guessing, so pick a long random password (up to 14 characters) and
  never store it in clear text.
- The tag is only 64 bits. It protects against forgery on air; it does not replace a strong password.
- Replay protection is the strictly increasing counter. A recorded frame is worthless once a counter
  greater than or equal to it was accepted. The node's counter survives reboots.
- No confidentiality: commands and replies are readable on air (amateur radio compliant). Never put
  secrets into `args` or `result`.
- Commands are bound to target and sender (`dst`, `src` in the tag); a reply is bound to the command's
  counter and sender and cannot pass as the reply to another command.

### Worked example (from the test vectors)

Password `secret`, target `DK5EN-90`, sender `DK5EN-1`, `ctr` 1, command `reboot` without arguments:

- Key (hex): `2bb80d537b1da3e38bd30361aa855686bde0eacd7162fef6a25fe97bf527a25b`
- Canonical string: `RM1|DK5EN-90|DK5EN-1|1|reboot`
- Tag: `18f287b79c551022`
- DM text: `RM1 1 reboot 18f287b79c551022`
- Reply canonical: `RM1R|DK5EN-90|DK5EN-1|1|ok rebooting`
- Reply tag: `facf04f881d89cde`
- Reply DM: `RM1 1 ok rebooting facf04f881d89cde`

More vectors (with arguments, `sync`, maximum counter): `tools/tests/remote_cmd_vectors.json`.

## Code / Code

### Python (stdlib only; checked against the vector above)

```python
import hashlib
import hmac


def derive_key(passwd: str) -> bytes:
    return hashlib.sha256(passwd.rstrip(" ").encode("utf-8")).digest()


def tag(key: bytes, canonical: str) -> str:
    return hmac.new(key, canonical.encode("utf-8"), hashlib.sha256).hexdigest()[:16]


def command_canonical(dst, src, ctr, cmd, args=""):
    return "RM1|%s|%s|%d|%s%s" % (dst, src, ctr, cmd, " " + args if args else "")


def reply_canonical(dst, src, ctr, result):
    # dst = the managed node (the replier), src = the original sender
    return "RM1R|%s|%s|%d|%s" % (dst, src, ctr, result)


def build_command(key, dst, src, ctr, cmd, args=""):
    t = tag(key, command_canonical(dst, src, ctr, cmd, args))
    return "RM1 %d %s%s %s" % (ctr, cmd, " " + args if args else "", t)


def verify_reply(key, node, me, ctr, text):
    p = text.split(" ")
    if len(p) < 4 or p[0] != "RM1" or p[1] != str(ctr) or p[2] not in ("ok", "err"):
        return False
    result = " ".join(p[2:-1])
    want = tag(key, reply_canonical(node, me, ctr, result))
    return hmac.compare_digest(want, p[-1])

# check against the worked example
if __name__ == "__main__":
    k = derive_key("secret")
    assert build_command(k, "DK5EN-90", "DK5EN-1", 1, "reboot") == "RM1 1 reboot 18f287b79c551022"
    assert verify_reply(k, "DK5EN-90", "DK5EN-1", 1, "RM1 1 ok rebooting facf04f881d89cde")
```

### C / C++ (sketch)

```c
// SKETCH, not compiled here. mbedTLS 2.x/3.x; any HMAC-SHA256 API works the same way.
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>
#include <string.h>
#include <stdio.h>

// K = SHA-256(password without trailing spaces)
static void rm_key(const char *passwd, uint8_t key[32]) {
    size_t n = strlen(passwd);
    while (n > 0 && passwd[n - 1] == ' ') n--;
    mbedtls_sha256((const unsigned char *)passwd, n, key, 0);   // 0 = SHA-256, not SHA-224
}

// tag_hex: 17 bytes. text = canonical string (no terminating NUL is hashed).
static void rm_tag(const uint8_t key[32], const char *text, char tag_hex[17]) {
    uint8_t mac[32];
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), key, 32,
                    (const unsigned char *)text, strlen(text), mac);
    for (int i = 0; i < 8; i++) sprintf(tag_hex + 2 * i, "%02x", mac[i]);   // lower case
}

// Compare tags in constant time (do not use strcmp/memcmp).
static int rm_tag_equal(const char *a, const char *b) {
    unsigned char d = 0;
    for (int i = 0; i < 16; i++) d |= (unsigned char)(a[i] ^ b[i]);
    return d == 0;
}
```
