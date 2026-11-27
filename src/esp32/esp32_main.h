#include <configuration.h>

void esp32setup();

void esp32loop();

int checkRX(bool bRadio);

// TD-30: runtime BLE / WLAN switches (header icons, console). The caller stores node_bleon itself.
void esp32SetBleEnabled(bool on);
#if defined(BOARD_T_DECK) || defined(BOARD_T_DECK_PLUS)
void esp32SetWifiEnabled(bool on);
#endif

// FORMAL-02 B8: RX hand-over around the spectrum sweep (spectral_sweep.cpp).
bool loraRxDisarmForScan(void);   // false: no radio, TX/read/RX pending or a packet mid-air; radio untouched
bool loraRxRearmAfterScan(void);  // startReceive + interrupt re-wire + csma_reset; false: startReceive failed (logged)

// RF chip begin and the chip settings setup applies after it (TCXO, boosted gain, current limit): shared by
// setup and sx126x_spectral_finish_scan(). Return the first RadioLib error or 0. Not built on BOARD_T5_EPAPER.
int loraChipBegin(void);
int loraApplyChipExtras(void);
