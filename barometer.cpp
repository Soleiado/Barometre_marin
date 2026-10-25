
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <TFT_eSPI.h> // Hardware-specific library
#include "datetime_local.h"
#include "rtc_clock.h"     // DS3231 / PCF85063A / horloge logicielle
#include "Definition.h"
#include "barometer.h"     // geometrie du bandeau d'informations (INFO_BAND_*)

//---------------------------------------------------------------------
// Geometrie du graphique et de ses bandeaux (canvas 800x480).
//
// Le graphique occupe la colonne de droite : 456 px de large, 300 px de
// haut, avec le bandeau de titre au-dessus et le bandeau d'informations
// en dessous. La pression est tracee verticalement a raison de
// HIST_PPMB pixels par millibar, HIST_TOP etant la ligne du maximum.
//---------------------------------------------------------------------

#define HIST_W        GRAPH_BAND_W                          // largeur des sprites
#define HIST_H        GRAPH_BAND_H                          // hauteur du graphique
#define HIST_X        GRAPH_BAND_X                          // origine a l'ecran
#define HIST_Y        GRAPH_BAND_Y
#define HIST_LEFT      72                                  // x du premier point
#define HIST_RIGHT   (HIST_W - 20)                          // x du dernier point
#define HIST_TOP       20                                  // y de max_pressure
#define HIST_PPMB       6                                  // pixels par mbar
#define HIST_SPAN      50                                  // plage 1030 -> 980 hPa
#define HIST_LABELS    (HIST_SPAN / 10 + 1)                // 6 etiquettes
#define HIST_BOTTOM  (HIST_TOP + HIST_SPAN * HIST_PPMB)

#define TITLE_Y         8
#define TITLE_H        44
#define INFO_Y        INFO_BAND_Y                          // bandeau date / heure / pression
#define INFO_H        INFO_BAND_H

int8_t  history_mode = MODE_06H;

// MAX, MID, MINPRESSURE représentent les limites du graphique de pressions au démarrage.
// L'échelle est flottante et s'adapte en fonction de la pression pour ne representer qu'un interval de 50mb
int16_t max_pressure = MAXPRESSURE;
int16_t mid_pressure = MIDPRESSURE;
int16_t min_pressure = MINPRESSURE;

// Valeurs affichées au démarrage. Ces valeurs pevent changer, car l'echelle est flottante
char    label[HIST_LABELS][5] = {"1030", "1020", "1010", "1000", "0990", "0980"};

int16_t normal_pressure_y = HIST_TOP;

// Scratch : pression du mode courant convertie en pixels y.
int16_t meter_data[HIST_SLOTS];

// Historique unique : une mesure toutes les 15 min, 96 h de profondeur.
// L'indice 0 est la plus ancienne, HIST_SLOTS-1 la plus recente. Une case
// non encore remplie vaut NOPRESSHUM (hum/pression) ou NOTEMP (temperature).
static int16_t hum_data     [HIST_SLOTS];
static int16_t temp_data    [HIST_SLOTS];
static int16_t pressure_data[HIST_SLOTS];

// Nombre de mesures valides accumulees (0..HIST_SLOTS).
static int16_t histCount = 0;

TFT_eSprite *graph;
TFT_eSprite *misc;    // bandeau de titre
TFT_eSprite *info;    // bandeau date / heure / pression

// Ecran de destination (canvas TFT_eSprite de la classe Screen)
static TFT_eSPI *s_tft = nullptr;

// Date de la derniere mesure (la plus recente).
DateTime last_update;

//====================================================
// initBarometer: cree les sprites du graphique et
// vide les series d'historique.
//====================================================

void initBarometer(TFT_eSPI *tft)
{
  s_tft = tft;

  graph = new TFT_eSprite(tft);
  misc  = new TFT_eSprite(tft);
  info  = new TFT_eSprite(tft);

  // Sprites 16 bits (profondeur par defaut) : voir la remarque sur les couleurs
  graph->createSprite(HIST_W, HIST_H);
  misc->createSprite(HIST_W, TITLE_H);
  info->createSprite(HIST_W, INFO_H);

  // Buffer vide au demarrage : aucune mesure avant la premiere lecture.
  histCount = 0;
  last_update = rtcClockNow();

  for (int16_t i = 0; i < HIST_SLOTS; i++) {
    pressure_data[i] = NOPRESSHUM;
    hum_data[i]      = NOPRESSHUM;
    temp_data[i]     = NOTEMP;
  }
}

