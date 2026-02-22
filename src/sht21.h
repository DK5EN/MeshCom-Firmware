
#ifndef _SHT21_H_
#define _SHT21_H_

#include <Arduino.h>

#if defined (ENABLE_SHT21)

void setupSHT21(bool bNewStart);

bool loopSHT21(void);

float getSHT21Temp();
float getSHT21Hum();

#endif

#endif
