#include <configuration.h>

void esp32setup();

void esp32loop();

int checkRX(bool bRadio);

// TD-30: runtime BLE / WLAN switches (header icons, console). The caller stores node_bleon itself.
void esp32SetBleEnabled(bool on);
#if defined(BOARD_T_DECK) || defined(BOARD_T_DECK_PLUS)
void esp32SetWifiEnabled(bool on);
#endif