//====================================================
// Fenetres d'historique
//
// Il n'y a qu'un buffer (une mesure toutes les 15 min) : une fenetre de
// "hours" heures vaut hours x 4 points. Le point 0 de la fenetre est le
// plus ancien, le dernier est la mesure la plus recente.
//====================================================

static int16_t windowPoints(int8_t hours)
{
  return (int16_t)hours * 4;      // 6->24, 12->48, 24->96, 96->384
}

static int16_t windowStart(int8_t hours)
{
  return (int16_t)(HIST_SLOTS - windowPoints(hours));
}

//====================================================
// Acces en lecture pour l'interface web et l'affichage
//====================================================

int16_t barometerHistCount()
{
  return histCount;
}

int16_t barometerHistWindow(int8_t hours)
{
  return windowPoints(hours);
}

int16_t barometerHistStep(int8_t hours)
{
  (void) hours;                   // pas de temps unique pour toutes les durees

  return HIST_STEP_MIN;
}

uint32_t barometerHistStamp(int8_t hours)
{
  (void) hours;

  return last_update.unixtime();
}

// Heure locale de la derniere mesure, en secondes depuis minuit : evite
// toute conversion de fuseau horaire dans la page web.
int32_t barometerHistTimeOfDay(int8_t hours)
{
  (void) hours;

  return (int32_t)last_update.hour() * 3600 + (int32_t)last_update.minute() * 60
         + last_update.second();
}

// Pression en hPa, humidite en %HR, temperature en degres C.
// "i" indexe la fenetre (0 = plus ancien).
float barometerHistValue(HistSeriesKind kind, int8_t hours, int16_t i)
{
  if (i < 0 || i >= windowPoints(hours))
    return NAN;

  int16_t idx = windowStart(hours) + i;

  switch (kind) {
    case HIST_HUMIDITY:
      return (hum_data[idx] == NOPRESSHUM) ? NAN : (float)hum_data[idx];

    case HIST_TEMPERATURE:
      return (temp_data[idx] == NOTEMP) ? NAN : (float)temp_data[idx] / 10.0f;

    default:
      return (pressure_data[idx] == NOPRESSHUM) ? NAN : (float)pressure_data[idx];
  }
}

//====================================================
// Decale le buffer d'un cran et place la nouvelle
// mesure en derniere position (la plus recente).
//====================================================

static void shiftSeries(int16_t *data)
{
  memmove(data, data + 1, (HIST_SLOTS - 1) * sizeof(int16_t));
}

//====================================================
// appendSample: ajoute une mesure aux trois grandeurs.
// La pression, l'humidite et la temperature partagent
// ainsi exactement le meme axe des temps.
//====================================================

static void appendSample(float pres, float temp, int16_t hum)
{
  shiftSeries(pressure_data);
  shiftSeries(hum_data);
  shiftSeries(temp_data);

  pressure_data[HIST_SLOTS - 1] = (int16_t)lroundf(pres);
  hum_data[HIST_SLOTS - 1]      = hum;
  temp_data[HIST_SLOTS - 1]     = (int16_t)lroundf(temp * 10.0f);

  if (histCount < HIST_SLOTS)
    histCount++;

  last_update = rtcClockNow();
}

//====================================================
// saveHistoricalValues: ajoute la mesure courante au
// buffer, une fois toutes les 15 min (SAVE_DATA_DELAY).
//====================================================

void saveHistoricalValues(float pres, float temp, int16_t hum)
{
  static unsigned long last_time = millis();
  static bool         first_time = true;

  if (millis() - last_time > SAVE_DATA_DELAY || first_time) {
    appendSample(pres, temp, hum);

    last_time  = millis();
    first_time = false;
  }
}

//====================================================
// Echelle verticale de la pression, commune a l'ecran et
// a la page web.
//
// La fenetre fait 50 hPa (6 etiquettes tous les 10 hPa). Tant que la
// derniere mesure reste dans la plage de base [980, 1030], la fenetre est
// celle d'origine ; sinon elle glisse par pas de 10 hPa pour contenir la
// mesure (2 hPa de marge en haut et en bas). Le buffer etant unique, une
// seule echelle sert a toutes les durees : l'ecran et le web voient donc
// exactement la meme fenetre.
//====================================================

static int16_t s_scaleMax = MAXPRESSURE;
static int16_t s_scaleMin = MINPRESSURE;

