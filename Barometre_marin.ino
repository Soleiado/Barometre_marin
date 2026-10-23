//===========================================
// Includes
//===========================================

#define USE_ESP_IDF_LOG

#include <arduino.h>
#include <stdint.h>
#include <SPI.h>
#include "screen.h"        // Dalle RGB 800x480, tactile GT911 et canvas TFT_eSPI
#include "rtc_clock.h"     // DS3231 / PCF85063A / horloge logicielle
#include "bme280_local.h"  // BME280 sur le bus I2C partage de la carte
#include "meter.h"
#include "barometer.h"
#include "menu.h"
#include "Definition.h"
#include "nmea.h"
#include "webui.h"         // interface web (WiFi) : mesures + historique

//===========================================
// Global instantiation

//===========================================

Screen          tft;
Menu            menu1(&tft,  1, 6);
Menu            menu11(&tft,11, 3);
Menu            menu12(&tft,12, 5);
Menu            menu13(&tft,13, 6);
Form            form1(&tft, 14);
GUI             *gui = &menu1;       // Default menu

Meter           humidityMeter = Meter(&tft);
Meter           tempMeter = Meter(&tft);

DateTime        now;
BME280_Class    BME280;
bool            bme280_ok = false;

// Le dessin se fait dans le canvas hors ecran : le contenu n'arrive sur la
// dalle que lorsque ce drapeau (ou l'equivalent local a loop()) est leve.
// Les touches de menu agissent au RELACHEMENT du bouton (voir Menu::checkTouch) :
// sans ce drapeau, un sous-menu ouvert sur relachement restait dans le canvas
// et n'apparaissait qu'a la touche suivante ("il faut toucher deux fois").
bool            guiNeedsFlush = false;

//===========================================
// function definition
//===========================================

void touch_calibrate(TFT_eSPI &tft);

//===========================================
// Zones de touche de la page de mesure
//
// Chaque widget de la page de mesure occupe une zone rectangulaire :
//   - le bandeau gris du bas a droite ouvre le menu ;
//   - les deux cadrans ouvrent l'historique plein ecran de l'humidite et
//     de la temperature ;
//   - le graphique de droite ouvre l'historique plein ecran de la
//     pression.
// Les coordonnees sont celles des widgets : elles viennent de meter.h
// (METER_W / METER_H) et de barometer.h (INFO_BAND_*, GRAPH_BAND_*), donc
// un deplacement d'un widget deplace sa zone automatiquement.
//===========================================

#define HUM_METER_X     8            // origine du cadran humidite
#define HUM_METER_Y     8
#define TEMP_METER_X    8            // origine du cadran temperature
#define TEMP_METER_Y  250

static bool inZone(uint16_t x, uint16_t y, int16_t zx, int16_t zy, int16_t zw, int16_t zh)
{
  return (x >= zx) && (x < zx + zw) && (y >= zy) && (y < zy + zh);
}

static bool inMenuZone(uint16_t x, uint16_t y)
{
  return inZone(x, y, INFO_BAND_X, INFO_BAND_Y, INFO_BAND_W, INFO_BAND_H);
}

static bool inHumidityZone(uint16_t x, uint16_t y)
{
  return inZone(x, y, HUM_METER_X, HUM_METER_Y, METER_W, METER_H);
}

static bool inTemperatureZone(uint16_t x, uint16_t y)
{
  return inZone(x, y, TEMP_METER_X, TEMP_METER_Y, METER_W, METER_H);
}

static bool inPressureZone(uint16_t x, uint16_t y)
{
  return inZone(x, y, GRAPH_BAND_X, GRAPH_BAND_Y, GRAPH_BAND_W, GRAPH_BAND_H);
}

// Page d'historique correspondant a une page donnee
static HistSeriesKind historyKindOf(int16_t p)
{
  switch (p) {
    case PAGE_HUM_HIST:  return HIST_HUMIDITY;
    case PAGE_TEMP_HIST: return HIST_TEMPERATURE;
    default:             return HIST_PRESSURE;
  }
}

