#ifndef _PHONE_COMMANDS_H_
#define _PHONE_COMMANDS_H_

#include "phone_link.h"

void readPhoneCommand(uint8_t conf_data[MAX_MSG_LEN_PHONE], PhoneLinkOwner src = PL_BLE);
void phoneSessionEnded(PhoneLinkOwner who);
void sendToPhone();
void sendComToPhone();
void sendConfigToPhone ();

#endif