void barometerHistPressureScale(int8_t hours, int16_t *lo, int16_t *hi)
{
  (void) hours;

  int16_t last = pressure_data[HIST_SLOTS - 1];
  int16_t maxp = s_scaleMax;
  int16_t minp = s_scaleMin;

  if ((last == NOPRESSHUM) ||
      ((last <= MAXPRESSURE - 2) && (last >= MINPRESSURE + 2))) {
    maxp = MAXPRESSURE;
    minp = MINPRESSURE;
  } else {
    while (last > maxp - 2) {
      maxp += 10;
      minp += 10;
    }
    while (last < minp + 2) {
      maxp -= 10;
      minp -= 10;
    }
  }

  s_scaleMax = maxp;
  s_scaleMin = minp;

  if (lo) *lo = minp;
  if (hi) *hi = maxp;
}

//====================================================
// mapPressureHistValues: Map actual pressure values in the
// a pressure range [1003,1023] 'mbar' to fit scale.
//====================================================

void mapPressureHistValues() {
  int16_t w     = windowPoints(history_mode);
  int16_t start = windowStart(history_mode);

  barometerHistPressureScale(history_mode, &min_pressure, &max_pressure);
  mid_pressure = (max_pressure + min_pressure) / 2;

  for (int i = 0; i < HIST_LABELS; i++) {
    sprintf(label[i], "%04d", (max_pressure - 10 * i));
  }
  normal_pressure_y = HIST_TOP + (max_pressure - 1013) * HIST_PPMB;

  for (int16_t i = 0; i < w; i++) {
    int16_t pressure = pressure_data[start + i];
    if (pressure != NOPRESSHUM) {
      if (pressure > max_pressure)
        pressure = max_pressure;
      else if (pressure < min_pressure)
        pressure = min_pressure;

      meter_data[i] = HIST_TOP + (max_pressure - pressure) * HIST_PPMB;
    } else
      meter_data[i] = NOPRESSHUM;
  }

  return;
}

//====================================================
// drawPressureHist: 
//====================================================

void drawPressureHistBg()
{
  graph->fillSprite(TFT_WHITE);

  // Cadre en degrade (8 px d'epaisseur)
  graph->fillRectHGradient(           0,  0,  8, HIST_H, TFT_LIGHTGREY, TFT_DARKGREY);
  graph->fillRectHGradient(HIST_W - 8,  0,  8, HIST_H, TFT_LIGHTGREY, TFT_DARKGREY);
  graph->fillRectVGradient(           0,  0, HIST_W,     8, TFT_DARKGREY, TFT_LIGHTGREY);
  graph->fillRectVGradient(           0, HIST_H - 8, HIST_W, 8, TFT_DARKGREY, TFT_LIGHTGREY);

  // Ligne verte : pression standard (1013 hPa)
  if (normal_pressure_y > HIST_TOP - 6 && normal_pressure_y < HIST_BOTTOM + 6)
    graph->drawWideLine(HIST_LEFT - 10, normal_pressure_y, HIST_RIGHT + 10, normal_pressure_y,
                        6, TFT_GREEN, TFT_GREEN);

  // Libelles de pression : police vectorielle, un peu plus grande et plus
  // lisible que la police bitmap 2 utilisee a l'origine.
  graph->setFreeFont(&FreeSansBold12pt7b);

  // Lignes horizontales tous les 5 hPa, libelle tous les 10 hPa
  for (int i = 0; i <= HIST_SPAN; i += 5) {
    int16_t y = HIST_TOP + i * HIST_PPMB;

    if (i % 10 == 0) {
      graph->drawWideLine(HIST_LEFT - 10, y, HIST_RIGHT + 10, y, 3, TFT_DARKGREY, TFT_DARKGREY);

      graph->setTextColor(TFT_DARKGREY);
      graph->setTextDatum(CR_DATUM);
      graph->drawString(label[i / 10], HIST_LEFT - 14, y);
    } else {
      graph->drawLine(HIST_LEFT - 10, y, HIST_RIGHT + 10, y, TFT_LIGHTGREY);
    }
  }

  // Lignes verticales : cinq segments reguliers sur l'axe des temps
  for (int k = 0; k <= 5; k++) {
    int16_t x = HIST_LEFT + k * (HIST_RIGHT - HIST_LEFT) / 5;
    graph->drawLine(x, HIST_TOP - 10, x, HIST_BOTTOM, TFT_BLACK);
  }
}

