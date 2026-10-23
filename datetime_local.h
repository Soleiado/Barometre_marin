//---------------------------------------------------------------------------------
//  Remplacement minimal de la classe DateTime de RTClib.
//
//  RTClib tire Adafruit_BusIO qui inclut <Wire.h> : la bibliotheque Wire
//  d'Arduino utilise le NOUVEAU pilote I2C (esp32-hal-i2c-ng.c), alors que
//  ESP32_Display_Panel utilise l'ANCIEN pilote (i2c_driver_install) pour le
//  CH422G et le GT911.  Les deux ne peuvent pas cohabiter : le constructeur
//  check_i2c_driver_conflict() du vieux pilote fait un abort() au demarrage.
//
//  Seule la classe DateTime est utilisee ici : on la reimplemente, ce qui
//  retire Wire (et le nouveau pilote) du plan de liaison.
//---------------------------------------------------------------------------------

#ifndef _DATETIME_LOCAL_H
#define _DATETIME_LOCAL_H

#include <Arduino.h>

class DateTime {
public:
  DateTime() : _y(2000), _mo(1), _d(1), _h(0), _mi(0), _s(0) {}

  DateTime(uint16_t year, uint8_t month, uint8_t day,
           uint8_t hour = 0, uint8_t minute = 0, uint8_t second = 0)
    : _y(year), _mo(month), _d(day), _h(hour), _mi(minute), _s(second) {}

  DateTime(uint32_t t) { fromUnix(t); }

  DateTime(const char *date, const char *time)
    : _y(2000), _mo(1), _d(1), _h(0), _mi(0), _s(0) {
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    if (date && time) {
      for (uint8_t i = 0; i < 12; i++) {
        if (strncmp(date, months + (i * 3), 3) == 0) { _mo = i + 1; break; }
      }
      _d  = (uint8_t)atoi(date + 4);
      _y  = (uint16_t)atoi(date + 7);
      _h  = (uint8_t)atoi(time);
      _mi = (uint8_t)atoi(time + 3);
      _s  = (uint8_t)atoi(time + 6);
    }
  }

  DateTime(const __FlashStringHelper *date, const __FlashStringHelper *time)
    : DateTime((const char *)date, (const char *)time) {}

  uint16_t year()   const { return _y; }
  uint8_t  month()  const { return _mo; }
  uint8_t  day()    const { return _d; }
  uint8_t  hour()   const { return _h; }
  uint8_t  minute() const { return _mi; }
  uint8_t  second() const { return _s; }

  //  RTClib : une date est valide si elle est dans la plage supportee.
  bool isValid() const {
    if (_y < 2000 || _y > 2099) return false;
    if (_mo < 1 || _mo > 12) return false;
    if (_d < 1 || _d > 31) return false;
    if (_h > 23 || _mi > 59 || _s > 59) return false;
    return true;
  }

  //  0 = dimanche, 1 = lundi, ... 6 = samedi
  uint8_t dayOfTheWeek() const {
    return (uint8_t)(((unixtime() / 86400UL) + 4UL) % 7UL);   // 01/01/1970 = jeudi
  }

  uint32_t unixtime() const {
    //  Conversion date civile -> jours depuis le 01/01/1970 (algorithme de
    //  Howard Hinnant, valable pour tout le calendrier gregorien).
    int y = (int)_y;
    int m = (int)_mo;
    y -= (m <= 2);
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (unsigned)(m + (m > 2 ? -3 : 9)) + 2u) / 5u + (unsigned)_d - 1u;
    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    int32_t days = (int32_t)era * 146097 + (int32_t)doe - 719468;
    return (uint32_t)((int64_t)days * 86400 + (int32_t)_h * 3600 + (int32_t)_mi * 60 + (int32_t)_s);
  }

private:
  void fromUnix(uint32_t t) {
    int32_t days = (int32_t)(t / 86400UL);
    uint32_t rem = t % 86400UL;
    _h  = (uint8_t)(rem / 3600UL); rem %= 3600UL;
    _mi = (uint8_t)(rem / 60UL);
    _s  = (uint8_t)(rem % 60UL);

    int32_t z = days + 719468;
    int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    uint32_t doe = (uint32_t)(z - era * 146097);
    uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    int32_t  y   = (int32_t)yoe + era * 400;
    uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    uint32_t mp  = (5u * doy + 2u) / 153u;
    uint32_t d   = doy - (153u * mp + 2u) / 5u + 1u;
    int32_t  m   = (int32_t)mp + (mp < 10u ? 3 : -9);
    y += (m <= 2);
    _y  = (uint16_t)y;
    _mo = (uint8_t)m;
    _d  = (uint8_t)d;
  }

  uint16_t _y;
  uint8_t  _mo, _d, _h, _mi, _s;
};

#endif // _DATETIME_LOCAL_H
