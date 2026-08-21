// printfdeb_functions.h fuer den nativen Testbuild.
//
// Schattet src/printfdeb_functions.h (die -I test/support geht in
// platformio.ini vor -I src). Das echte printfdeb_functions.cpp haengt an
// Serial/net_console und wird nativ nicht gebaut; wer den echten Header
// zieht, braucht trotzdem eine Implementierung jeder deklarierten Signatur,
// sonst bricht der Linker. Hier: No-Op-Stubs, inline, damit keine eigene
// Uebersetzungseinheit noetig ist.
//
// Nur txring_functions.cpp zieht diesen Header aktuell (siehe dort); die
// bestehenden native_aprs-Suiten (test_aprs_spec, test_aprs_decode, ...)
// bauten schon vorher ohne jede printfdeb-Implementierung, weil weder
// aprs_functions.cpp noch dessen Includes (loop_functions.h,
// loop_functions_extern.h, regex_functions.cpp, lora_setchip.h)
// printfdeb_functions.h ziehen -- dieser Shim aendert daran nichts.
#pragma once

#ifndef NATIVE_BUILD
#error "test/support/printfdeb_functions.h darf nur im nativen Testbuild verwendet werden"
#endif

#include <Arduino.h>

inline int printlndeb(const char *buff) { (void)buff; return 0; }
inline int printdeb(const char *buff) { (void)buff; return 0; }

inline int printlndeb(int iVar) { (void)iVar; return 0; }

inline int printdeb(int iVar) { (void)iVar; return 0; }
inline int printdeb(unsigned int iVar) { (void)iVar; return 0; }
inline int printdeb(short iVar) { (void)iVar; return 0; }
inline int printdeb(float fVar) { (void)fVar; return 0; }
inline int printdeb(char c) { (void)c; return 0; }
inline int printdeb(unsigned char c) { (void)c; return 0; }

inline int printlndeb(String str) { (void)str; return 0; }
inline int printdeb(String str) { (void)str; return 0; }

inline int printfdeb(const char *format, ...) { (void)format; return 0; }
