# Zeitzonen / Time zones

Deutsch zuerst, English below.

---

## Deutsch

### Zeitzonen-Regel setzen

Die Firmware nimmt eine POSIX-TZ-Regel entgegen. Damit schaltet der Knoten Sommer- und Winterzeit selbst um, ohne Zeitserver und ohne feste UTC-Verschiebung.

- Konsole: `--settz <regel>` setzt die Regel, `--settz none` loescht sie.
- Web-Setup: Feld "Zeitzone" (maximal 39 Zeichen).
- Gespeichert wird die Regel in `node_tz`. Auf ESP32 ist beim ersten Start `CET-1CEST,M3.5.0,M10.5.0/3` voreingestellt.
- `--utcoff` loescht die Regel wieder (feste UTC-Verschiebung).

Beispiel: `--settz CET-1CEST,M3.5.0,M10.5.0/3`

### Aufbau der Regel

`Name Offset [SommerName [Offset] ,Beginn,Ende]`

- Name: mindestens 3 Buchstaben, oder die Form `<+0530>`. Gespeichert werden maximal 7 Zeichen.
- Offset: `[+-]hh[:mm[:ss]]` mit POSIX-Vorzeichen, also **westlich positiv**: `CET-1` bedeutet UTC+1, `EST5` bedeutet UTC-5.
- Fehlt der Offset der Sommerzeit, gilt Normalzeit plus 1 Stunde.
- Regeln nur in der Form `Mm.w.d[/zeit]`: Monat 1-12, Woche 1-5 (5 = letzte), Wochentag 0-6 (0 = Sonntag). Zeit lokal, Standard 02:00, bis 167 Stunden, auch ueber 24 Stunden hinaus.
- Der Beginn gilt in Normalzeit, das Ende in Sommerzeit.
- Maximal 39 Zeichen.

### Abgelehnte Formen

| Eingabe                                      | Meldung der Firmware                              |
| -------------------------------------------- | ------------------------------------------------- |
| `J`- oder Tageszahl-Regeln (`J60`, `59`)     | `only M rules supported (Mm.w.d)`                 |
| laenger als 39 Zeichen                       | `too long (max 39 characters)`                    |
| alles andere (Syntaxfehler, Name ohne Regel) | `format (std offset dst,Mm.w.d/time,Mm.w.d/time)` |

### Tabelle

Die Strings entsprechen den POSIX-Zeilen der IANA-tzdata (Stand Oktober 2026, geprueft gegen die tzdata des Entwicklungsrechners). Die Spalte "UTC-Offset" zeigt Winter / Sommer. Jeder String wird vom Test `test/test_tz_rule` geprueft, damit Dokument und Firmware nicht auseinanderlaufen.

