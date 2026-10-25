
#ifndef _INC_DEFINITION_H
#define _INC_DEFINITION_H

// Historique : un SEUL buffer de 384 mesures par grandeur (pression,
// humidite, temperature), a raison d'une mesure toutes les 15 min, soit
// 96 heures de profondeur. Les vues 6 / 12 / 24 / 96 h sont des fenetres
// glissantes dans ce buffer (24, 48, 96 ou 384 points a 15 min).
#define HIST_SLOTS        384     // 96 h x 4 (une mesure toutes les 15 min)
#define HIST_STEP_MIN     15      // pas de temps fixe entre deux mesures

#define NOPRESSHUM        -1      // mbar
#define NOTEMP            ((int16_t)-32768)

// Ces 3 valeurs ne servent qu'à determiner l'echelle initiale de l'axe des Y pour les pressions
#define MINPRESSURE       980     // mbar (plage affichee : 1030 -> 980)
#define MIDPRESSURE       1005    // mbar
#define MAXPRESSURE       1030    // mbar

#define DROPPRESSURE_96H  28      // pressure drop in 1h
#define DROPPRESSURE_48H  14      // pressure drop in 1h
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
#define MODE_48H          48
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