//====================================================
// drawPressureHist: 
//====================================================

void drawPressureHist(float pres,  DateTime& now)
{
  int16_t drop_pressure;
  String  title;

  switch (history_mode) {
    case MODE_06H:
      drop_pressure = DROPPRESSURE_06H;
      title = "Atm. Pres. Hist.: 6h";
      break;
    case MODE_12H:
      drop_pressure = DROPPRESSURE_12H;
      title = "Atm. Pres. Hist.: 12h";
      break;
    case MODE_24H:
      drop_pressure = DROPPRESSURE_24H;
      title = "Atm. Pres. Hist.: 24h";
      break;
    case MODE_48H:
      drop_pressure = DROPPRESSURE_48H;
      title = "Atm. Pres. Hist.: 48h";
      break;
    case MODE_96H:
      drop_pressure = DROPPRESSURE_96H;
      title = "Atm. Pres. Hist.: 96h";
      break;
  }

  mapPressureHistValues();
  drawPressureHistBg();

  int16_t w = windowPoints(history_mode);

  for (int16_t i = 0; i < w; i++) {
    int16_t x = HIST_LEFT + (int32_t)i * (HIST_RIGHT - HIST_LEFT) / (w - 1);

    if (meter_data[i] == NOPRESSHUM)
      continue;

    uint32_t color = TFT_NAVY;

    if (i > 0 && meter_data[i - 1] != NOPRESSHUM) {
      if (meter_data[i - 1] - meter_data[i] >= drop_pressure * HIST_PPMB)
        color = TFT_RED;

      int16_t px = HIST_LEFT + (int32_t)(i - 1) * (HIST_RIGHT - HIST_LEFT) / (w - 1);
      graph->drawLine(px, meter_data[i - 1], x, meter_data[i], color);
    }

    graph->fillCircle(x, meter_data[i], 3, color);
  }

  graph->drawRect(0, 0, HIST_W - 1, HIST_H - 1, TFT_BLACK);
  graph->pushToSprite(static_cast<TFT_eSprite *>(s_tft), HIST_X, HIST_Y);

  // Bandeau de titre
  misc->fillRectVGradient(0, 0, HIST_W, TITLE_H, TFT_LIGHTGREY, TFT_DARKGREY);
  misc->drawRect(0, 0, HIST_W - 1, TITLE_H - 1, TFT_BLACK);
  misc->setTextDatum(TC_DATUM);
  misc->setTextColor(TFT_BLACK);
  misc->setTextFont(4);
  misc->drawString(title, HIST_W / 2 - 1, 9);
  misc->drawString(title, HIST_W / 2,     9);
  misc->drawString(title, HIST_W / 2 + 1, 9);
  misc->pushToSprite(static_cast<TFT_eSprite *>(s_tft), HIST_X, TITLE_Y);

  // Bandeau date / heure / pression :
  //   - a gauche, la date avec l'heure centree dessous (meme axe vertical) ;
  //   - a droite, la pression en 18pt, centree horizontalement et
  //     verticalement dans la hauteur du bandeau.
  char buf[25];

  info->fillRectVGradient(0, 0, HIST_W, INFO_H, TFT_LIGHTGREY, TFT_DARKGREY);
  info->drawRect(0, 0, HIST_W - 1, INFO_H - 1, TFT_BLACK);
  info->setTextColor(TFT_BLACK);

  const int16_t leftX  = HIST_W * 30 / 100;    // 136 : axe date / heure
  const int16_t rightX = HIST_W * 75 / 100;    // 342 : axe de la pression

  // Date et heure
  info->setTextFont(4);
  info->setTextDatum(TC_DATUM);

  sprintf(buf, "%4d-%02d-%02d", now.year(), now.month(), now.day());

  info->drawString(buf, leftX - 1, 3);
  info->drawString(buf, leftX,     3);
  info->drawString(buf, leftX + 1, 3);

  sprintf(buf, "%02d:%02d", now.hour(), now.minute());

  info->drawString(buf, leftX - 1, 31);
  info->drawString(buf, leftX,     31);
  info->drawString(buf, leftX + 1, 31);

  // Pression : plus grande, centree dans la moitie droite du bandeau
  info->setFreeFont(&FreeSansBold18pt7b);
  info->setTextDatum(MC_DATUM);

  sprintf(buf, "%.2f mb", pres);

  info->drawString(buf, rightX - 1, INFO_H / 2);
  info->drawString(buf, rightX,     INFO_H / 2);
  info->drawString(buf, rightX + 1, INFO_H / 2);

  info->pushToSprite(static_cast<TFT_eSprite *>(s_tft), HIST_X, INFO_Y);
}

