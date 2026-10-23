
#include <stdint.h>
#include <math.h>
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
#define HIST_STEP      15                                  // px entre deux points
#define HIST_TOP       20                                  // y de max_pressure
#define HIST_PPMB       6                                  // pixels par mbar
#define HIST_SPAN      50                                  // plage 1030 -> 980 hPa
#define HIST_LABELS    (HIST_SPAN / 10 + 1)                // 6 etiquettes
#define HIST_RIGHT   (HIST_LEFT + HIST_STEP * (MAXHOURTIMESLOT - 1))
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

int16_t meter_data[MAXHOURTIMESLOT];

// Historique de l'humidite (%HR), de la temperature (0,1 degre C) et de la pression
//
// Une mesure toutes les 15 min dans la serie 6h
// toutes les 30 min dans la serie 12h
// toutes les heures dans la serie 24h
// toutes les 4 h dans la serie 96h (voir saveHistoricalValues).
//
// Les 3 series sont indexees par modeIndex() : 6h, 12h, 24h, 96h.
// NOTEMP marque les cases encore vides (une temperature de -0,1 degre est
// une valeur valide, d'ou un sentinelle different de -1).

static int16_t hum_data     [4][MAXHOURTIMESLOT];
static int16_t temp_data    [4][MAXHOURTIMESLOT];
static int16_t pressure_data[4][MAXHOURTIMESLOT];

TFT_eSprite *graph;
TFT_eSprite *misc;    // bandeau de titre
TFT_eSprite *info;    // bandeau date / heure / pression

// Ecran de destination (canvas TFT_eSprite de la classe Screen)
static TFT_eSPI *s_tft = nullptr;

DateTime last_update[4];

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

  // Series vides au demarrage : aucune mesure avant la premiere lecture
  for (int8_t m = 0; m < 4; m++) {
    last_update[m] = rtcClockNow();
    for (int8_t i = 0; i < MAXHOURTIMESLOT; i++) {
      pressure_data[m][i]  = NOPRESSHUM;
      hum_data[m][i]       = NOPRESSHUM;
      temp_data[m][i]      = NOTEMP;
    }
  }
}

//====================================================
// Index de la serie correspondant a un mode d'historique
//====================================================

static int8_t modeIndex(int8_t mode)
{
  switch (mode) {
    case MODE_96H: return 3;
    case MODE_24H: return 2;
    case MODE_12H: return 1;
    default:       return 0;
  }
}

//====================================================
// Acces aux series : la pression est stockee en hPa,
// l'humidite en %HR et la temperature en 0,1 degre C.
//====================================================

static int16_t *pressureSeries(int8_t mode)
{
  return pressure_data[modeIndex(mode)];
}

static int16_t *humiditySeries(int8_t mode)
{
  return hum_data[modeIndex(mode)];
}

static int16_t *temperatureSeries(int8_t mode)
{
  return temp_data[modeIndex(mode)];
}

//====================================================
// Acces en lecture pour l'interface web
//====================================================

static int8_t hoursIndex(int8_t hours)
{
  switch (hours) {
    case MODE_12H: return 1;
    case MODE_24H: return 2;
    case MODE_96H: return 3;
    default:       return 0;
  }
}

int16_t barometerHistCount()
{
  return MAXHOURTIMESLOT;
}

int16_t barometerHistStep(int8_t hours)
{
  switch (hours) {
    case MODE_96H: return 240;
    case MODE_24H: return  60;
    case MODE_12H: return  30;
    default:       return  15;
  }
}

uint32_t barometerHistStamp(int8_t hours)
{
  return last_update[hoursIndex(hours)].unixtime();
}

// Heure locale de la derniere mesure, en secondes depuis minuit : evite
// toute conversion de fuseau horaire dans la page web.
int32_t barometerHistTimeOfDay(int8_t hours)
{
  DateTime t = last_update[hoursIndex(hours)];

  return (int32_t)t.hour() * 3600 + (int32_t)t.minute() * 60 + t.second();
}

// Pression en hPa, humidite en %HR, temperature en degres C.
float barometerHistValue(HistSeriesKind kind, int8_t hours, int16_t i)
{
  if (i < 0 || i >= MAXHOURTIMESLOT)
    return NAN;

  int8_t m = hoursIndex(hours);

  switch (kind) {
    case HIST_HUMIDITY:
      return (hum_data[m][i] == NOPRESSHUM) ? NAN : (float)hum_data[m][i];

    case HIST_TEMPERATURE:
      return (temp_data[m][i] == NOTEMP) ? NAN : (float)temp_data[m][i] / 10.0f;

    default:
      return (pressure_data[m][i] == NOPRESSHUM) ? NAN : (float)pressure_data[m][i];
  }
}

//====================================================
// Decale une serie d'un cran et place la nouvelle
// mesure en derniere position (la plus recente).
//====================================================

static void shiftSeries(int16_t *data, int16_t value)
{
  for (int8_t i = 0; i < MAXHOURTIMESLOT - 1; i++)
    data[i] = data[i + 1];

  data[MAXHOURTIMESLOT - 1] = value;
}

