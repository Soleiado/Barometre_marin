//=====================================================================
//  Sortie NMEA 2000 (CAN / TWAI)
//
//  La carte Waveshare ESP32-S3-Touch-LCD-4.3B embarque un transceiver
//  CAN relie a GPIO 15 (CANTX) et GPIO 16 (CANRX).
//
//  Le pilote historique de la bibliotheque NMEA2000_esp32
//  (NMEA2000_esp32.cpp) inclut <driver/periph_ctrl.h>, supprime depuis
//  ESP-IDF 5 : il ne compile plus. La meme bibliotheque fournit un
//  second pilote (NMEA2000_esp32_twai.cpp) ecrit pour le controleur TWAI
//  d'IDF 5 (driver/twai.h), avec sa tache de controle et la gestion
//  bus-off / bus-recovery.
//
//  NMEA2000_CAN.h n'est volontairement pas inclus : il instancie en dur
//  le pilote legacy (tNMEA2000_esp32). L'objet NMEA2000 est cree ici.
//
//  PGN emis (1 Hz, decales de 50 ms les uns des autres) :
//    130316  Temperature (extended range)
//    130313  Humidite
//    130314  Pression atmospherique
//=====================================================================

#include <Arduino.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "definition.h"
#include "nmea2k.h"

// Pins CAN de la carte : a definir AVANT d'inclure le pilote
#define ESP32_CAN_TX_PIN GPIO_NUM_15
#define ESP32_CAN_RX_PIN GPIO_NUM_16

#include <Preferences.h>
#include <esp_mac.h>
#include <NMEA2000.h>
#include <NMEA2000_esp32_twai.h>
#include <N2kMessages.h>

// Instance unique du pilote (le constructeur installe le controleur TWAI
// et lance sa tache de controle ; l'ouverture se fait dans nmea2Ksetup()).
tNMEA2000 &NMEA2000 = *(new NMEA2000_esp32_twai(ESP32_CAN_TX_PIN, ESP32_CAN_RX_PIN));

//---------------------------------------------------------------------

#define SlowDataUpdatePeriod 1000  // periode d'emission des PGN (ms)
#define TempSendOffset          0  // +   0 ms
#define HumiditySendOffset     50  // +  50 ms
#define PressureSendOffset    100  // + 100 ms

#define N2K_STATUS_PERIOD   10000  // periode de la trace d'etat (ms)

int         NodeAddress = 34;      // adresse N2K courante (memorisee en NVS)
Preferences preferences;

static bool     s_started = false; // pilote ouvert ?
static uint32_t s_sent    = 0;     // PGN acceptes par le controleur
static uint32_t s_failed  = 0;     // PGN refuses (file pleine, bus off...)

// Messages annonces sur le bus
const unsigned long TransmitMessages[] PROGMEM = {130316L, // Temperature
                                                  130313L, // Humidite
                                                  130314L, // Pression
                                                  0
                                                 };

//---------------------------------------------------------------------

static const char *twaiStateName(twai_state_t state)
{
  switch (state) {
    case TWAI_STATE_STOPPED:    return "STOPPED";
    case TWAI_STATE_RUNNING:    return "RUNNING";
    case TWAI_STATE_BUS_OFF:    return "BUS_OFF";
    case TWAI_STATE_RECOVERING: return "RECOVERING";
    default:                    return "?";
  }
}

//---------------------------------------------------------------------

static bool n2kSend(const tN2kMsg &msg)
{
  if (NMEA2000.SendMsg(msg)) {
    s_sent++;
    return true;
  }

  s_failed++;
  return false;
}

//---------------------------------------------------------------------

static void nmea2Ksetup()
{
  uint8_t  chipid[6];
  uint32_t id = 0;

  // Reserve assez de place pour toutes les trames envoyees
  NMEA2000.SetN2kCANMsgBufSize(8);
  NMEA2000.SetN2kCANReceiveFrameBufSize(150);
  NMEA2000.SetN2kCANSendFrameBufSize(150);

  // Identifiant unique a partir de l'adresse MAC
  esp_efuse_mac_get_default(chipid);
  for (int i = 0; i < 6; i++) id += (chipid[i] << (7 * i));

  NMEA2000.SetProductInformation("1",                       // serial
                                 100,                       // code produit
                                 "Barometre marin (ESP32-S3 4.3B)",
                                 "1.0.0 (2026-10-20)",      // version logiciel
                                 "1.0.0 (2026-10-20)");     // version modele

  NMEA2000.SetDeviceInformation(id,
                                130,   // fonction : Atmospheric
                                85,    // classe   : External Environment
                                2046); // code constructeur (non enregistre)

  preferences.begin("nvs", false);
  NodeAddress = preferences.getInt("LastNodeAddress", 34);
  preferences.end();

  NMEA2000.SetMode(tNMEA2000::N2km_NodeOnly, NodeAddress);
  NMEA2000.ExtendTransmitMessages(TransmitMessages);

  bool opened = NMEA2000.Open();

  twai_status_info_t status;
  if (twai_get_status_info(&status) == ESP_OK) {
    Serial.printf("[n2k] TWAI %s, adresse %d, etat %s, tx_err %lu, rx_err %lu\n",
                  opened ? "ouvert" : "ECHEC",
                  (int)NMEA2000.GetN2kSource(),
                  twaiStateName(status.state),
                  (unsigned long)status.tx_error_counter,
                  (unsigned long)status.rx_error_counter);
  } else {
    Serial.printf("[n2k] TWAI %s, adresse %d (etat TWAI indisponible)\n",
                  opened ? "ouvert" : "ECHEC",
                  (int)NMEA2000.GetN2kSource());
  }
}