//---------------------------------------------------------------------
// Historique plein ecran (humidite, temperature, pression)
//
// Meme presentation que le graphique de la page de mesure, etendu a toute
// la dalle : bandeau de titre, cadre, lignes de grille avec les valeurs a
// gauche, courbe et, en bas, l'heure des mesures. Toutes les vues lisent
// le buffer unique a 15 min : le nombre de points depend de la duree
// choisie (24 pour 6 h, 48 pour 12 h, 96 pour 24 h, 384 pour 96 h).
//---------------------------------------------------------------------

#define FULL_W          800
#define FULL_H          480
#define FULL_TITLE_H     52
#define FULL_PLOT_X      92
#define FULL_PLOT_Y      84
#define FULL_PLOT_W     672
#define FULL_PLOT_H     310
#define FULL_XLABEL_Y   (FULL_PLOT_Y + FULL_PLOT_H + 10)

// Nombre de reperes verticaux (grille + etiquette d'heure) sur l'axe,
// du plus ancien (bord gauche) a maintenant (bord droit).
#define FULL_LABELS      6

extern bool bfahrenheit;

//---------------------------------------------------------------------
// Pas de temps, en minutes, entre deux mesures (fixe : 15 min).
//---------------------------------------------------------------------

static int16_t historyStepMinutes()
{
  return barometerHistStep(history_mode);
}

//---------------------------------------------------------------------
// Trace commun aux trois series.
//
// data     : pointeur sur le debut de la fenetre (voir windowStart)
// points   : nombre de points de la fenetre (windowPoints)
// noValue  : valeur marquant une case vide
// scale / offset : valeur reelle = data[i] * scale + offset
// dropValue: chute (dans l'unite reelle) a partir de laquelle le segment
//            passe en rouge ; 0 ou moins = pas de surbrillance
//---------------------------------------------------------------------

