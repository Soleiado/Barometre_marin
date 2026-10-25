//=====================================================================
//  sdlog.cpp - enregistrement des mesures sur la carte SD (slot TF)
//
//  Voir sdlog.h pour le format et les broches.
//=====================================================================

#include "sdlog.h"

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>

#include "Definition.h"
#include "datetime_local.h"
#include "rtc_clock.h"
#include "screen.h"     // ch422SetIOBit() : chip-select du slot TF (EXIO4)

//---------------------------------------------------------------------
// Broches du slot TF (SPI) et seuil de remplissage
//---------------------------------------------------------------------

#define SD_EXIO_CS    4       // sortie EXIO4 du CH422G (CS, actif bas)
#define SD_SPI_SCK    12
#define SD_SPI_MISO   13
#define SD_SPI_MOSI   11

#define SD_FULL_PCT   90      // au-dela : suppression du plus ancien fichier

//---------------------------------------------------------------------

static bool s_enabled     = false;
static bool s_present     = false;
static int  s_lastCheckDay = -1;

//---------------------------------------------------------------------
// Reglages persistants (NVS "baro", cle "log")
//---------------------------------------------------------------------

static bool loadSetting()
{
  Preferences p;

  p.begin("baro", true);
  bool on = p.getBool("log", false);
  p.end();

  return on;
}

static void saveSetting(bool on)
{
  Preferences p;

  p.begin("baro", false);
  p.putBool("log", on);
  p.end();
}

//---------------------------------------------------------------------
// Montage de la carte
//---------------------------------------------------------------------

static bool mountCard()
{
  // Le CS est sur l'expandeur, pas sur une broche du microcontroleur : on
  // l'active en direct et on laisse la bibliotheque SD sans gestion de CS.
  ch422SetIOBit(SD_EXIO_CS, false);

  SPI.setHwCs(false);
  SPI.begin(SD_SPI_SCK, SD_SPI_MISO, SD_SPI_MOSI, -1);

  if (SD.begin((uint8_t)-1, SPI, 4000000)) {
    s_present = true;
    return true;
  }

  s_present = false;
  ch422SetIOBit(SD_EXIO_CS, true);

  return false;
}

//---------------------------------------------------------------------

bool sdlogInit()
{
  s_enabled = loadSetting();

  // On monte toujours la carte pour connaitre son etat (presente/absente)
  // et l'afficher dans la page Reglages, meme si l'enregistrement est coupe.
  if (!mountCard()) {
    if (s_enabled) {
      // Reglage active mais pas de carte : on repasse l'option a off.
      s_enabled = false;
      saveSetting(false);
    }
    return false;
  }

  return true;
}

//---------------------------------------------------------------------

bool sdlogSetEnabled(bool on)
{
  if (on && !s_present) {
    // Carte inseree apres le demarrage : on la monte maintenant.
    if (!mountCard()) {
      s_enabled = false;
      saveSetting(false);
      return false;
    }
  }

  s_enabled = on;
  saveSetting(on);

  return true;
}

bool sdlogEnabled() { return s_enabled; }
bool sdlogPresent() { return s_present; }

int sdlogPercentUsed()
{
  if (!s_present) return -1;

  uint64_t total = SD.totalBytes();
  if (total == 0) return -1;

  return (int)(SD.usedBytes() * 100 / total);
}

//---------------------------------------------------------------------
// Suppression du plus ancien fichier CSV (plus petit nom = date la plus
// ancienne, car les noms sont AAAAMMJJ.csv).
//---------------------------------------------------------------------

static void deleteOldestCsv()
{
  File dir = SD.open("/");
  if (!dir) return;

  String oldest;
  File f = dir.openNextFile();

  while (f) {
    String n = f.name();
    if (n.endsWith(".csv") && (oldest.length() == 0 || n < oldest))
      oldest = n;
    f = dir.openNextFile();
  }

  dir.close();

  if (oldest.length())
    SD.remove(("/" + oldest).c_str());
}

//---------------------------------------------------------------------
// Ajout d'une mesure (une ligne toutes les 15 minutes)
//---------------------------------------------------------------------

void sdlogUpdate(float pres, float tempC, int16_t hum)
{
  if (!s_enabled || !s_present)
    return;

  static unsigned long last_time  = millis();
  static bool         first_time  = true;

  if (millis() - last_time < SAVE_DATA_DELAY && !first_time)
    return;

  last_time  = millis();
  first_time = false;

  DateTime now = rtcClockNow();

  // Controle du remplissage : une fois par jour.
  if (now.day() != s_lastCheckDay) {
    s_lastCheckDay = now.day();

    if (sdlogPercentUsed() >= SD_FULL_PCT)
      deleteOldestCsv();
  }

  // Fichier du jour : AAAAMMJJ.csv
  char path[24];
  snprintf(path, sizeof(path), "/%04d%02d%02d.csv",
           now.year(), now.month(), now.day());

  File file = SD.open(path, FILE_APPEND);

  if (!file) {
    // Carte retiree ou pleine : on desactive l'enregistrement.
    s_present = false;
    s_enabled = false;
    saveSetting(false);
    return;
  }

  // Ligne : AAAAMMJJHHMM,NNNN.N,-NN.N,NN (virgule, point decimal)
  file.printf("%04d%02d%02d%02d%02d,%.1f,%.1f,%d\n",
              now.year(), now.month(), now.day(), now.hour(), now.minute(),
              pres, tempC, (int)hum);
  file.close();
}