| Region        | Zone (IANA, for orientation)                                                                                               | TZ string                         | UTC offset winter / summer | DST rule                                                                                                  |
| ------------- | -------------------------------------------------------------------------------------------------------------------------- | --------------------------------- | -------------------------- | --------------------------------------------------------------------------------------------------------- |
| UTC           | UTC                                                                                                                        | `UTC0`                            | +0 / +0                    | none                                                                                                      |
| Europe        | Europe/London                                                                                                              | `GMT0BST,M3.5.0/1,M10.5.0`        | +0 / +1                    | last Sun Mar 01:00 UTC to last Sun Oct 01:00 UTC                                                          |
| Europe        | Europe/Dublin                                                                                                              | `GMT0IST,M3.5.0/1,M10.5.0`        | +0 / +1                    | as London (tzdata writes IST-1GMT0 with negative DST; use the listed form)                                |
| Europe        | Europe/Lisbon                                                                                                              | `WET0WEST,M3.5.0/1,M10.5.0`       | +0 / +1                    | as London                                                                                                 |
| Europe        | Europe/Vienna (also Berlin, Zurich, Rome, Warsaw, Prague, Budapest, Zagreb, Ljubljana, Amsterdam, Brussels, Paris, Madrid) | `CET-1CEST,M3.5.0,M10.5.0/3`      | +1 / +2                    | last Sun Mar 02:00 to last Sun Oct 03:00 local (01:00 UTC both)                                           |
| Europe        | Europe/Athens (also Helsinki, Bucharest, Sofia, Kyiv, Riga, Tallinn, Vilnius)                                              | `EET-2EEST,M3.5.0/3,M10.5.0/4`    | +2 / +3                    | last Sun Mar 03:00 to last Sun Oct 04:00 local (01:00 UTC both)                                           |
| Europe        | Europe/Moscow                                                                                                              | `MSK-3`                           | +3 / +3                    | none                                                                                                      |
| Europe        | Europe/Istanbul                                                                                                            | `<+03>-3`                         | +3 / +3                    | none                                                                                                      |
| Europe        | Atlantic/Reykjavik                                                                                                         | `GMT0`                            | +0 / +0                    | none                                                                                                      |
| North America | America/New_York (Eastern)                                                                                                 | `EST5EDT,M3.2.0,M11.1.0`          | -5 / -4                    | 2nd Sun Mar 02:00 to 1st Sun Nov 02:00 local                                                              |
| North America | America/Chicago (Central)                                                                                                  | `CST6CDT,M3.2.0,M11.1.0`          | -6 / -5                    | as New York                                                                                               |
| North America | America/Denver (Mountain)                                                                                                  | `MST7MDT,M3.2.0,M11.1.0`          | -7 / -6                    | as New York                                                                                               |
| North America | America/Phoenix (Arizona)                                                                                                  | `MST7`                            | -7 / -7                    | none                                                                                                      |
| North America | America/Los_Angeles (Pacific)                                                                                              | `PST8PDT,M3.2.0,M11.1.0`          | -8 / -7                    | as New York                                                                                               |
| North America | America/Anchorage (Alaska)                                                                                                 | `AKST9AKDT,M3.2.0,M11.1.0`        | -9 / -8                    | as New York                                                                                               |
| North America | Pacific/Honolulu (Hawaii)                                                                                                  | `HST10`                           | -10 / -10                  | none                                                                                                      |
| North America | America/Halifax (Atlantic)                                                                                                 | `AST4ADT,M3.2.0,M11.1.0`          | -4 / -3                    | as New York                                                                                               |
| North America | America/St_Johns (Newfoundland)                                                                                            | `NST3:30NDT,M3.2.0,M11.1.0`       | -3:30 / -2:30              | as New York                                                                                               |
| North America | America/Mexico_City                                                                                                        | `CST6`                            | -6 / -6                    | none (abolished 2022)                                                                                     |
| South America | America/Sao_Paulo (Brazil)                                                                                                 | `<-03>3`                          | -3 / -3                    | none (abolished 2019)                                                                                     |
| South America | America/Argentina/Buenos_Aires                                                                                             | `<-03>3`                          | -3 / -3                    | none                                                                                                      |
| South America | America/Bogota (Colombia)                                                                                                  | `<-05>5`                          | -5 / -5                    | none                                                                                                      |
| South America | America/Santiago (Chile)                                                                                                   | `<-04>4<-03>,M9.1.6/24,M4.1.6/24` | -4 / -3                    | 1st Sat Sep 24:00 (i.e. Sun 00:00) to 1st Sat Apr 24:00 local; southern hemisphere, DST over the New Year |
| Africa        | Africa/Johannesburg (South Africa)                                                                                         | `SAST-2`                          | +2 / +2                    | none                                                                                                      |
| Africa        | Africa/Lagos (Nigeria)                                                                                                     | `WAT-1`                           | +1 / +1                    | none                                                                                                      |
| Africa        | Africa/Cairo (Egypt)                                                                                                       | `EET-2EEST,M4.5.5/0,M10.5.4/24`   | +2 / +3                    | last Fri Apr 00:00 to last Thu Oct 24:00 local (DST reinstated 2023)                                      |
| Asia          | Asia/Kolkata (India)                                                                                                       | `<+0530>-5:30`                    | +5:30 / +5:30              | none                                                                                                      |
| Asia          | Asia/Kathmandu (Nepal)                                                                                                     | `<+0545>-5:45`                    | +5:45 / +5:45              | none                                                                                                      |
| Asia          | Asia/Dubai (UAE)                                                                                                           | `<+04>-4`                         | +4 / +4                    | none                                                                                                      |
| Asia          | Asia/Tehran (Iran)                                                                                                         | `<+0330>-3:30`                    | +3:30 / +3:30              | none (abolished 2022)                                                                                     |
| Asia          | Asia/Jerusalem (Israel)                                                                                                    | `IST-2IDT,M3.4.4/26,M10.5.0`      | +2 / +3                    | Friday before the last Sun Mar 02:00 (rule: 4th Thu Mar 26:00 local) to last Sun Oct 02:00                |
| Asia          | Asia/Shanghai (China)                                                                                                      | `CST-8`                           | +8 / +8                    | none                                                                                                      |
| Asia          | Asia/Hong_Kong                                                                                                             | `HKT-8`                           | +8 / +8                    | none                                                                                                      |
| Asia          | Asia/Singapore                                                                                                             | `<+08>-8`                         | +8 / +8                    | none                                                                                                      |
| Asia          | Asia/Tokyo (Japan)                                                                                                         | `JST-9`                           | +9 / +9                    | none                                                                                                      |
| Asia          | Asia/Seoul (Korea)                                                                                                         | `KST-9`                           | +9 / +9                    | none                                                                                                      |
| Australia     | Australia/Sydney (also Melbourne, Canberra, Hobart)                                                                        | `AEST-10AEDT,M10.1.0,M4.1.0/3`    | +10 / +11                  | 1st Sun Oct 02:00 to 1st Sun Apr 03:00 local; southern hemisphere                                         |
| Australia     | Australia/Brisbane (Queensland)                                                                                            | `AEST-10`                         | +10 / +10                  | none                                                                                                      |
| Australia     | Australia/Adelaide                                                                                                         | `ACST-9:30ACDT,M10.1.0,M4.1.0/3`  | +9:30 / +10:30             | as Sydney                                                                                                 |
| Australia     | Australia/Darwin                                                                                                           | `ACST-9:30`                       | +9:30 / +9:30              | none                                                                                                      |
| Australia     | Australia/Perth                                                                                                            | `AWST-8`                          | +8 / +8                    | none                                                                                                      |
| New Zealand   | Pacific/Auckland                                                                                                           | `NZST-12NZDT,M9.5.0,M4.1.0/3`     | +12 / +13                  | last Sun Sep 02:00 to 1st Sun Apr 03:00 local; southern hemisphere                                        |