//===========================================
// Global variables
//===========================================

int32_t         temperature  = 0;
int32_t         hum          = 0;
int32_t         pressure     = 0;
int32_t         gas          = 0;
float           pres         = 0.0;
float           temp         = 0.0;
float           tempC        = 0.0;      // temperature en degres C, quelle que soit
                                         // l'unite d'affichage (temp passe en degres F
                                         // a chaque dessin de la page de mesure)
int16_t         humidity     = 0;
bool            bfahrenheit  = false;
int32_t         speed_nmea   = 0;
int             page         = PAGE_DATA;
int             old_page     = PAGE_SCREEN_SAVER;

String          label1 [6] = {"Temp. Unit", "Hist. Range", "NMEA Config.", "Date & Time", "Restart Screen", "Exit"};
String          label11[3] = {"Celsuis", "Fahr.", "Exit"};
String          label12[5] = {"hist. 6h", "hist. 12h", "hist. 24h", "hist. 96h", "Exit"};
String          label13[6] = {"N2K only", "4800b", "9600b", "19200b", "38400b", "Exit"};

extern int8_t   history_mode;

// Redessin demande par l'interface web (unite ou heure modifiee)
volatile bool   uiRefresh    = false;

#define HUMIDITY_CORRECTION(x)  ((int8_t)((float)x*43.0/3100.0));

//------------------------------------------------------------------------

void drawMainPage(int16_t humidity, float temp, float pres)
 {
  char      bupres[15];  // sprintf text buffer
  char      butemp[15];  // sprintf text buffer
  static    float old_pres = 0.0;

  now = rtcClockNow();

  humidityMeter.setValue(humidity);
  sprintf(bupres, "%d%%", humidity);  // Indicating now-value is outside MAXPRESSURE range
  humidityMeter.setLabel2(bupres);

  if (!bfahrenheit) {
    tempMeter.setRange(-20, 40, 20);
    tempMeter.setZone(-20, 5, TFT_BLUE);
    tempMeter.setZone(5, 25, TFT_WHITE);
    tempMeter.setZone(25, 40, TFT_RED);
    tempMeter.setValue(temp);
    sprintf(bupres, "%.1fC", temp);  // Indicating now-value is outside MAXPRESSURE range
    tempMeter.setLabel2(bupres);
  } else {
    tempMeter.setRange(-4, 104, 20);
    tempMeter.setZone(-4, 41, TFT_BLUE);
    tempMeter.setZone(41, 77, TFT_WHITE);
    tempMeter.setZone(77, 104, TFT_RED);
    temp = temp * 9.0/5.0 + 32;
    tempMeter.setValue(temp);
    sprintf(bupres, "%.1fF", temp);  // Indicating now-value is outside MAXPRESSURE range
    tempMeter.setLabel2(bupres);
  }

  humidityMeter.drawMeter(  8,  8);
  tempMeter.drawMeter    (  8,250);

  drawPressureHist(pres, now);
}

//------------------------------------------------------------------------

void setNmeaMenuLabel(int16_t button)
{
  for (int i = 0; i < 7; i++)
    menu13.setButtonLabel(i, label13[i]);

  button = button - 131;
  menu13.setButtonLabel(button, "*"+label13[button]+"*");
}

//------------------------------------------------------------------------
// Reglage modifie depuis l'interface web : on met a jour le repere du menu
// tactile (l'etoile marque le debit NMEA choisi) et on redessine la page en
// cours au prochain tour de loop().
//------------------------------------------------------------------------

void uiSettingsChanged()
{
  static const int32_t speeds[5] = { 0, 4800, 9600, 19200, 38400 };

  for (uint8_t i = 0; i < 5; i++) {
    if (speed_nmea == speeds[i]) {
      setNmeaMenuLabel(131 + i);
      break;
    }
  }

  uiRefresh = true;
}

//------------------------------------------------------------------------