static void drawHistoryPage(const char *title,
                            const int16_t *data,
                            int16_t points,
                            int16_t noValue,
                            float   scale,
                            float   offset,
                            float   minVal,
                            float   maxVal,
                            float   gridStep,
                            float   labelStep,
                            float   dropValue,
                            float   refLine,
                            int16_t stepMin,
                            DateTime& now)
{
  //-------------------------------------------------------------------
  // Fond et bandeau de titre
  //-------------------------------------------------------------------

  s_tft->fillRect(0, 0, FULL_W, FULL_H, TFT_WHITE);

  s_tft->fillRectVGradient(0, 0, FULL_W, FULL_TITLE_H, TFT_LIGHTGREY, TFT_DARKGREY);
  s_tft->drawRect(0, 0, FULL_W - 1, FULL_TITLE_H - 1, TFT_BLACK);

  // Le canvas est partage avec le menu, qui dessine son texte en taille 2
  // (dernier argument de initGUI) et laisse cette taille memorisee. Les
  // polices libres sont multipliees par elle : sans ce rappel, tout le
  // texte de cette page serait deux fois trop gros apres un passage par le
  // menu.
  s_tft->setTextSize(1);

  s_tft->setFreeFont(&FreeSansBold18pt7b);
  s_tft->setTextColor(TFT_BLACK);
  s_tft->setTextDatum(TC_DATUM);
  s_tft->drawString(title, FULL_W / 2 - 1, 8);
  s_tft->drawString(title, FULL_W / 2,     8);
  s_tft->drawString(title, FULL_W / 2 + 1, 8);

  // Rappel : un appui sur l'ecran revient a la page de mesure
  s_tft->setFreeFont(&FreeSansBold9pt7b);
  s_tft->setTextDatum(TR_DATUM);
  s_tft->drawString("tap: back", FULL_W - 10, 20);

  //-------------------------------------------------------------------
  // Grille horizontale et libelles de valeurs
  //-------------------------------------------------------------------

  s_tft->setFreeFont(&FreeSansBold12pt7b);

  int16_t stepsPerLabel = (int16_t)lroundf(labelStep / gridStep);
  int16_t n = 0;

  if (stepsPerLabel < 1) stepsPerLabel = 1;

  for (float v = minVal; v <= maxVal + 0.001f; v += gridStep, n++) {
    int16_t y = FULL_PLOT_Y + (int16_t)lroundf((maxVal - v) * FULL_PLOT_H / (maxVal - minVal));

    if (n % stepsPerLabel == 0) {
      s_tft->drawLine(FULL_PLOT_X, y, FULL_PLOT_X + FULL_PLOT_W, y, TFT_DARKGREY);

      char buf[10];
      snprintf(buf, sizeof(buf), "%d", (int)lroundf(v));

      s_tft->setTextColor(TFT_DARKGREY);
      s_tft->setTextDatum(CR_DATUM);
      s_tft->drawString(buf, FULL_PLOT_X - 8, y);
    } else
      s_tft->drawLine(FULL_PLOT_X, y, FULL_PLOT_X + FULL_PLOT_W, y, TFT_LIGHTGREY);
  }

  // Ligne de reference (pression standard 1013 hPa) : en vert, si elle est
  // dans la fenetre affichee.
  if (!isnan(refLine) && refLine >= minVal && refLine <= maxVal) {
    int16_t y = FULL_PLOT_Y + (int16_t)lroundf((maxVal - refLine) * FULL_PLOT_H / (maxVal - minVal));

    s_tft->drawWideLine(FULL_PLOT_X, y, FULL_PLOT_X + FULL_PLOT_W, y, 2, TFT_GREEN, TFT_GREEN);
  }

  // Verticales regulieres : FULL_LABELS reperes repartis sur l'axe
  for (int8_t k = 0; k < FULL_LABELS; k++) {
    int16_t idx = (int16_t)lroundf(k * (points - 1) / (float)(FULL_LABELS - 1));
    int16_t x   = FULL_PLOT_X + (int32_t)idx * FULL_PLOT_W / (points - 1);

    s_tft->drawLine(x, FULL_PLOT_Y, x, FULL_PLOT_Y + FULL_PLOT_H, TFT_LIGHTGREY);
  }

  s_tft->drawRect(FULL_PLOT_X, FULL_PLOT_Y, FULL_PLOT_W + 1, FULL_PLOT_H + 1, TFT_BLACK);

  //-------------------------------------------------------------------
  // Courbe
  //-------------------------------------------------------------------

  int16_t prevX    = 0;
  int16_t prevY    = 0;
  float   prevV    = 0.0f;
  bool    havePrev = false;

  for (int16_t i = 0; i < points; i++) {
    if (data[i] == noValue) {
      havePrev = false;
      continue;
    }

    float   v = data[i] * scale + offset;
    int16_t x = FULL_PLOT_X + (int32_t)i * FULL_PLOT_W / (points - 1);

    // Une valeur hors echelle (par exemple une temperature sous -20 degres
    // ou au-dessus de 40) est plaquee sur le bord du cadre plutot que de
    // sortir du graphique.
    if (v < minVal) v = minVal;
    if (v > maxVal) v = maxVal;

    int16_t y = FULL_PLOT_Y + (int16_t)lroundf((maxVal - v) * FULL_PLOT_H / (maxVal - minVal));

    uint32_t color = TFT_NAVY;

    if (havePrev && dropValue > 0.0f && (prevV - v) >= dropValue)
      color = TFT_RED;

    if (havePrev)
      s_tft->drawWideLine(prevX, prevY, x, y, 2, color, color);

    s_tft->fillCircle(x, y, 3, color);

    prevX    = x;
    prevY    = y;
    prevV    = v;
    havePrev = true;
  }

  //-------------------------------------------------------------------
  // Etiquettes d'heure : du plus ancien (bord gauche) a maintenant (bord
  // droit). L'axe couvre ainsi toute la duree de l'historique. Sous chaque
  // heure, le jour relatif (j = aujourd'hui, j-1 = hier, ...).
  //-------------------------------------------------------------------

  s_tft->setFreeFont(&FreeSansBold9pt7b);
  s_tft->setTextColor(TFT_BLACK);
  s_tft->setTextDatum(TC_DATUM);

  uint32_t nowsec = now.unixtime();
  int32_t  tod    = (int32_t)now.hour() * 3600 + (int32_t)now.minute() * 60 + now.second();

  for (int8_t k = 0; k < FULL_LABELS; k++) {
    int16_t  idx  = (int16_t)lroundf(k * (points - 1) / (float)(FULL_LABELS - 1));
    uint32_t back = (uint32_t)(points - 1 - idx) * (uint32_t)stepMin * 60UL;
    int16_t  x    = FULL_PLOT_X + (int32_t)idx * FULL_PLOT_W / (points - 1);
    DateTime stamp(nowsec - back);

    char buf[10];
    snprintf(buf, sizeof(buf), "%02d:%02d", stamp.hour(), stamp.minute());
    s_tft->drawString(buf, x, FULL_XLABEL_Y);

    // Jour relatif : j, j-1, j-2... (division entiere arrondie vers -inf)
    int64_t s = (int64_t)tod - (int64_t)back;
    int64_t d = s / 86400;
    if (s < 0 && s % 86400 != 0) d -= 1;

    char day[8];
    if (d == 0) strcpy(day, "j");
    else snprintf(day, sizeof(day), "j%d", (int)d);

    s_tft->setTextColor(TFT_DARKGREY);
    s_tft->drawString(day, x, FULL_XLABEL_Y + 14);
    s_tft->setTextColor(TFT_BLACK);
  }
}

