//=====================================================================
//  bme280_local.cpp - pilote BME280 minimal, I2C uniquement
//
//  Voir bme280_local.h pour la justification et les unites renvoyees.
//
//  Les fonctions de compensation sont la transcription des formules
//  "double" du code de reference Bosch Sensortec (BME280_driver).
//  Le texte de la licence d'origine est reproduit ci-dessous.
//
//  Copyright (C) 2013 Bosch Sensortec
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions
//  are met:
//
//   1. Redistributions of source code must retain the above copyright
//      notice, this list of conditions and the following disclaimer.
//   2. Redistributions in binary form must reproduce the above
//      copyright notice, this list of conditions and the following
//      disclaimer in the documentation and/or other materials provided
//      with the distribution.
//   3. Neither the name of the copyright holder nor the names of its
//      contributors may be used to endorse or promote products derived
//      from this software without specific prior written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
//  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
//  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
//  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
//  COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
//  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
//  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
//  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
//  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
//  STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
//  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
//  OF THE POSSIBILITY OF SUCH DAMAGE.
//
//  Le reste de ce fichier (acces au bus, sequencage des registres) est
//  sous licence MIT, comme le reste du projet.
//=====================================================================

#include "bme280_local.h"
#include "i2c_bus.h"

#include <math.h>

//---------------------------------------------------------------------
// Registres
//---------------------------------------------------------------------

static const uint8_t REG_CALIB_TP  = 0x88;   // 0x88..0xA1 : T1..T3, P1..P9, H1
static const uint8_t REG_ID        = 0xD0;
static const uint8_t REG_RESET     = 0xE0;
static const uint8_t REG_CALIB_H   = 0xE1;   // 0xE1..0xE7 : H2..H6
static const uint8_t REG_CTRL_HUM  = 0xF2;
static const uint8_t REG_STATUS    = 0xF3;
static const uint8_t REG_CTRL_MEAS = 0xF4;
static const uint8_t REG_CONFIG    = 0xF5;
static const uint8_t REG_DATA      = 0xF7;   // 0xF7..0xFE : P, T, H

static const uint8_t BME280_CHIP_ID = 0x60;
static const uint8_t BME280_RESET   = 0xB6;

static const uint8_t CALIB_TP_LEN = 26;
static const uint8_t CALIB_H_LEN  = 7;

//=====================================================================
//  Bus
//=====================================================================

bool BME280_Class::writeRegister(uint8_t reg, uint8_t value)
{
  if (!_address) return false;

  return boardI2C.writeRegister(_address, reg, &value, 1);
}

//---------------------------------------------------------------------

uint8_t BME280_Class::readRegister(uint8_t reg)
{
  uint8_t value = 0;

  if (!_address) return 0;

  boardI2C.readRegister(_address, reg, &value, 1);

  return value;
}

//=====================================================================
//  Initialisation
//=====================================================================

bool BME280_Class::begin(const uint32_t i2cSpeed)
{
  (void) i2cSpeed;          // le bus est deja configure par i2c_bus.cpp

  _present = false;
  _address = 0;

  // Le capteur est en 0x76 si SDO est a la masse, en 0x77 sinon.
  const uint8_t candidates[2] = { BME280_ADDR_LOW, BME280_ADDR_HIGH };

  for (uint8_t i = 0; i < 2 && !_present; i++) {
    _address = candidates[i];

    if (readRegister(REG_ID) == BME280_CHIP_ID)
      _present = true;
  }

  if (!_present) {
    _address = 0;
    return false;
  }

  reset();

  if (!readCalibration()) {
    _present = false;
    return false;
  }

  // Etat de depart : veille, aucune mesure, filtre coupe.
  _ctrlHum  = 0x00;
  _ctrlMeas = 0x00;
  _config   = 0x00;
  _mode     = SleepMode;

  writeRegister(REG_CTRL_HUM,  _ctrlHum);
  writeRegister(REG_CONFIG,    _config);
  writeRegister(REG_CTRL_MEAS, _ctrlMeas);

  return true;
}

//---------------------------------------------------------------------

void BME280_Class::reset()
{
  if (!_address) return;

  writeRegister(REG_RESET, BME280_RESET);

  delay(3);                 // le datasheet demande 2 ms avant tout acces

  // Attendre la fin de la relecture des NVM (bit im_update du statut)
  for (uint8_t i = 0; i < 20; i++) {
    if (!(readRegister(REG_STATUS) & 0x01)) break;

    delay(2);
  }
}

