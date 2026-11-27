/*
  RadioLib SX126x Spectrum Scan Example

  This example shows how to perform a spectrum power scan using SX126x.
  The output is in the form of scan lines, each line has 33 power bins.
  First power bin corresponds to -11 dBm, the second to -15 dBm and so on.
  Higher number of samples in a bin corresponds to more power received
  at that level. The example performs frequency sweep over a given range.

  To show the results in a plot, run the Python script
  RadioLib/extras/SX126x_Spectrum_Scan/SpectrumScan.py

  WARNING: This functionality is experimental and requires a binary patch
  to be uploaded to the SX126x device. There may be some undocumented
  side effects!

  For default module settings, see the wiki page
  https://github.com/jgromes/RadioLib/wiki/Default-configuration#sx126x---lora-modem

  For full API reference, see the GitHub Pages
  https://jgromes.github.io/RadioLib/
*/
#include "spectral_scan.h"
#include "lora_setchip.h"
#include "esp32/esp32_flash.h"
#include "esp32/esp32_main.h"
#include "watchdog_feed.h"

#if defined(ESP32)
#include "esp_task_wdt.h"
#endif

#if defined(BOARD_T_DECK_PRO)
//extern hardware
#elif defined(SX1262X) || defined(SX126X) || defined(SX1268_V3) || defined(SX1262_V3) || defined(SX1262_E290) || defined(USING_SX1262)

#include <RadioLib.h>

using namespace std;

#define SPECTRAL_STEP_TIMEOUT_MS 1000UL


// include the library


// this file contains binary patch for the SX1262
#include <modules/SX126x/patches/SX126x_patch_scan.h>



/**
 * ###########################################################################################################################
 * Initializes the scanning
 * @param freq the initial frequency - this does not need to be the first frequency we start scanning with
 * @return a result code - see RadioLib's TypeDef.h
 */
int sx126x_spectral_init_scan(float freq)
{
  // initialize SX1262 FSK modem at the initial frequency
  int state = radio.beginFSK(freq);
  if (state != RADIOLIB_ERR_NONE)
  {
    return state;
  }

  // upload a patch to the SX1262 to enable spectral scan
  // NOTE: this patch is uploaded into volatile memory,
  //       and must be re-uploaded on every power up
  state = radio.uploadPatch(sx126x_patch_scan, sizeof(sx126x_patch_scan));
  if (state != RADIOLIB_ERR_NONE)
  {
    return state;
  }

  // configure scan bandwidth to 234.4 kHz
  // and disable the data shaping
  state = radio.setRxBandwidth(234.3);  //any other frequwency does not work - maybe those values are somehow fixed in the binary blob of the scan patch
  //ToDo: check if that patch can be recompiled allowing a narrower bandwidth, see: https://github.com/Lora-net/sx1302_hal/blob/master/util_spectral_scan/src/spectral_scan.c
  state |= radio.setDataShaping(RADIOLIB_SHAPING_NONE);
  if (state != RADIOLIB_ERR_NONE)
  {
    return state;
  }

  // if we reach this point, everythung should be okay
  return RADIOLIB_ERR_NONE;
}




/**
 * ###########################################################################################################################
 * Once the scan has finished, we wish ro return to a normal working state.
 * We need to set back our default LoRa Modem configuration
 * 
 */
int sx126x_spectral_finish_scan()
{
  // Chip reset back to LoRa mode, then everything setup applied: the begin as setup does it (per board),
  // TCXO / boosted gain / current limit (lost with the reset, not part of lora_setchip_meshcom()), then
  // freq/BW/SF/CR/sync/preamble/power/CRC. Failures are logged and returned, never swallowed.
  // Only a failed begin stops the restore. An extras error is logged and reported like at setup (which
  // carries on as well), but the node's channel and LoRa parameters are applied regardless: otherwise RX
  // would come back on the begin() defaults.
  const char *step = "begin";
  int rc = loraChipBegin();
  if (rc == RADIOLIB_ERR_NONE)
  {
    const int extras = loraApplyChipExtras();
    const bool setchip_ok = lora_setchip_meshcom();
    if (extras != RADIOLIB_ERR_NONE)
    {
      step = "extras";
      rc = extras;
    }
    else if (!setchip_ok)
    {
      step = "setchip";
      rc = RADIOLIB_ERR_UNKNOWN;
    }
  }
  if (rc != RADIOLIB_ERR_NONE)
    Serial.printf("[LoRa] spectrum re-init failed rc=%d step=%s\n", rc, step);
  return rc;
}



