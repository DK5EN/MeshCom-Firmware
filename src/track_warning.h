#pragma once

// TRK-01 (BACKLOG) — Warnhinweis, wenn Track (SmartBeaconing) aktiv ist.
//
// Track drueckt die Beacon-Kadenz einer einzelnen Station auf bis zu 10 s
// herunter, gegen einen Kanal, der im Mittel etwa ein Paket alle 8 s traegt.
// Eine Handvoll Track-Nutzer reicht damit aus, eine Zelle fuer alle anderen
// zuzufahren — genau das Bild, das aus dem Feld als "Paketverlust" gemeldet
// wird. Bisher war der Schalter an keiner Stelle mit dieser Kosteninformation
// versehen: die WebGUI beschrieb ihn als "enable display of SmartBeaconing",
// --track on quittierte kommentarlos, und ein Knoten, der mit gesetztem Flag
// bootet, schwieg dazu dauerhaft.
//
// Der Wortlaut ist vom Operator fixiert und wird nicht abgeschwaecht.
//
// Zwei Varianten, weil der Kontext unterschiedlich viel verraet:
//  - Auf Serial/Netconsole steht die Zeile allein im Log; ohne das Praefix
//    "Track on - " ist nicht erkennbar, welche Einstellung sie ausgeloest hat.
//  - In der WebGUI steht der Hinweis direkt neben dem Track-Switch, dort ist
//    die Herkunft offensichtlich und das Praefix nur Rauschen.
#define TRACK_WARNING_TEXT   "degraded MeshCom RX performance, packetloss likely"
#define TRACK_WARNING_SERIAL "Track on - " TRACK_WARNING_TEXT
