// configuration.h fuer den nativen Testbuild.
//
// Auf der Hardware liefert jede Variante ihre eigene configuration.h
// (variants/<board>/configuration.h). Nativ gibt es keine Variante, deshalb
// diese Datei.
//
// WICHTIG — explizites Board-Profil:
// configuration_global.h waehlt die Ringgroessen ueber eine #if-Leiter aus.
// Ohne jede Board-Definition faellt ein nativer Build in den #else-Zweig und
// bekommt stillschweigend MAX_MHEARD 30 / MAX_MHPATH 40 / MAX_RING 30 /
// MAX_DEDUP_RING 70 — also andere Dimensionen als jedes Board, gegen das wir
// testen. Ein Test, der so laeuft, prueft eine Konfiguration, die es auf
// keiner Hardware gibt.
//
// Deshalb wird hier CONFIG_IDF_TARGET_ESP32S3 gesetzt: das ist das Profil der
// Bench-Boards (Heltec WiFi LoRa 32 V3) und zugleich das des RAK4631 —
// MAX_MHEARD 80 / MAX_MHPATH 100 / MAX_RING 20 / MAX_DEDUP_RING 100.
//
// Wird spaeter gegen ein anderes Profil getestet, gehoert das in ein eigenes
// Test-Environment mit eigenem Define, nicht in eine Aenderung hier.
#pragma once

#ifndef NATIVE_BUILD
#error "test/support/configuration.h darf nur im nativen Testbuild verwendet werden"
#endif

#include <Arduino.h>

#ifndef CONFIG_IDF_TARGET_ESP32S3
#define CONFIG_IDF_TARGET_ESP32S3 1
#endif

#include <configuration_global.h>
