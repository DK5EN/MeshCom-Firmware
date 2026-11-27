#ifndef _PHONE_COMMANDS_H_
#define _PHONE_COMMANDS_H_

#include <stddef.h>
#include <stdint.h>
#include "phone_link.h"

// len = bytes actually received (<= MAX_MSG_LEN_PHONE); a frame whose header claims more is dropped
void readPhoneCommand(const uint8_t *conf_data, size_t len, PhoneLinkOwner src = PL_BLE);
void phoneSessionEnded(PhoneLinkOwner who);
void sendToPhone();
void sendComToPhone();
void sendConfigToPhone ();

#endif