int manageMenu(int page, int16_t t_x, int16_t t_y)
{
  int ret = page;
  int16_t button = gui->checkTouch(t_x, t_y);

  // Un bouton relache = bouton redessine et, souvent, changement de page ou de
  // sous-menu : il faut recopier le canvas dans le framebuffer.
  if (button)
    guiNeedsFlush = true;

  switch (button) {
    //-------------------------------
    // Option : "EXIT"
    //-------------------------------
    case 16:
    case 113:
    case 125:
    case 136:
      ret = PAGE_DATA;
      break;   
    //-------------------------------
    // Option : "Restart Screen"
    //
    // Meme operation que /api/panel?display=1 : la dalle est reinitialisee
    // sans toucher aux mesures ni a l'historique.
    //-------------------------------
    case 15:
      gui = &menu1;
      Screen::reinit();
      ret = PAGE_DATA;
      break;
    //-------------------------------
    // Sub menu : "Temp. Unit"
    //-------------------------------
    case 11:
      gui = &menu11;
      gui->drawGUI();
      break;          
    case 111:
      bfahrenheit = false;
      ret = PAGE_DATA;
      break;          
    case 112:
      bfahrenheit = true;
      ret = PAGE_DATA;
      break;
    //-------------------------------
    // Sub menu : "Press. Hist."          
    //-------------------------------
    case 12:
      gui = &menu12;
      gui->drawGUI();
      break;          
    case 121:
      history_mode = MODE_06H;
      ret = PAGE_DATA;
      break;          
    case 122:
      history_mode = MODE_12H;
      ret = PAGE_DATA;
      break;          
    case 123:
      history_mode = MODE_24H;
      ret = PAGE_DATA;
      break;          
    case 124:
      history_mode = MODE_96H;
      ret = PAGE_DATA;
      break;          
    //-------------------------------
    // Sub Menu : "NMEA Config"
    //-------------------------------
    case 13:
      gui = &menu13;
      gui->drawGUI();
      break;          
    case 131:
      setNmeaMenuLabel(button);
      speed_nmea = 0;
      ret = PAGE_DATA;
      break;
    case 132:
      setNmeaMenuLabel(button);
      speed_nmea = 4800;
      ret = PAGE_DATA;
      break;          
    case 133:
      setNmeaMenuLabel(button);
      speed_nmea = 9600;
      ret = PAGE_DATA;
      break;          
    case 134:
      setNmeaMenuLabel(button);
      speed_nmea = 19200;
      ret = PAGE_DATA;
      break;          
    case 135:
      setNmeaMenuLabel(button);
      speed_nmea = 38400;
      ret = PAGE_DATA;
      break;
    //-------------------------------  
    // Sub menu : "Date & Time"
    //-------------------------------
    case 14:
      form1.setMessage("");
      gui = &form1;
      now = rtcClockNow();
      form1.setYear(now.year());
      form1.setMonth(now.month());
      form1.setDay(now.day());
      form1.setHour(now.hour());
      form1.setMin(now.minute());
      gui->drawGUI();
      break;
    case 144:
      now = DateTime(form1.getYear(), form1.getMonth(), form1.getDay(), form1.getHour(), form1.getMin(), 0);
      if (now.isValid()) {
        form1.setMessage("");
        rtcClockAdjust(now);
        ret = PAGE_DATA;
      } else {
        form1.setMessage("Invalid date");
        gui->drawGUI();
      }
      break;
    case 141:
      form1.setMessage("");
      ret = PAGE_DATA;
      break;          
    case 142:
      form1.setMessage("");
      form1.decFocusValue();
      gui->drawGUI();
      break;          
    case 143:
      form1.setMessage("");
      form1.incFocusValue();
      gui->drawGUI();
      break;          
  }

  if (ret == PAGE_DATA) 
    gui = &menu1;

  return ret;
}

//------------------------------------------------------------------------

void backLight(bool onOff)
{
  // Idempotent : un appui maintenu sur l'ecran enchaine plusieurs changements
  // de page, donc plusieurs appels avec le meme etat. Sans ce test, chaque
  // appel relancait trois transactions I2C sur le CH422G (WR_SET, WR_IO puis
  // relecture RD_IO) pour rien.
  if (tft.backlightIsOn() == onOff)
    return;

  tft.setBacklight(onOff);

  if (!onOff) {
    gui = &menu1;
    tft.fillRectVGradient(0,0,UI_W,UI_H, TFT_YELLOW,  TFT_NAVY);
    drawMainPage(humidity, temp, pres);
    tft.flush();
  }
}

