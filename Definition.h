
#ifndef _INC_DEFINITION_H
#define _INC_DEFINITION_H

#define MAXHOURTIMESLOT   25      // Points d'historique : (25 - 1) intervalles
                                  // de 15 min (6h), 30 min (12h), 1 h (24h)
                                  // ou 4 h (96h) -> l'axe couvre exactement
                                  // la duree choisie.
#define MAXHISTORY        100     // Historical data - 4 days

#define NOPRESSHUM        -1      // mbar
#define NOTEMP            ((int16_t)-32768)
#define MINPRESSURE       980     // mbar (plage affichee : 1030 -> 980)
#define MIDPRESSURE       1005    // mbar
#define MAXPRESSURE       1030    // mbar

#define DROPPRESSURE_96H  28      // pressure drop in 1h
#define DROPPRESSURE_24H  7       // pressure drop in 1h
#define DROPPRESSURE_12H  4       // pressure drop in 30min
#define DROPPRESSURE_06H  2       // pressure drop in 15min

#define SENSOR_DELAY       30000L  // 30s
#define NMEA_DELAY         10000L  // 10s
#define SCREEN_SAVER_DELAY 300000L // 5mins
#define SAVE_DATA_DELAY    900000L // 15mins

// Console de diagnostic USB (0 = supprimee) : 's' force la veille,
// 'b' bascule le retro eclairage, 'i' affiche l'etat courant.
#define SERIAL_CONSOLE_DEBUG 0

#define MODE_96H          96
#define MODE_24H          24
#define MODE_12H          12
#define MODE_06H          06

#define NOTHING_PRESSED   -2
#define DEFAULT_BUTTON    10

#define PAGE_MENU         1
#define PAGE_DATA         0
#define PAGE_SCREEN_SAVER -99

// Historiques plein ecran (ouverts par un appui sur le widget correspondant)
#define PAGE_HUM_HIST     2
#define PAGE_TEMP_HIST    3
#define PAGE_PRES_HIST    4

//===========================================
// Debug code, set MYDEBUG to 1
//===========================================

#define MYDEBUG 1

#if MYDEBUG == 1
#define debug(x) Serial.print(x)
#define debugln(x) Serial.println(x)
#else
#define debug(x)
#define debugln(x)
#endif

//===========================================
// functions declaration
//===========================================

void update_humidity_temp(bool init, int humidity, float temp, bool redraw_needle = false);

//===========================================
// external variables
//===========================================

#endif