Hinweise:

- Dublin: tzdata selbst schreibt `IST-1GMT0,M10.5.0,M3.5.0/1` (negative Sommerzeit). Die Firmware nimmt die gleichwertige, hier gelistete Form.
- Die Offsets der Spalte sind die uebliche Schreibweise (Ost positiv), im String steht das POSIX-Vorzeichen (West positiv).

### Regeln aendern sich politisch

Laender schaffen die Sommerzeit ab oder fuehren sie wieder ein (Brasilien 2019, Mexiko und Iran 2022, Aegypten 2023). Vor dem Eintragen die aktuelle Regel pruefen. Auf Linux und macOS gibt die letzte Zeile der Zonendatei den POSIX-String aus:

```
tail -1 /usr/share/zoneinfo/Europe/Vienna
CET-1CEST,M3.5.0,M10.5.0/3
```

Liefert die Datei eine Form mit `J`-Regeln, negativer Sommerzeit oder mehr als 39 Zeichen, muss sie von Hand umgeschrieben werden (Name kuerzen, `<+03>`-Form verwenden).

---

## English

### Setting a time zone rule

The firmware accepts a POSIX TZ rule. The node then switches between standard and daylight time by itself, with no time server and no fixed UTC offset.

- Console: `--settz <rule>` sets the rule, `--settz none` clears it.
- Web setup: field "time zone" (at most 39 characters).
- The rule is stored in `node_tz`. On ESP32 the first-boot default is `CET-1CEST,M3.5.0,M10.5.0/3`.
- `--utcoff` clears the rule again (fixed UTC offset).

Example: `--settz CET-1CEST,M3.5.0,M10.5.0/3`

### Rule grammar

`name offset [dstname [offset] ,start,end]`

- Name: at least 3 letters, or the form `<+0530>`. At most 7 characters are kept.
- Offset: `[+-]hh[:mm[:ss]]` with the POSIX sign, that is **west positive**: `CET-1` means UTC+1, `EST5` means UTC-5.
- If the DST offset is omitted, it is standard time plus 1 hour.
- Rules only in the form `Mm.w.d[/time]`: month 1-12, week 1-5 (5 = last), weekday 0-6 (0 = Sunday). Time is local, default 02:00, up to 167 hours, so values above 24 are allowed.
- The start is evaluated in standard time, the end in daylight time.
- At most 39 characters.

### Rejected forms

| Input                                                | Firmware message                                  |
| ---------------------------------------------------- | ------------------------------------------------- |
| `J` or day-number rules (`J60`, `59`)                | `only M rules supported (Mm.w.d)`                 |
| longer than 39 characters                            | `too long (max 39 characters)`                    |
| anything else (syntax error, DST name without rules) | `format (std offset dst,Mm.w.d/time,Mm.w.d/time)` |

### Table