//---------------------------------------------------------------------

bool BME280_Class::readCalibration()
{
  uint8_t b[CALIB_TP_LEN];

  if (!boardI2C.readRegister(_address, REG_CALIB_TP, b, CALIB_TP_LEN))
    return false;

  _calT1 = (uint16_t)(b[0]  | (b[1]  << 8));
  _calT2 = (int16_t) (b[2]  | (b[3]  << 8));
  _calT3 = (int16_t) (b[4]  | (b[5]  << 8));

  _calP1 = (uint16_t)(b[6]  | (b[7]  << 8));
  _calP2 = (int16_t) (b[8]  | (b[9]  << 8));
  _calP3 = (int16_t) (b[10] | (b[11] << 8));
  _calP4 = (int16_t) (b[12] | (b[13] << 8));
  _calP5 = (int16_t) (b[14] | (b[15] << 8));
  _calP6 = (int16_t) (b[16] | (b[17] << 8));
  _calP7 = (int16_t) (b[18] | (b[19] << 8));
  _calP8 = (int16_t) (b[20] | (b[21] << 8));
  _calP9 = (int16_t) (b[22] | (b[23] << 8));

  _calH1 = b[25];           // 0xA1

  uint8_t h[CALIB_H_LEN];

  if (!boardI2C.readRegister(_address, REG_CALIB_H, h, CALIB_H_LEN))
    return false;

  _calH2 = (int16_t)(h[0] | (h[1] << 8));
  _calH3 = (int16_t) h[2];
  _calH4 = (int16_t)(((int8_t) h[3]) * 16 + (h[4] & 0x0F));
  _calH5 = (int16_t)(((int8_t) h[5]) * 16 + (h[4] >> 4));
  _calH6 = (int16_t)((int8_t) h[6]);

  // Un capteur dont la calibration est vide (0 ou 0xFFFF) est defectueux.
  if (_calT1 == 0 || _calT1 == 0xFFFF || _calP1 == 0 || _calP1 == 0xFFFF)
    return false;

  return true;
}

//=====================================================================
//  Reglages
//=====================================================================

uint8_t BME280_Class::mode(const uint8_t operatingMode)
{
  if (operatingMode != UINT8_MAX) {
    uint8_t bits;

    switch (operatingMode) {
      case SleepMode:   bits = 0x00; break;   // sleep
      case ForcedMode:
      case ForcedMode2: bits = 0x01; break;   // forced, une conversion
      case NormalMode:  bits = 0x03; break;   // normal, continu
      default:          return _mode;
    }

    _mode     = operatingMode;
    _ctrlMeas = (uint8_t)((_ctrlMeas & 0xFC) | bits);

    writeRegister(REG_CTRL_MEAS, _ctrlMeas);
  }

  return _mode;
}

//---------------------------------------------------------------------

bool BME280_Class::setOversampling(const uint8_t sensor, const uint8_t sampling)
{
  if (sampling > Oversample16) return false;

  const uint8_t s = sampling;     // la valeur de l'enum est celle du registre

  switch (sensor) {
    case TemperatureSensor:
      _ctrlMeas = (uint8_t)((_ctrlMeas & 0x1F) | (s << 5));
      return writeRegister(REG_CTRL_MEAS, _ctrlMeas);

    case PressureSensor:
      _ctrlMeas = (uint8_t)((_ctrlMeas & 0xE3) | (s << 2));
      return writeRegister(REG_CTRL_MEAS, _ctrlMeas);

    case HumiditySensor:
      _ctrlHum = (uint8_t)(s & 0x07);
      // Le reglage de l'humidite n'est pris en compte qu'a l'ecriture
      // suivante de ctrl_meas : on la refait donc ici.
      writeRegister(REG_CTRL_HUM, _ctrlHum);
      return writeRegister(REG_CTRL_MEAS, _ctrlMeas);

    default:
      return false;
  }
}

//---------------------------------------------------------------------

uint8_t BME280_Class::iirFilter(const uint8_t setting)
{
  if (setting != UINT8_MAX && setting <= IIR16) {
    _config = (uint8_t)((_config & 0xE3) | ((setting & 0x07) << 2));

    writeRegister(REG_CONFIG, _config);
  }

  return (uint8_t)((_config >> 2) & 0x07);
}