//------------------------------------------------------------------------

void getSensorData()
{
  if (bme280_ok) {
    BME280.getSensorData(temperature, hum, pressure);  // Get real data, ignore gas value

    pres     = pressure / 100.0;
    temp     = (float) temperature / 100.0;
    tempC    = temp;
    humidity = HUMIDITY_CORRECTION(hum);
  }

  saveHistoricalValues(pres, temp, humidity);
}

//#########################################################################
//######                           SETUP                             ######
//#########################################################################

void setup(void)
{
  Serial.begin(115200);

  // Ne pas bloquer le demarrage si aucun moniteur serie n'est connecte
  uint32_t serialWait = millis();
  while (!Serial && millis() - serialWait < 3000) delay(10);

  Serial.println();
  Serial.println("Barometre marin - Waveshare ESP32-S3-Touch-LCD-4.3B");
  Serial.flush();

  //-- Reglages enregistres depuis la page web (unite de temperature,
  //   sortie NMEA) : charges avant le premier dessin de l'ecran.
  webSettingsLoad();
  uiSettingsChanged();

  //-- NMEA 2000 : ouvre le controleur CAN et s'annonce sur le bus. Le bus
  //   etant dedie, l'emission des PGN est ensuite toujours active.
  nmea2KInit();

  //-- Ecran, tactile et retroeclairage
  tft.init();
  tft.setRotation(3);          // sans effet : le canvas est toujours dans le bon sens
  touch_calibrate(tft);        // GT911 : pas de calibration necessaire

  //-- Horloge : DS3231 externe, PCF85063A de la carte, sinon horloge logicielle
  rtcClockInit();

  //-- BME280 : facultatif, l'interface doit demarrer meme sans capteur
  for (int attempt = 0; attempt < 3 && !bme280_ok; attempt++) {
    bme280_ok = BME280.begin(I2C_FAST_MODE);
    if (!bme280_ok) {
      Serial.println(F("-  BME280 introuvable."));
      delay(500);
    }
  }

  if (bme280_ok) {
    BME280.mode(NormalMode);
    BME280.setOversampling(TemperatureSensor,Oversample16);
    BME280.setOversampling(HumiditySensor,   Oversample16);
    BME280.setOversampling(PressureSensor,   Oversample16);
    BME280.iirFilter(IIR16);
    //BME280.inactiveTime(inactive1000ms);

    delay(200);
    BME280.getSensorData(temperature, hum, pressure); // Throw away first reading, I2C/BME280 garbage

    pres     = pressure / 100.0;
    temp     = (float) temperature / 100.0;
    tempC    = temp;
    humidity = HUMIDITY_CORRECTION(hum);
  } else {
    Serial.println(F("BME280 absent : valeurs par defaut"));
  }

  tft.setFreeFont(&FreeMonoBoldOblique9pt7b);
  tft.setTextDatum(TL_DATUM);

  initBarometer(&tft);

  humidityMeter.setZone(50, 67, TFT_GREEN);
  humidityMeter.setZone(67, 84, TFT_ORANGE);
  humidityMeter.setZone(84, 100, TFT_RED);
  humidityMeter.setLabel1("Humidity");
  humidityMeter.setOrigin(HUM_METER_X, HUM_METER_Y);

  tempMeter.setLabel1("Temp.");
  tempMeter.setOrigin(TEMP_METER_X, TEMP_METER_Y);

  // Menu initialization
  // Les menus sont centres automatiquement (x et y negatifs) et leurs
  // boutons se repartissent dans la hauteur demandee.

  form1.setFrameColor(TFT_NAVY, TFT_SKYBLUE);
  form1.initGUI(120, 70, 560, 340, TFT_DARKGREY, TFT_LIGHTGREY, TFT_BLACK, 2);
  form1.setYear(2023);
  form1.setMonth(2);
  form1.setDay(1);
  
  for (int i = 0; i < 6; i++)
    menu1.setButtonLabel(i, label1[i]);
  menu1.setFrameColor(TFT_NAVY, TFT_SKYBLUE);
  menu1.initGUI(-1, -1, 380, 360, TFT_DARKGREY, TFT_LIGHTGREY, TFT_BLACK, 2);

  for (int i = 0; i < 3; i++)
    menu11.setButtonLabel(i, label11[i]);
  menu11.setFrameColor(TFT_DARKGREY, TFT_LIGHTGREY);
  menu11.initGUI(-1, -1, 380, 210, TFT_NAVY, TFT_SKYBLUE, TFT_BLACK, 2);

  for (int i = 0; i < 5; i++)
    menu12.setButtonLabel(i, label12[i]);
  menu12.setFrameColor(TFT_DARKGREY, TFT_LIGHTGREY);
  menu12.initGUI(-1, -1, 380, 300, TFT_NAVY, TFT_SKYBLUE, TFT_BLACK, 2);

  menu13.setButtonLabel(0, "*"+label13[0]+"*");
  for (int i = 1; i < 7; i++)
    menu13.setButtonLabel(i, label13[i]);
  menu13.setFrameColor(TFT_DARKGREY, TFT_LIGHTGREY);
  menu13.initGUI(-1, -1, 380, 410, TFT_NAVY, TFT_SKYBLUE, TFT_BLACK, 2);

  //-- Interface web : WiFi domestique (identifiants en NVS) ou point
  //   d'acces de secours "Barometre". Non bloquant : loop() s'occupe du reste.
  webInit();

  log_d("Free heap: %d", ESP.getFreeHeap());
}