/**
 * ###########################################################################################################################
 * Starts the scan of a small frequency span beginning with the frequency defined as parameter "freq" and the scan width of 200kHz
 * @param freq the starting frequency of the 200kHz span to scan
 * @param samples the amount of samples taken (fewer samples = better temporal resolution)
 * @return an array containing the results
 */
uint16_t *sx126x_spectral_scan_freq(float freq, uint16_t samples)
{
  uint16_t *results = new uint16_t[RADIOLIB_SX126X_SPECTRAL_SCAN_RES_SIZE](); // () value-initialises: zeros, we will return the array containing zeros if anything goes wrong
  if (radio.setFrequency(freq) == RADIOLIB_ERR_NONE)
  {
    // start spectral scan
    if (radio.spectralScanStart(samples) == RADIOLIB_ERR_NONE)
    { // if scan started without an error, wait for spectral scan to finish
      // Bounded poll (FORMAL-02 B8): one step takes a few ms; a stuck chip must not hang the loop task.
      const unsigned long t0 = millis();
      while (radio.spectralScanGetStatus() != RADIOLIB_ERR_NONE){
        if ((unsigned long)(millis() - t0) > SPECTRAL_STEP_TIMEOUT_MS)
        {
          delete[] results;
          return nullptr;   // callers handle nullptr (timeout)
        }
        meshcom_wdt_feed();
        delay(10);
      }

      // read the results
      if (radio.spectralScanGetResult(results) == RADIOLIB_ERR_NONE){
        return results;
      }
    }
  }

  // if we reach this point, something went wrong
  return results; // return an array of zeros
}



/**
 * ###########################################################################################################################
 * Performs a spectrum scan over the defined fequency range (freqStart to freqEnd)
 * Formats the output to be compatible wuth RadioLib's Example.
 *
 * This function might be removed later.
 */
void sx126x_spectral_scan()
{
  
  float freqStart = 430.0;          // scan start frequency in MHz 
  float freqEnd = 440.2;      // scan end frequency in MHz 
  float samples = 2048;       // amount of samples to be taken

  if(getFreq() > 500)
  {
    freqStart = 863.0;          // scan start frequency in MHz 
    freqEnd = 870.0;      // scan end frequency in MHz 
    samples = 2048;       // amount of samples to be taken
  }

  sx126x_spectral_init_scan(freqStart);

  while (freqStart <= freqEnd)
  {
    Serial.print("FREQ ");
    Serial.println(freqStart, 2);
    Serial.print(F("[SX1262] Starting spectral scan ... \n"));
    // get the results
    uint16_t *res;
    res = sx126x_spectral_scan_freq(freqStart, samples);
    if (res == nullptr)
    {
      Serial.println("SCAN TIMEOUT");
      break;
    }

		esp_task_wdt_reset();

    // we have some results, print it
    Serial.print("SCAN ");
    for (uint8_t i = 0; i < RADIOLIB_SX126X_SPECTRAL_SCAN_RES_SIZE; i++)
    {
      Serial.print(res[i]);
      Serial.print(',');
    }
    Serial.println(" END");

    delete[] res;
    res = nullptr;

    // wait a little bit before the next scan
    delay(100);

    // set the next frequency
    // the frequency step should be slightly smaller
    // or the same as the Rx bandwidth set in setup
    freqStart += 0.2;

    // yield();    //pet the watchdog so it stays calm
  }
  Serial.println("SCAN END");

  // Legacy serial console scan: a failed restore is already logged by finish_scan; the console has no
  // caller to hand the status to, so it is ignored here on purpose.
  (void)sx126x_spectral_finish_scan();
}

int init_scan(float freq)
{
return RADIOLIB_ERR_WRONG_MODEM;
}

#else
// Serial.print(F("Spectral scan not implemented for this plattform ... "));
// DUMMY FOR NON-SX1262
void sx126x_spectral_scan()
{
  Serial.print(F("Spectral scan not implemented for this plattform ... "));
  return;
}


#endif