//====================================================
// pushSample: ajoute une mesure aux trois series d'un
// mode. La pression, l'humidite et la temperature
// partagent ainsi exactement le meme axe des temps.
//====================================================

static void pushSample(int8_t mode, float pres, float temp, int16_t hum)
{
  shiftSeries(pressureSeries(mode),    (int16_t)lroundf(pres));
  shiftSeries(humiditySeries(mode),    hum);
  shiftSeries(temperatureSeries(mode), (int16_t)lroundf(temp * 10.0f));

  last_update[modeIndex(mode)] = rtcClockNow();
}


//====================================================
// saveHistoricalValues: ajoute la mesure courante aux
// trois series (pression, humidite, temperature).
//
// Une mesure toutes les 15 min alimente la serie 6h ; la serie 12h est
// decalee une fois sur deux, la 24h toutes les heures et la 96h toutes
// les 4 h : les quatre series couvrent donc bien 6, 12, 24 et 96 heures.
//====================================================

void saveHistoricalValues(float pres, float temp, int16_t hum)
{
  static unsigned long  last_time = millis();
  static bool           first_time  = true;
  static int8_t         quarter     = 0;
  static int8_t         hour        = 0;
  
  if (millis() - last_time > SAVE_DATA_DELAY || first_time) {
    pushSample(MODE_06H, pres, temp, hum);

    if (quarter == 2) {
      pushSample(MODE_12H, pres, temp, hum);
    } else if (quarter == 0) {
      if (hour == 0) {
        pushSample(MODE_96H, pres, temp, hum);
      }
      pushSample(MODE_12H, pres, temp, hum);
      pushSample(MODE_24H, pres, temp, hum);
      hour = ++hour % 4;
    }

    quarter = ++quarter % 4;
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
// mesure (2 hPa de marge en haut et en bas). L'etat est conserve par
// duree (6/12/24/96 h) : l'ecran et le web voient donc exactement la meme
// echelle pour une serie donnee.
//====================================================

static int16_t s_scaleMax[4] = {MAXPRESSURE, MAXPRESSURE, MAXPRESSURE, MAXPRESSURE};
static int16_t s_scaleMin[4] = {MINPRESSURE, MINPRESSURE, MINPRESSURE, MINPRESSURE};

void barometerHistPressureScale(int8_t hours, int16_t *lo, int16_t *hi)
{
  int8_t   idx  = hoursIndex(hours);
  int16_t  last = pressure_data[idx][MAXHOURTIMESLOT - 1];
  int16_t  maxp = s_scaleMax[idx];
  int16_t  minp = s_scaleMin[idx];

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

  s_scaleMax[idx] = maxp;
  s_scaleMin[idx] = minp;

  if (lo) *lo = minp;
  if (hi) *hi = maxp;
}

//====================================================
// mapPressureHistValues: Map actual pressure values in the
// a pressure range [1003,1023] 'mbar' to fit scale.
//====================================================

void mapPressureHistValues() {
  int16_t *pressure_data = pressureSeries(history_mode);

  barometerHistPressureScale(history_mode, &min_pressure, &max_pressure);
  mid_pressure = (max_pressure + min_pressure) / 2;

  for (int i = 0; i < HIST_LABELS; i++) {
    sprintf(label[i], "%04d", (max_pressure - 10 * i));
  }
  normal_pressure_y = HIST_TOP + (max_pressure - 1013) * HIST_PPMB;

  for (int8_t i = 0; i < MAXHOURTIMESLOT; i++) {
    int16_t pressure = pressure_data[i];
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

  // Lignes verticales : une tous les deux points
  for (int x = HIST_LEFT; x <= HIST_RIGHT; x += 2 * HIST_STEP)
    graph->drawLine(x, HIST_TOP - 10, x, HIST_BOTTOM, TFT_BLACK);
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
    case MODE_96H:
      drop_pressure = DROPPRESSURE_96H;
      title = "Atm. Pres. Hist.: 96h";
      break;
  }

  mapPressureHistValues();
  drawPressureHistBg();

  for (int i = 0; i < MAXHOURTIMESLOT; i++) {
    int16_t x = HIST_LEFT + HIST_STEP * i;

    if (meter_data[i] == NOPRESSHUM)
      continue;

    uint32_t color = TFT_NAVY;

    if (i > 0 && meter_data[i - 1] != NOPRESSHUM) {
      if (meter_data[i - 1] - meter_data[i] >= drop_pressure * HIST_PPMB)
        color = TFT_RED;

      graph->drawLine(x - HIST_STEP, meter_data[i - 1], x, meter_data[i], color);
    }

    graph->fillCircle(x, meter_data[i], 6, color);
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
// gauche, courbe et, en bas, l'heure des mesures (une etiquette toutes les
// cinq mesures). Le pas de temps entre deux points est celui du mode
// choisi dans le menu, commun aux trois series.
//---------------------------------------------------------------------

#define FULL_W          800
#define FULL_H          480
#define FULL_TITLE_H     52
#define FULL_PLOT_X      92
#define FULL_PLOT_Y      84
#define FULL_PLOT_W     672
#define FULL_PLOT_H     310
#define FULL_POINT_STEP (FULL_PLOT_W / (MAXHOURTIMESLOT - 1))   // 28 px
#define FULL_XLABEL_Y   (FULL_PLOT_Y + FULL_PLOT_H + 10)

// Une verticale et une etiquette d'heure tous les 6 points : les 25 points
// donnent donc 5 reperes, du plus ancien (bord gauche) a maintenant (bord
// droit), et l'axe couvre exactement la duree choisie (6, 12, 24 ou 96 h).
#define FULL_LABEL_STEP  6

extern bool bfahrenheit;

//---------------------------------------------------------------------
// Pas de temps, en minutes, entre deux mesures du mode courant
//---------------------------------------------------------------------

static int16_t historyStepMinutes()
{
  return barometerHistStep(history_mode);
}

//---------------------------------------------------------------------
// Trace commun aux trois series.
//
// data     : serie du mode courant
// noValue  : valeur marquant une case vide
// scale / offset : valeur reelle = data[i] * scale + offset
// dropValue: chute (dans l'unite reelle) a partir de laquelle le segment
//            passe en rouge ; 0 ou moins = pas de surbrillance
//---------------------------------------------------------------------

static void drawHistoryPage(const char *title,
                            const int16_t *data,
                            int16_t noValue,
                            float   scale,
                            float   offset,
                            float   minVal,
                            float   maxVal,
                            float   gridStep,
                            float   labelStep,
                            float   dropValue,
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

  // Une verticale et une etiquette d'heure tous les 6 points
  for (int8_t i = 0; i < MAXHOURTIMESLOT; i += FULL_LABEL_STEP) {
    int16_t x = FULL_PLOT_X + i * FULL_POINT_STEP;

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

  for (int8_t i = 0; i < MAXHOURTIMESLOT; i++) {
    if (data[i] == noValue) {
      havePrev = false;
      continue;
    }

    float   v = data[i] * scale + offset;
    int16_t x = FULL_PLOT_X + i * FULL_POINT_STEP;

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

    s_tft->fillCircle(x, y, 5, color);

    prevX    = x;
    prevY    = y;
    prevV    = v;
    havePrev = true;
  }

  //-------------------------------------------------------------------
  // Etiquettes d'heure : du plus ancien (bord gauche) a maintenant (bord
  // droit). L'axe couvre ainsi toute la duree de l'historique.
  //-------------------------------------------------------------------

  s_tft->setFreeFont(&FreeSansBold9pt7b);
  s_tft->setTextColor(TFT_BLACK);
  s_tft->setTextDatum(TC_DATUM);

  uint32_t nowsec = now.unixtime();

  for (int8_t i = 0; i < MAXHOURTIMESLOT; i += FULL_LABEL_STEP) {
    uint32_t back = (uint32_t)(MAXHOURTIMESLOT - 1 - i) * (uint32_t)stepMin * 60UL;
    DateTime stamp(nowsec - back);

    char buf[10];
    snprintf(buf, sizeof(buf), "%02d:%02d", stamp.hour(), stamp.minute());

    s_tft->drawString(buf, FULL_PLOT_X + i * FULL_POINT_STEP, FULL_XLABEL_Y);
  }
}

//---------------------------------------------------------------------

void drawFullHistoryPage(HistSeriesKind kind)
{
  char     title[40];
  char     span[8];
  int16_t  stepMin = historyStepMinutes();
  DateTime now = last_update[modeIndex(history_mode)];

  snprintf(span, sizeof(span), "%dh", (int)history_mode);

  switch (kind) {
    //-----------------------------------------------------------------
    // Humidite : echelle fixe 0..100 %HR, une ligne tous les 10 %,
    // un libelle tous les 20 %.
    //-----------------------------------------------------------------
    case HIST_HUMIDITY:
      snprintf(title, sizeof(title), "Humidity hist.: %s", span);
      drawHistoryPage(title, humiditySeries(history_mode), NOPRESSHUM,
                      1.0f, 0.0f, 0.0f, 100.0f, 10.0f, 20.0f, -1.0f,
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
      drawHistoryPage(title, temperatureSeries(history_mode), NOTEMP,
                      scale, offset, lo, hi, grid, label, -1.0f, stepMin, now);
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
        case MODE_24H: drop = DROPPRESSURE_24H; break;
        case MODE_12H: drop = DROPPRESSURE_12H; break;
        default:       drop = DROPPRESSURE_06H; break;
      }

      mapPressureHistValues();       // met a jour min_pressure / max_pressure

      snprintf(title, sizeof(title), "Atm. Pres. Hist.: %s", span);
      drawHistoryPage(title, pressureSeries(history_mode), NOPRESSHUM,
                      1.0f, 0.0f, (float)min_pressure, (float)max_pressure,
                      5.0f, 10.0f, drop, stepMin, now);
      break;
    }
  }
}