The strings match the POSIX lines of the IANA tzdata (state October 2026, cross-checked against the tzdata of the development host). The "UTC offset" column shows winter / summer. Every string is checked by the test `test/test_tz_rule`, so document and firmware cannot drift apart.

The zone names are IANA names for orientation only, the firmware does not know them. The DST rule column is in English in both sections.

| Region        | Zone (IANA, for orientation)                                                                                               | TZ string                         | UTC offset winter / summer | DST rule                                                                                                  |
| ------------- | -------------------------------------------------------------------------------------------------------------------------- | --------------------------------- | -------------------------- | --------------------------------------------------------------------------------------------------------- |
| UTC           | UTC                                                                                                                        | `UTC0`                            | +0 / +0                    | none                                                                                                      |
| Europe        | Europe/London                                                                                                              | `GMT0BST,M3.5.0/1,M10.5.0`        | +0 / +1                    | last Sun Mar 01:00 UTC to last Sun Oct 01:00 UTC                                                          |
| Europe        | Europe/Dublin                                                                                                              | `GMT0IST,M3.5.0/1,M10.5.0`        | +0 / +1                    | as London (tzdata writes IST-1GMT0 with negative DST; use the listed form)                                |
| Europe        | Europe/Lisbon                                                                                                              | `WET0WEST,M3.5.0/1,M10.5.0`       | +0 / +1                    | as London                                                                                                 |
| Europe        | Europe/Vienna (also Berlin, Zurich, Rome, Warsaw, Prague, Budapest, Zagreb, Ljubljana, Amsterdam, Brussels, Paris, Madrid) | `CET-1CEST,M3.5.0,M10.5.0/3`      | +1 / +2                    | last Sun Mar 02:00 to last Sun Oct 03:00 local (01:00 UTC both)                                           |
| Europe        | Europe/Athens (also Helsinki, Bucharest, Sofia, Kyiv, Riga, Tallinn, Vilnius)                                              | `EET-2EEST,M3.5.0/3,M10.5.0/4`    | +2 / +3                    | last Sun Mar 03:00 to last Sun Oct 04:00 local (01:00 UTC both)                                           |
| Europe        | Europe/Moscow                                                                                                              | `MSK-3`                           | +3 / +3                    | none                                                                                                      |
| Europe        | Europe/Istanbul                                                                                                            | `<+03>-3`                         | +3 / +3                    | none                                                                                                      |
| Europe        | Atlantic/Reykjavik                                                                                                         | `GMT0`                            | +0 / +0                    | none                                                                                                      |
| North America | America/New_York (Eastern)                                                                                                 | `EST5EDT,M3.2.0,M11.1.0`          | -5 / -4                    | 2nd Sun Mar 02:00 to 1st Sun Nov 02:00 local                                                              |
| North America | America/Chicago (Central)                                                                                                  | `CST6CDT,M3.2.0,M11.1.0`          | -6 / -5                    | as New York                                                                                               |
| North America | America/Denver (Mountain)                                                                                                  | `MST7MDT,M3.2.0,M11.1.0`          | -7 / -6                    | as New York                                                                                               |
| North America | America/Phoenix (Arizona)                                                                                                  | `MST7`                            | -7 / -7                    | none                                                                                                      |
| North America | America/Los_Angeles (Pacific)                                                                                              | `PST8PDT,M3.2.0,M11.1.0`          | -8 / -7                    | as New York                                                                                               |
| North America | America/Anchorage (Alaska)                                                                                                 | `AKST9AKDT,M3.2.0,M11.1.0`        | -9 / -8                    | as New York                                                                                               |
| North America | Pacific/Honolulu (Hawaii)                                                                                                  | `HST10`                           | -10 / -10                  | none                                                                                                      |
| North America | America/Halifax (Atlantic)                                                                                                 | `AST4ADT,M3.2.0,M11.1.0`          | -4 / -3                    | as New York                                                                                               |
| North America | America/St_Johns (Newfoundland)                                                                                            | `NST3:30NDT,M3.2.0,M11.1.0`       | -3:30 / -2:30              | as New York                                                                                               |
| North America | America/Mexico_City                                                                                                        | `CST6`                            | -6 / -6                    | none (abolished 2022)                                                                                     |
| South America | America/Sao_Paulo (Brazil)                                                                                                 | `<-03>3`                          | -3 / -3                    | none (abolished 2019)                                                                                     |
| South America | America/Argentina/Buenos_Aires                                                                                             | `<-03>3`                          | -3 / -3                    | none                                                                                                      |
| South America | America/Bogota (Colombia)                                                                                                  | `<-05>5`                          | -5 / -5                    | none                                                                                                      |
| South America | America/Santiago (Chile)                                                                                                   | `<-04>4<-03>,M9.1.6/24,M4.1.6/24` | -4 / -3                    | 1st Sat Sep 24:00 (i.e. Sun 00:00) to 1st Sat Apr 24:00 local; southern hemisphere, DST over the New Year |
| Africa        | Africa/Johannesburg (South Africa)                                                                                         | `SAST-2`                          | +2 / +2                    | none                                                                                                      |
| Africa        | Africa/Lagos (Nigeria)                                                                                                     | `WAT-1`                           | +1 / +1                    | none                                                                                                      |
| Africa        | Africa/Cairo (Egypt)                                                                                                       | `EET-2EEST,M4.5.5/0,M10.5.4/24`   | +2 / +3                    | last Fri Apr 00:00 to last Thu Oct 24:00 local (DST reinstated 2023)                                      |
| Asia          | Asia/Kolkata (India)                                                                                                       | `<+0530>-5:30`                    | +5:30 / +5:30              | none                                                                                                      |
| Asia          | Asia/Kathmandu (Nepal)                                                                                                     | `<+0545>-5:45`                    | +5:45 / +5:45              | none                                                                                                      |
| Asia          | Asia/Dubai (UAE)                                                                                                           | `<+04>-4`                         | +4 / +4                    | none                                                                                                      |
| Asia          | Asia/Tehran (Iran)                                                                                                         | `<+0330>-3:30`                    | +3:30 / +3:30              | none (abolished 2022)                                                                                     |
| Asia          | Asia/Jerusalem (Israel)                                                                                                    | `IST-2IDT,M3.4.4/26,M10.5.0`      | +2 / +3                    | Friday before the last Sun Mar 02:00 (rule: 4th Thu Mar 26:00 local) to last Sun Oct 02:00                |
| Asia          | Asia/Shanghai (China)                                                                                                      | `CST-8`                           | +8 / +8                    | none                                                                                                      |
| Asia          | Asia/Hong_Kong                                                                                                             | `HKT-8`                           | +8 / +8                    | none                                                                                                      |
| Asia          | Asia/Singapore                                                                                                             | `<+08>-8`                         | +8 / +8                    | none                                                                                                      |
| Asia          | Asia/Tokyo (Japan)                                                                                                         | `JST-9`                           | +9 / +9                    | none                                                                                                      |
| Asia          | Asia/Seoul (Korea)                                                                                                         | `KST-9`                           | +9 / +9                    | none                                                                                                      |
| Australia     | Australia/Sydney (also Melbourne, Canberra, Hobart)                                                                        | `AEST-10AEDT,M10.1.0,M4.1.0/3`    | +10 / +11                  | 1st Sun Oct 02:00 to 1st Sun Apr 03:00 local; southern hemisphere                                         |
| Australia     | Australia/Brisbane (Queensland)                                                                                            | `AEST-10`                         | +10 / +10                  | none                                                                                                      |
| Australia     | Australia/Adelaide                                                                                                         | `ACST-9:30ACDT,M10.1.0,M4.1.0/3`  | +9:30 / +10:30             | as Sydney                                                                                                 |
| Australia     | Australia/Darwin                                                                                                           | `ACST-9:30`                       | +9:30 / +9:30              | none                                                                                                      |
| Australia     | Australia/Perth                                                                                                            | `AWST-8`                          | +8 / +8                    | none                                                                                                      |
| New Zealand   | Pacific/Auckland                                                                                                           | `NZST-12NZDT,M9.5.0,M4.1.0/3`     | +12 / +13                  | last Sun Sep 02:00 to 1st Sun Apr 03:00 local; southern hemisphere                                        |

Notes:

- Dublin: tzdata itself writes `IST-1GMT0,M10.5.0,M3.5.0/1` (negative DST). The firmware takes the equivalent form listed in the table.
- The offsets in the column use the usual notation (east positive); the string carries the POSIX sign (west positive).

### Rules change politically

Countries abolish or reintroduce DST (Brazil 2019, Mexico and Iran 2022, Egypt 2023). Check the current rule before entering it. On Linux and macOS the last line of the zone file prints the POSIX string:

```
tail -1 /usr/share/zoneinfo/Europe/Vienna
CET-1CEST,M3.5.0,M10.5.0/3
```

If the file gives a form with `J` rules, negative DST or more than 39 characters, rewrite it by hand (shorten the name, use the `<+03>` form).
