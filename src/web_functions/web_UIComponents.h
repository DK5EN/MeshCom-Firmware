
#ifndef _WEB_UICOMPONENTS_H_
#define _WEB_UICOMPONENTS_H_

#include <Arduino.h>
#include "web_commonServer.h"

void uic_cssstub();

void uic_button(CommonWebClient* target, char onClickHandler[], char buttonCaption[], char confirmationText[]);
void uic_button(CommonWebClient* target, char onClickHandler[], char buttonCaption[]);

void uic_input(CommonWebClient* target, char id[], char onChangeHandler[], char value[]);
#endif