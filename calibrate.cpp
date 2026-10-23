
#include <stdint.h>

#include "screen.h"

//=====================================================================
//  Calibration tactile
//
//  Sur la carte Waveshare ESP32-S3-Touch-LCD-4.3B, le tactile est un
//  GT911 : il renvoie directement des coordonnees en pixels, il n'y a
//  donc rien a calibrer (contrairement aux dalles resistives qui
//  utilisaient la calibration TFT_eSPI + SPIFFS).
//
//  La fonction est conservee pour ne pas modifier l'appel du sketch.
//=====================================================================

void touch_calibrate(TFT_eSPI &tft)
{
  (void)tft;

  Serial.println("[touch] GT911 : calibration inutile");
}
