#ifndef _ONEBUTTON_FUNCTIONS_H_
#define _ONEBUTTON_FUNCTIONS_H_

#include <Arduino.h>

void init_onebutton(void);

#include <OneButton.h>
extern OneButton btn;

void singleClick();
void doubleClick();
void tripleClick();
void PressLong();

#endif