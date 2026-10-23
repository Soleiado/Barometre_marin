//=====================================================================
//  bme280_local.h - pilote BME280 minimal, I2C uniquement
//
//  Pourquoi un pilote maison plutot qu'une bibliotheque toute faite :
//
//    * le bus I2C de la carte (SDA 8, SCL 9) est tenu par le pilote
//      "legacy" d'ESP-IDF, installe par ESP32_Display_Panel pour le
//      tactile GT911 et l'expandeur CH422G. Wire y installe le nouveau
//      pilote i2c_master, qui ne peut pas cohabiter sur le meme port
//      (voir i2c_bus.h) ;
//    * les bibliotheques publiques (Adafruit, SV-Zanshin) parlent Wire
//      en dur : il faudrait les modifier puis les redistribuer ;
//    * SV-Zanshin est sous licence GPL v3, incompatible avec la licence
//      MIT de ce projet.
//
//  Ce pilote se limite a ce dont le barometre a besoin : lecture des
//  registres de calibration, suréchantillonnage, filtre IIR, temps de
//  veille, mode normal ou force, puis compensation numerique.
//
//  Les formules de compensation sont celles publiees par Bosch
//  Sensortec dans son code de reference BME280_driver (licence
//  BSD-3-Clause, copyright conserve dans bme280_local.cpp). Le reste du
//  fichier est sous licence MIT, comme le reste du projet.
//
//  Unites renvoyees par getSensorData() :
//    temperature   centiemes de degre Celsius    2150   -> 21,50 C
//    humidite      centiemes de pourcent         5800   -> 58,00 %
//    pression      pascals                       101325 -> 1013,25 hPa
//=====================================================================

#ifndef BME280_LOCAL_H
#define BME280_LOCAL_H

#include <Arduino.h>
#include <stdint.h>

// Vitesses I2C : constantes conservees pour ne pas changer les appels
// existants. Le bus est deja ouvert a 400 kHz par i2c_bus.cpp.
const uint32_t I2C_STANDARD_MODE       =  100000;
const uint32_t I2C_FAST_MODE           =  400000;
const uint32_t I2C_FAST_MODE_PLUS_MODE = 1000000;
const uint32_t I2C_HIGH_SPEED_MODE     = 3400000;

// Adresses possibles du BME280 (SDO a la masse ou au VDD)
#define BME280_ADDR_LOW    0x76
#define BME280_ADDR_HIGH   0x77

//---------------------------------------------------------------------
//  Types repris tels quels de l'interface d'origine : les noms sont
//  utilises par le croquis (NormalMode, Oversample16, IIR16...).
//---------------------------------------------------------------------

enum modeTypes         { SleepMode, ForcedMode, ForcedMode2, NormalMode, UnknownMode };
enum sensorTypes       { TemperatureSensor, HumiditySensor, PressureSensor, UnknownSensor };
enum oversamplingTypes { SensorOff, Oversample1, Oversample2, Oversample4,
                         Oversample8, Oversample16, UnknownOversample };
enum iirFilterTypes    { IIROff, IIR2, IIR4, IIR8, IIR16, UnknownIIR };
enum inactiveTimeTypes { inactiveHalf, inactive63ms, inactive125ms, inactive250ms,
                         inactive500ms, inactive1000ms, inactive10ms, inactive20ms,
                         UnknownInactive };

//---------------------------------------------------------------------

class BME280_Class
{
  public:
    bool     begin(const uint32_t i2cSpeed = I2C_STANDARD_MODE);
    void     reset();

    uint8_t  mode(const uint8_t operatingMode = UINT8_MAX);
    bool     setOversampling(const uint8_t sensor, const uint8_t sampling);
    uint8_t  iirFilter(const uint8_t setting = UINT8_MAX);
    uint8_t  inactiveTime(const uint8_t setting = UINT8_MAX);

    void     getSensorData(int32_t &temp, int32_t &hum, int32_t &press);

    bool     present() const { return _present; }
    uint8_t  address() const { return _address; }

  private:
    bool     readCalibration();
    bool     readSensors();

    bool     writeRegister(uint8_t reg, uint8_t value);
    uint8_t  readRegister(uint8_t reg);

    bool     _present = false;
    uint8_t  _address = 0;

    // Registres de configuration, conserves ici pour que les reglages
    // successifs se completent au lieu de s'ecraser.
    uint8_t  _ctrlMeas = 0;     // 0xF4 : osrs_t[7:5] osrs_p[4:2] mode[1:0]
    uint8_t  _ctrlHum  = 0;     // 0xF2 : osrs_h[2:0]
    uint8_t  _config   = 0;     // 0xF5 : t_standby[7:5] filtre[4:2]
    uint8_t  _mode     = SleepMode;

    // Calibration : 0x88..0xA1 puis 0xE1..0xE7
    uint16_t _calT1 = 0, _calP1 = 0;
    int16_t  _calT2 = 0, _calT3 = 0;
    int16_t  _calP2 = 0, _calP3 = 0, _calP4 = 0, _calP5 = 0;
    int16_t  _calP6 = 0, _calP7 = 0, _calP8 = 0, _calP9 = 0;
    uint8_t  _calH1 = 0;
    int16_t  _calH2 = 0, _calH3 = 0, _calH4 = 0, _calH5 = 0, _calH6 = 0;

    // Dernieres valeurs compensees
    int32_t  _tFine       = 0;
    int32_t  _temperature = 0;  // 0,01 C
    int32_t  _humidity    = 0;  // 0,01 %
    int32_t  _pressure    = 0;  // Pa
};

#endif // BME280_LOCAL_H