//---------------------------------------------------------------------

void drawFullHistoryPage(HistSeriesKind kind)
{
  char     title[40];
  char     span[8];
  int16_t  stepMin = historyStepMinutes();
  int16_t  points  = windowPoints(history_mode);
  int16_t  start   = windowStart(history_mode);
  DateTime now = last_update;

  snprintf(span, sizeof(span), "%dh", (int)history_mode);

  switch (kind) {
    //-----------------------------------------------------------------
    // Humidite : echelle fixe 0..100 %HR, une ligne tous les 10 %,
    // un libelle tous les 20 %.
    //-----------------------------------------------------------------
    case HIST_HUMIDITY:
      snprintf(title, sizeof(title), "Humidity hist.: %s", span);
      drawHistoryPage(title, &hum_data[start], points, NOPRESSHUM,
                      1.0f, 0.0f, 0.0f, 100.0f, 10.0f, 20.0f, -1.0f, NAN,
                      stepMin, now);
      break;

    //-----------------------------------------------------------------
    // Temperature : echelle fixe, celle du cadran (-20..40 degres C,
    // soit -4..104 degres F), avec une ligne tous les 5 degres C.
    //-----------------------------------------------------------------
    case HIST_TEMPERATURE: {
      float scale, offset, lo, hi, grid, label;

      if (!bfahrenheit) {
        scale  = 0.1f;               // serie stockee en 0,1 degre C
        offset = 0.0f;
        lo     = -20.0f;
        hi     = 40.0f;
        grid   = 5.0f;
        label  = 10.0f;
      } else {
        // 0,1 degre C -> degre F : x * 0,1 * 9/5 + 32 = x * 0,18 + 32
        scale  = 0.18f;
        offset = 32.0f;
        lo     = -4.0f;
        hi     = 104.0f;
        grid   = 9.0f;               // 9 degres F = 5 degres C
        label  = 18.0f;              // 18 degres F = 10 degres C
      }

      snprintf(title, sizeof(title), "Temperature hist.: %s", span);
      drawHistoryPage(title, &temp_data[start], points, NOTEMP,
                      scale, offset, lo, hi, grid, label, -1.0f, NAN, stepMin, now);
      break;
    }

    //-----------------------------------------------------------------
    // Pression : meme echelle que le graphique de la page de mesure
    // (50 hPa, par pas de 10) et meme surbrillance des chutes.
    //-----------------------------------------------------------------
    case HIST_PRESSURE: {
      float drop;

      switch (history_mode) {
        case MODE_96H: drop = DROPPRESSURE_96H; break;
        case MODE_48H: drop = DROPPRESSURE_48H; break;
        case MODE_24H: drop = DROPPRESSURE_24H; break;
        case MODE_12H: drop = DROPPRESSURE_12H; break;
        default:       drop = DROPPRESSURE_06H; break;
      }

      mapPressureHistValues();       // met a jour min_pressure / max_pressure

      snprintf(title, sizeof(title), "Atm. Pres. Hist.: %s", span);
      drawHistoryPage(title, &pressure_data[start], points, NOPRESSHUM,
                      1.0f, 0.0f, (float)min_pressure, (float)max_pressure,
                      5.0f, 10.0f, drop, 1013.0f, stepMin, now);
      break;
    }
  }
}