//#########################################################################
//######                           LOOP                              ######
//#########################################################################

void loop()
{
  uint16_t t_x = 9999;
  uint16_t t_y = 9999;

  webLoop();                  // requetes HTTP (non bloquant)

  // Surveillance du balayage : si une image a ete anormalement longue (bus
  // memoire bloque par le WiFi ou par une copie d'ecran), on demande tout de
  // suite une resynchronisation de la dalle. C'est gratuit et cela evite
  // qu'un decalage ne s'installe durablement.
  {
    static uint32_t lastStalls = 0;
    uint32_t frames = 0, stalls = 0, maxUs = 0;

    Screen::frameStats(&frames, &stalls, &maxUs);

    if (stalls != lastStalls) {
      lastStalls = stalls;
      tft.restartPanel();
    }
  }

  static unsigned long sensorDelay      = 0;
  static unsigned long screenSaverDelay = millis();
  static bool          needFlush        = false;

  // Appui deja consomme par un changement de page (reveil, ouverture du
  // menu ou d'un historique) : il ne doit pas etre reinterpreté par la
  // nouvelle page. Il reste arme tant que le doigt n'est pas releve.
  static bool          holdGuard        = false;

#if SERIAL_CONSOLE_DEBUG
  // Aide au diagnostic : evite d'attendre les 5 minutes de veille.
  if (Serial.available()) {
    int c = Serial.read();

    switch (c) {
      case 's':                                   // veille immediate
        screenSaverDelay = millis() - SCREEN_SAVER_DELAY - 1;
        Serial.println("[debug] veille forcee");
        break;
      case 'b':                                   // bascule du retro eclairage
        tft.setBacklight(!tft.backlightIsOn());
        break;
      case 'i':                                   // etat courant
        Serial.printf("[debug] page = %d, retroeclairage = %s\n",
                      page, tft.backlightIsOn() ? "ON" : "OFF");
        break;
      default:
        break;
    }
  }
#endif

  if (millis() - screenSaverDelay > SCREEN_SAVER_DELAY)
    page = PAGE_SCREEN_SAVER;

  if (tft.getTouch(&t_x, &t_y)) {
    screenSaverDelay = millis();          // Screen saver reinitialization

    if (holdGuard) {
      // Contact deja traite par le changement de page qu'il a provoque.
    } else {
      switch (page) {
        case PAGE_DATA:
          // Chaque widget ouvre sa vue : bandeau gris -> menu, cadrans et
          // graphique -> historique plein ecran correspondant.
          if (inMenuZone(t_x, t_y)) {
            page = PAGE_MENU;
            holdGuard = true;
          } else if (inHumidityZone(t_x, t_y)) {
            page = PAGE_HUM_HIST;
            holdGuard = true;
          } else if (inTemperatureZone(t_x, t_y)) {
            page = PAGE_TEMP_HIST;
            holdGuard = true;
          } else if (inPressureZone(t_x, t_y)) {
            page = PAGE_PRES_HIST;
            holdGuard = true;
          }
          break;
        case PAGE_MENU:
          // Le menu reste affiche jusqu'a "Exit".
          page = manageMenu(page, t_x, t_y);
          needFlush = true;
          break;
        case PAGE_SCREEN_SAVER:
          // Reveil : on rallume et on revient a la page de mesure, sans
          // ouvrir le menu ni un historique, meme si le doigt est dans une
          // zone sensible.
          page = PAGE_DATA;
          holdGuard = true;
          break;
        default:
          // Historique plein ecran : un appui revient a la page de mesure.
          page = PAGE_DATA;
          holdGuard = true;
          break;
      }
    }
  } else {
    holdGuard = false;                    // doigt releve : l'appui suivant compte
    if (page == PAGE_MENU)
      page = manageMenu(page, 0, 0);
  }

  if (old_page != page) {
    switch (page) {
      case PAGE_DATA:
        backLight(true);
        // Le canvas contenait le menu ou un historique : on repeint le fond
        // de la page de mesure avant que les widgets ne soient redessines.
        if (old_page == PAGE_MENU || old_page == PAGE_HUM_HIST ||
            old_page == PAGE_TEMP_HIST || old_page == PAGE_PRES_HIST)
          tft.fillRectVGradient(0,0,UI_W,UI_H, TFT_YELLOW,  TFT_NAVY);
        sensorDelay = 0;
        break;
      case PAGE_MENU:
        backLight(true);
        gui->drawGUI();
        break;
      case PAGE_HUM_HIST:
      case PAGE_TEMP_HIST:
      case PAGE_PRES_HIST:
        backLight(true);
        now = rtcClockNow();
        drawFullHistoryPage(historyKindOf(page));
        break;
      case PAGE_SCREEN_SAVER:
        backLight(false);
        break;
    }
    needFlush = true;
  }

  old_page = page;

  if (guiNeedsFlush) {          // bouton relache : le canvas a change
    needFlush = true;
    guiNeedsFlush = false;
  }

  // Reglage modifie depuis la page web : on force un rafraichissement
  // complet de la page affichee (unite, heure...).
  if (uiRefresh) {
    uiRefresh   = false;
    sensorDelay = 0;
  }

  if (millis() - sensorDelay > SENSOR_DELAY || sensorDelay == 0) {
    getSensorData();

    if (page == PAGE_DATA) {
      drawMainPage(humidity, temp, pres);
      needFlush = true;
    } else if (page == PAGE_HUM_HIST || page == PAGE_TEMP_HIST || page == PAGE_PRES_HIST) {
      // Rafraichissement de l'historique affiche (etiquettes d'heure et
      // derniere mesure) a la meme cadence que la page de mesure.
      drawFullHistoryPage(historyKindOf(page));
      needFlush = true;
    }

    sensorDelay = millis();
  }

  // Le dessin se fait hors ecran (canvas en PSRAM) : on le recopie
  // dans le framebuffer de la dalle uniquement quand quelque chose a change.
  if (needFlush) {
    tft.flush();
    needFlush = false;
  }

  // Les trames NMEA sont toujours emises en unites metriques, quelle que
  // soit l'unite d'affichage choisie pour l'ecran (temp a pu etre converti
  // en degres F par le dessin de la page de mesure).
  nmeaOutput(speed_nmea, humidity, tempC, pres);
}
