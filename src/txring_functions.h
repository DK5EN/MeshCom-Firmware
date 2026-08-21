#ifndef _TXRING_FUNCTIONS_H_
#define _TXRING_FUNCTIONS_H_

// TX-Ring-Kern (Prio-Klassifizierung, Slot-Auswahl, Enqueue/Overflow):
// aus lora_functions.cpp extrahiert (QA-Welle 2026-08-22), damit dieser vom
// Projekteigner ausdruecklich misstraute Code nativ (ohne Hardware) getestet
// werden kann. Logik unveraendert — reine Verschiebung in eine eigene
// Uebersetzungseinheit. Siehe docs/.../08-defect-catalogue.md N-14 und
// test/test_txring/test_txring.cpp.
//
// advanceIReadPastEmpty() ist hier zusaetzlich deklariert (nicht mehr rein
// TU-lokal wie im Original): doTX() in lora_functions.cpp ruft sie ebenfalls
// auf (Zeile ~1837 vor der Extraktion) und braucht daher externe Sichtbarkeit.
// Siehe Abweichungsvermerk im Wave-Report.

#include <Arduino.h>
#include <configuration.h>

uint8_t getMessagePriority(int slot);
int getNextTxSlot(void);
void advanceIReadPastEmpty(void);

#endif // _TXRING_FUNCTIONS_H_