//---------------------------------------------------------------------

uint8_t BME280_Class::inactiveTime(const uint8_t setting)
{
  if (setting != UINT8_MAX && setting <= inactive20ms) {
    _config = (uint8_t)((_config & 0x1F) | ((setting & 0x07) << 5));

    writeRegister(REG_CONFIG, _config);
  }

  return (uint8_t)((_config >> 5) & 0x07);
}

//=====================================================================
//  Mesure et compensation
//=====================================================================

bool BME280_Class::readSensors()
{
  if (!_present) return false;

  // Mesure en mode "force" : declencher une conversion, attendre sa fin
  // (bit measuring du statut), puis lire. Fiable et deterministe : en mode
  // normal, lire pendant une conversion donnait parfois des valeurs
  // incoherentes (pression a 714 hPa au lieu de 1028).
  writeRegister(REG_CTRL_MEAS, (uint8_t)((_ctrlMeas & 0xFC) | 0x01));

  for (uint16_t i = 0; i < 250; i++) {
    if (!(readRegister(REG_STATUS) & 0x08)) break;   // measuring = 0
    delay(1);
  }

  uint8_t b[8];

  if (!boardI2C.readRegister(_address, REG_DATA, b, sizeof(b))) return false;

  const uint32_t adcP = ((uint32_t)b[0] << 12) | ((uint32_t)b[1] << 4) | (b[2] >> 4);
  const uint32_t adcT = ((uint32_t)b[3] << 12) | ((uint32_t)b[4] << 4) | (b[5] >> 4);
  const uint32_t adcH = ((uint32_t)b[6] << 8)  |  (uint32_t)b[7];

  //--- temperature ---------------------------------------------------
  double var1 = ((double)adcT) / 16384.0 - ((double)_calT1) / 1024.0;
  double var2 = ((double)adcT) / 131072.0 - ((double)_calT1) / 8192.0;

  var1 = var1 * ((double)_calT2);
  var2 = var2 * var2 * ((double)_calT3);

  _tFine = (int32_t)(var1 + var2);

  const double tempC = (var1 + var2) / 5120.0;

  //--- pression ------------------------------------------------------
  var1 = ((double)_tFine) / 2.0 - 64000.0;
  var2 = var1 * var1 * ((double)_calP6) / 32768.0;
  var2 = var2 + var1 * ((double)_calP5) * 2.0;
  var2 = var2 / 4.0 + ((double)_calP4) * 65536.0;
  var1 = (((double)_calP3) * var1 * var1 / 524288.0 + ((double)_calP2) * var1) / 524288.0;
  var1 = (1.0 + var1 / 32768.0) * ((double)_calP1);

  double presPa;

  if (var1 == 0.0) {
    presPa = 0.0;                       // calibration absente : pas de division
  } else {
    presPa = 1048576.0 - (double)adcP;
    presPa = (presPa - var2 / 4096.0) * 6250.0 / var1;

    var1 = ((double)_calP9) * presPa * presPa / 2147483648.0;
    var2 = presPa * ((double)_calP8) / 32768.0;

    presPa = presPa + (var1 + var2 + ((double)_calP7)) / 16.0;
  }

  //--- humidite ------------------------------------------------------
  double hum = ((double)_tFine) - 76800.0;

  hum = ((double)adcH - ((double)_calH4 * 64.0 + (double)_calH5 / 16384.0 * hum)) *
        ((double)_calH2) / 65536.0 *
        (1.0 + (double)_calH6 / 67108864.0 * hum *
               (1.0 + (double)_calH3 / 67108864.0 * hum));

  hum = hum * (1.0 - (double)_calH1 * hum / 524288.0);

  if (hum > 100.0)     hum = 100.0;
  else if (hum < 0.0)  hum = 0.0;

  //--- unites du projet ----------------------------------------------
  _temperature = (int32_t) lround(tempC * 100.0);   // 0,01 C
  _pressure    = (int32_t) lround(presPa);          // Pa
  _humidity    = (int32_t) lround(hum * 100.0);     // 0,01 %

  return true;
}

//---------------------------------------------------------------------

void BME280_Class::getSensorData(int32_t &temp, int32_t &hum, int32_t &press)
{
  readSensors();

  temp  = _temperature;
  hum   = _humidity;
  press = _pressure;
}
