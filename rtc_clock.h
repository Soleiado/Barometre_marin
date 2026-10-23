
#ifndef _INC_RTC_CLOCK_H
#define _INC_RTC_CLOCK_H

//=====================================================================
//  Horloge temps reel
//
//  Le projet d'origine utilisait un DS3231 externe via la bibliotheque
//  RTClib (qui passe par la classe Wire d'Arduino, inutilisable sur
//  cette carte : voir i2c_bus.h).
//
//  Ce module remplace RTClib pour l'acces materiel et sait utiliser :
//    - un DS3231 externe (0x68), comme avant ;
//    - le PCF85063A present sur la carte Waveshare (0x51) ;
//    - a defaut, une horloge logicielle (perdue a la mise hors tension).
//
//  Le type DateTime utilise partout ailleurs vient de datetime_local.h
//  (copie minimale de celui de RTClib) : RTClib ne doit PAS etre liee, car
//  elle tire la classe Wire d'Arduino et donc le nouveau pilote I2C, qui
//  entre en conflit avec l'ancien pilote exige par ESP32_Display_Panel.
//=====================================================================

#include "datetime_local.h"

bool     rtcClockInit();        // detecte le circuit et l'initialise
DateTime rtcClockNow();         // heure courante
bool     rtcClockAdjust(DateTime dt);  // reglage de l'heure
bool     rtcClockIsHardware();  // true si un circuit a ete trouve
bool     rtcClockLostPower();   // true si le circuit signale une perte d'alimentation

#endif // _INC_RTC_CLOCK_H