//---------------------------------------------------------------------
// Trace periodique : etat du controleur et compteurs d'emission.
// Sur un bus absent, l'etat passe en BUS_OFF puis RECOVERING et les
// compteurs d'erreur montent : c'est normal.
//---------------------------------------------------------------------

static void nmea2Kstatus()
{
  static uint32_t last = 0;

  if (millis() - last < N2K_STATUS_PERIOD) return;
  last = millis();

  twai_status_info_t status;
  if (twai_get_status_info(&status) != ESP_OK) return;

  Serial.printf("[n2k] etat %s, envois %lu, echecs %lu, tx_err %lu, rx_err %lu\n",
                twaiStateName(status.state),
                (unsigned long)s_sent,
                (unsigned long)s_failed,
                (unsigned long)status.tx_error_counter,
                (unsigned long)status.rx_error_counter);
}

//---------------------------------------------------------------------
// Cadencement
//---------------------------------------------------------------------

static bool IsTimeToUpdate(unsigned long NextUpdate)
{
  return (NextUpdate < millis());
}

static unsigned long InitNextUpdate(unsigned long Period, unsigned long Offset = 0)
{
  return millis() + Period + Offset;
}

static void SetNextUpdate(unsigned long &NextUpdate, unsigned long Period)
{
  while (NextUpdate < millis()) NextUpdate += Period;
}

//---------------------------------------------------------------------

void SendN2kTemperature(float temp)
{
  static unsigned long SlowDataUpdated = InitNextUpdate(SlowDataUpdatePeriod, TempSendOffset);
  tN2kMsg N2kMsg;

  if (IsTimeToUpdate(SlowDataUpdated)) {
    SetNextUpdate(SlowDataUpdated, SlowDataUpdatePeriod);

    SetN2kPGN130316(N2kMsg, 0, 0, N2kts_InsideTemperature, CToKelvin(temp), N2kDoubleNA);

    n2kSend(N2kMsg);
  }
}

//---------------------------------------------------------------------

void SendN2kHumidity(float humidity)
{
  static unsigned long SlowDataUpdated = InitNextUpdate(SlowDataUpdatePeriod, HumiditySendOffset);
  tN2kMsg N2kMsg;

  if (IsTimeToUpdate(SlowDataUpdated)) {
    SetNextUpdate(SlowDataUpdated, SlowDataUpdatePeriod);

    SetN2kPGN130313(N2kMsg, 0, 0, N2khs_InsideHumidity, humidity, N2kDoubleNA);

    n2kSend(N2kMsg);
  }
}

//---------------------------------------------------------------------

void SendN2kPressure(float pres)
{
  static unsigned long SlowDataUpdated = InitNextUpdate(SlowDataUpdatePeriod, PressureSendOffset);
  tN2kMsg N2kMsg;

  if (IsTimeToUpdate(SlowDataUpdated)) {
    SetNextUpdate(SlowDataUpdated, SlowDataUpdatePeriod);

    SetN2kPGN130314(N2kMsg, 0, 0, N2kps_Atmospheric, mBarToPascal(pres));

    n2kSend(N2kMsg);
  }
}

//---------------------------------------------------------------------
// L'adresse peut changer apres une revendication d'adresse sur le bus :
// on la memorise pour demarrer plus vite au prochain boot.
//---------------------------------------------------------------------

static void CheckSourceAddressChange()
{
  int SourceAddress = NMEA2000.GetN2kSource();

  if (SourceAddress != NodeAddress) {
    NodeAddress = SourceAddress;

    preferences.begin("nvs", false);
    preferences.putInt("LastNodeAddress", SourceAddress);
    preferences.end();
  }
}

//---------------------------------------------------------------------
// Ouverture du pilote : a appeler au demarrage pour que le barometre
// s'annonce (revendication d'adresse) sur le bus CAN des le boot.
// L'appel est sans effet s'il a deja eu lieu.
//---------------------------------------------------------------------

void nmea2KInit()
{
  if (s_started) return;

  nmea2Ksetup();
  s_started = true;
}

//---------------------------------------------------------------------

void nmea2KOutput(int8_t humidity, float temp, float pres)
{
  nmea2KInit();

  SendN2kTemperature(temp);
  SendN2kHumidity(humidity);
  SendN2kPressure(pres);

  NMEA2000.ParseMessages();

  CheckSourceAddressChange();
  nmea2Kstatus();
}
