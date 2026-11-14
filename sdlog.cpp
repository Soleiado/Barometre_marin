//=====================================================================
//  sdlog.cpp - enregistrement des mesures sur la carte SD (slot TF)
//
//  Voir sdlog.h pour le format et les broches.
//=====================================================================

#include "sdlog.h"

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <string.h>
#include <math.h>

#include "Definition.h"
#include "datetime_local.h"
#include "rtc_clock.h"
#include "barometer.h"  // barometerLoadSample() pour la recuperation
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
  // Des qu'une carte est presente, l'enregistrement est actif : plus de
  // reglage manuel.
  if (!mountCard()) {
    s_enabled = false;
    return false;
  }

  s_enabled = true;
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
// Recuperation de l'historique depuis la carte SD
//
// Les mesures sont relues dans les fichiers quotidiens (AAAAMMJJ.csv) :
// les 384 dernieres (96 h) sont conservees, puis rechargees dans le
// buffer d'historique selon l'ecart avec l'heure courante :
//   - ecart < 15 min : prochain enregistrement cale sur le dernier ;
//   - 15 min <= ecart < 1 h : l'ecart est simule avec la derniere valeur ;
//   - ecart >= 1 h (ou horloge incoherente) : pas de recuperation.
//---------------------------------------------------------------------

struct SdSample {
  uint32_t ts;      // unix time
  int16_t  pres;    // hPa (entier)
  int16_t  temp;    // 0,1 degre C
  int16_t  hum;     // %
};

static SdSample s_samples[HIST_SLOTS];
static int16_t  s_sampleCount = 0;

static void addSample(uint32_t ts, int16_t pres, int16_t temp, int16_t hum)
{
  if (s_sampleCount < HIST_SLOTS) {
    SdSample &s = s_samples[s_sampleCount++];
    s.ts = ts; s.pres = pres; s.temp = temp; s.hum = hum;
  } else {
    // Buffer plein : on decale et on ne garde que les plus recents.
    memmove(s_samples, s_samples + 1, (HIST_SLOTS - 1) * sizeof(SdSample));
    SdSample &s = s_samples[HIST_SLOTS - 1];
    s.ts = ts; s.pres = pres; s.temp = temp; s.hum = hum;
  }
}

static void readCsvFile(const char *path)
{
  File f = SD.open(path);
  if (!f) return;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() < 16) continue;   // "AAAAMMJJHHMM,..." fait au moins 16 car.

    unsigned long long ymdhm = 0;    // AAAAMMJJHHMM : 12 chiffres, il faut du 64 bits
    float  pres = 0.0f, temp = 0.0f;
    int    hum  = 0;

    if (sscanf(line.c_str(), "%llu,%f,%f,%d", &ymdhm, &pres, &temp, &hum) != 4)
      continue;

    uint16_t y  = (uint16_t)(ymdhm / 100000000ULL);
    uint16_t mo = (uint16_t)((ymdhm / 1000000ULL) % 100);
    uint16_t d  = (uint16_t)((ymdhm / 10000ULL) % 100);
    uint8_t  h  = (uint8_t)((ymdhm / 100ULL) % 100);
    uint8_t  mi = (uint8_t)(ymdhm % 100);

    DateTime dt(y, mo, d, h, mi, 0);
    if (!dt.isValid()) continue;

    addSample(dt.unixtime(), (int16_t)lroundf(pres), (int16_t)lroundf(temp * 10.0f), (int16_t)hum);
  }

  f.close();
}

//---------------------------------------------------------------------

bool sdlogRecover()
{
  if (!s_present) return false;
  if (!rtcClockIsHardware()) return false;   // horloge logicielle : heure peu fiable

  s_sampleCount = 0;

  // Relire les 5 derniers jours (chronologiquement), en gardant les 384
  // enregistrements les plus recents (96 h).
  DateTime now = rtcClockNow();

  for (int off = 4; off >= 0; off--) {
    DateTime day(now.unixtime() - (uint32_t)off * 86400UL);

    char path[24];
    snprintf(path, sizeof(path), "/%04d%02d%02d.csv", day.year(), day.month(), day.day());

    readCsvFile(path);
  }

  if (s_sampleCount == 0) return false;

  const SdSample &last = s_samples[s_sampleCount - 1];
  int64_t gap = (int64_t)now.unixtime() - (int64_t)last.ts;

  // Ecart incoherent (horloge remise a zero) ou >= 1 h : pas de recuperation.
  if (gap < 0 || gap >= 3600) return false;

  // Recharger les mesures recuperees.
  for (int16_t i = 0; i < s_sampleCount; i++)
    barometerLoadSample(s_samples[i].pres, s_samples[i].temp, s_samples[i].hum, s_samples[i].ts);

  if (gap < 900) {
    // Cas 1 : ecart < 15 min -> prochain enregistrement cale sur le dernier.
    barometerAlignNextSave(last.ts + 900);
  } else {
    // Cas 2 : 15 min <= ecart < 1 h -> simuler l'ecart avec la derniere valeur.
    uint32_t t = last.ts;

    while (t + 900 <= now.unixtime()) {
      t += 900;
      barometerLoadSample(last.pres, last.temp, last.hum, t);
    }

    barometerAlignNextSave(t + 900);
  }

  return true;
}

//---------------------------------------------------------------------
// Ajout d'une mesure. La cadence est geree par barometerTimeToSave(),
// appelee par getSensorData().
//---------------------------------------------------------------------

void sdlogUpdate(float pres, float tempC, int16_t hum)
{
  if (!s_enabled || !s_present)
    return;

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
    return;
  }

  // Ligne : AAAAMMJJHHMM,NNNN.N,-NN.N,NN (virgule, point decimal)
  file.printf("%04d%02d%02d%02d%02d,%.1f,%.1f,%d\n",
              now.year(), now.month(), now.day(), now.hour(), now.minute(),
              pres, tempC, (int)hum);
  file.close();
}
