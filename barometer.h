
#ifndef _INC_BAROMETER_H
#define _INC_BAROMETER_H

#include <stdint.h>
#include <TFT_eSPI.h>

#include "datetime_local.h"

//---------------------------------------------------------------------
// Bandeau d'informations (date / heure / pression) : c'est le rectangle
// gris du bas a droite de l'ecran. Il sert aussi de zone de touche pour
// ouvrir le menu (voir inMenuZone() dans Barometre_marin.ino), donc sa
// geometrie est definie ici une seule fois et reutilisee par
// barometer.cpp.
//---------------------------------------------------------------------

#define INFO_BAND_X  336          // bord gauche (identique a HIST_X)
#define INFO_BAND_Y  408          // bord haut (identique a INFO_Y)
#define INFO_BAND_W  456          // largeur (identique a HIST_W)
#define INFO_BAND_H   64          // hauteur (identique a INFO_H)

//---------------------------------------------------------------------
// Graphique de pression de la page de mesure : rectangle blanc a cadre
// gris, a droite de l'ecran. Sert de zone de touche pour l'historique
// plein ecran de la pression (voir inPressureZone()).
//---------------------------------------------------------------------

#define GRAPH_BAND_X  336         // bord gauche
#define GRAPH_BAND_Y   60         // bord haut
#define GRAPH_BAND_W  456         // largeur
#define GRAPH_BAND_H  340         // hauteur

//---------------------------------------------------------------------
// Series d'historique disponibles en plein ecran. Les trois partagent le
// meme mode (6 / 12 / 24 / 96 h) choisi dans le menu.
//---------------------------------------------------------------------

enum HistSeriesKind
{
  HIST_HUMIDITY,
  HIST_TEMPERATURE,
  HIST_PRESSURE
};

void drawPressureHist(float pres, DateTime& now);
void drawFullHistoryPage(HistSeriesKind kind);
void saveHistoricalValues(float pres, float temp, int16_t hum);
void initBarometer(TFT_eSPI *tft);

//---------------------------------------------------------------------
// Lecture des series d'historique, pour l'interface web (webui.cpp) et
// l'affichage. Il n'y a qu'un buffer de 384 mesures (96 h a 15 min) ;
// "hours" (6, 12, 24 ou 96) choisit la fenetre affichee :
//   - barometerHistWindow(hours) donne le nombre de points (hours x 4) ;
//   - barometerHistValue(kind, hours, i) renvoie la valeur du point i
//     (0 = le plus ancien de la fenetre), ou NAN pour une case vide.
//---------------------------------------------------------------------

int16_t  barometerHistCount();                          // mesures valides accumulees
int16_t  barometerHistWindow(int8_t hours);             // points de la fenetre (hours x 4)
int16_t  barometerHistStep(int8_t hours);               // 15 min, pour toutes les durees
uint32_t barometerHistStamp(int8_t hours);              // date de la derniere mesure
int32_t  barometerHistTimeOfDay(int8_t hours);          // heure locale (s depuis minuit)
void     barometerHistPressureScale(int8_t hours, int16_t *lo, int16_t *hi);
float    barometerHistValue(HistSeriesKind kind, int8_t hours, int16_t i);

#endif // _INC_BAROMETER_H
