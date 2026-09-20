// Link-Stubs, gemeinsam fuer alle drei env:native_parsers-Suiten (PT-01,
// BACKLOG SS3.8j): test_decodeaprspos, test_decodemheard, test_checkvia.
//
// build_src_filter dieser Env ist Env-weit, nicht pro Test-Case: jede der
// drei Suiten linkt Regexp.cpp, regex_functions.cpp, aprs_functions.cpp,
// mheard_functions.cpp UND via_functions.cpp in ein eigenes Programm (siehe
// platformio.ini [env:native_parsers]). Der Linker verlangt darum fuer jedes
// der drei Programme dieselbe vollstaendige Menge an Stub-Definitionen,
// unabhaengig davon, welchen der drei Parser die jeweilige Suite eigentlich
// prueft -- exakt das Muster, das test_aprs_decode.cpp und test_hey_report.cpp
// in env:native_aprs schon vormachen (dort dupliziert, weil beide Suiten nur
// aprs_functions.cpp mitschleppen; hier ausgelagert, weil drei Suiten fuenf
// Uebersetzungseinheiten teilen).
//
// Bewusst KEIN "inline" auf den Funktionen: jede Suite bindet diese Datei
// genau einmal in ihre eigene main()-Uebersetzungseinheit ein (kein
// Mehrfachinklusions-Risiko), und eine ungerufene "inline"-Funktion muss der
// Compiler nicht in die Objektdatei emittieren -- der Linker braeuchte die
// Definition trotzdem, weil mheard_functions.o/via_functions.o sie von
// AUSSEN referenzieren. Gewoehnliche Funktionsdefinitionen mit externer
// Bindung werden dagegen immer emittiert, wie bei den lokalen Stubs in
// test_aprs_decode.cpp/test_hey_report.cpp (env:native_aprs).
//
// printfdeb_functions.h ist hier bewusst NICHT der Weg: "#include
// "printfdeb_functions.h"" in mheard_functions.cpp/via_functions.cpp sucht
// zuerst im Verzeichnis der inkludierenden Datei (src/) -- das echte
// src/printfdeb_functions.h (nur Deklarationen, Implementierung haengt an
// Serial/net_console) gewinnt daher gegen jeden per -I gereichten Shim,
// unabhaengig von dessen Position in der Suchliste. Die fehlenden
// Implementierungen muessen deshalb hier stehen statt in einem gleichnamigen
// Header.
#pragma once

#ifndef NATIVE_BUILD
#error "test/test_decodemheard/stubs/parser_link_stubs.h darf nur im nativen Testbuild verwendet werden"
#endif

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <Arduino.h>

// ---- printfdeb_functions.h (src/) -- nur die tatsaechlich verlinkten Overloads
int printlndeb(const char *buff) { (void)buff; return 0; }
int printdeb(const char *buff) { (void)buff; return 0; }
int printdeb(String str) { (void)str; return 0; }
int printfdeb(const char *format, ...) { (void)format; return 0; }

// ---- loop_functions.h
unsigned long getUnixClock() { return 0; }
String getTimeString() { return String(""); }
void addBLEOutBuffer(uint8_t *buffer, uint16_t len) { (void)buffer; (void)len; }
void addBLEComToOutBuffer(uint8_t *buffer, uint16_t len) { (void)buffer; (void)len; }
bool is_equ(const char *buf1, const char *buf2)
{
    return buf1 != nullptr && buf2 != nullptr && strcmp(buf1, buf2) == 0;
}

// ---- time_functions.h
String convertUNIXtoString(uint32_t timestamp) { (void)timestamp; return String(""); }

// ---- loop_functions_extern.h (globals checkMesh()/checkVia() read)
bool bGATEWAY = false;
bool bVIA = false;
