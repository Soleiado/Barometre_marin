
#include "rtc_clock.h"
#include "i2c_bus.h"

//=====================================================================
//  Registres des circuits
//=====================================================================

#define DS3231_ADDR        0x68
#define PCF85063A_ADDR     0x51

#define DS3231_REG_TIME    0x00    // seconde, minute, heure, jour, date, mois, annee
#define DS3231_REG_STATUS  0x0F    // bit 7 : OSF (oscillateur arrete)

#define PCF85063A_REG_CTRL1   0x00 // bit 5 : STOP
#define PCF85063A_REG_TIME    0x04 // seconde, minute, heure, jour, jour_semaine, mois, annee

enum rtc_type { RTC_NONE = 0, RTC_DS3231, RTC_PCF85063A };

static rtc_type  s_type      = RTC_NONE;
static int32_t   s_softBase  = 0;      // horloge logicielle : secondes depuis 2000

//---------------------------------------------------------------------

static inline uint8_t dec2bcd(uint8_t value) { return (uint8_t)(((value / 10) << 4) | (value % 10)); }
static inline uint8_t bcd2dec(uint8_t value) { return (uint8_t)(((value >> 4) * 10) + (value & 0x0F)); }

//---------------------------------------------------------------------

static bool ds3231Read(DateTime &dt)
{
  uint8_t reg[7];

  if (!boardI2C.readRegister(DS3231_ADDR, DS3231_REG_TIME, reg, sizeof(reg))) return false;

  uint8_t hour = reg[2] & 0x3F;

  if (reg[2] & 0x40) {                       // mode 12 heures
    uint8_t h = bcd2dec(hour & 0x1F);
    if (hour & 0x20) h = (h % 12) + 12;       // PM
    else             h = h % 12;              // AM
    hour = h;
  } else {
    hour = bcd2dec(hour);
  }

  dt = DateTime(2000 + bcd2dec(reg[6]), bcd2dec(reg[5] & 0x1F), bcd2dec(reg[4]),
                hour, bcd2dec(reg[1]), bcd2dec(reg[0] & 0x7F));

  return dt.isValid();
}

//---------------------------------------------------------------------

static bool ds3231Write(DateTime &dt)
{
  uint8_t reg[7];

  reg[0] = dec2bcd(dt.second());            // la remise a 0 du bit CH relance l'oscillateur
  reg[1] = dec2bcd(dt.minute());
  reg[2] = dec2bcd(dt.hour());              // 24 heures
  reg[3] = dt.dayOfTheWeek() + 1;           // 1 = dimanche
  reg[4] = dec2bcd(dt.day());
  reg[5] = dec2bcd(dt.month());
  reg[6] = dec2bcd(dt.year() - 2000);

  return boardI2C.writeRegister(DS3231_ADDR, DS3231_REG_TIME, reg, sizeof(reg));
}

//---------------------------------------------------------------------

static bool pcf85063aRead(DateTime &dt)
{
  uint8_t reg[7];

  if (!boardI2C.readRegister(PCF85063A_ADDR, PCF85063A_REG_TIME, reg, sizeof(reg))) return false;

  uint8_t hour = reg[2] & 0x3F;

  if (reg[2] & 0x40) {                      // mode 12 heures
    uint8_t h = bcd2dec(hour & 0x1F);
    if (hour & 0x20) h = (h % 12) + 12;      // PM
    else             h = h % 12;             // AM
    hour = h;
  } else {
    hour = bcd2dec(hour);
  }

  dt = DateTime(2000 + bcd2dec(reg[6]), bcd2dec(reg[5] & 0x1F), bcd2dec(reg[3]),
                hour, bcd2dec(reg[1]), bcd2dec(reg[0] & 0x7F));

  return dt.isValid();
}

//---------------------------------------------------------------------

static bool pcf85063aWrite(DateTime &dt)
{
  uint8_t ctrl1;
  uint8_t reg[7];

  // Le bit STOP doit etre a 0 pour que l'horloge avance
  if (boardI2C.readRegister(PCF85063A_ADDR, PCF85063A_REG_CTRL1, &ctrl1, 1)) {
    ctrl1 &= (uint8_t)~0x20;
    boardI2C.writeRegister(PCF85063A_ADDR, PCF85063A_REG_CTRL1, &ctrl1, 1);
  }

  reg[0] = dec2bcd(dt.second());            // remet aussi le drapeau OS a 0
  reg[1] = dec2bcd(dt.minute());
  reg[2] = dec2bcd(dt.hour());              // 24 heures
  reg[3] = dec2bcd(dt.day());               // jour du mois
  reg[4] = dt.dayOfTheWeek();               // 0 = dimanche
  reg[5] = dec2bcd(dt.month());
  reg[6] = dec2bcd(dt.year() - 2000);

  return boardI2C.writeRegister(PCF85063A_ADDR, PCF85063A_REG_TIME, reg, sizeof(reg));
}

//---------------------------------------------------------------------

bool rtcClockInit()
{
  DateTime compiled = DateTime(F(__DATE__), F(__TIME__));
  DateTime probe;

  s_softBase = (int32_t)compiled.unixtime();

  //------------------------------------------------------------------
  // 1) DS3231 externe (comme dans la version d'origine)
  //------------------------------------------------------------------
  if (boardI2C.probe(DS3231_ADDR) && ds3231Read(probe)) {
    s_type = RTC_DS3231;
    Serial.println("[rtc] DS3231 detecte");
    if (!probe.isValid() || rtcClockLostPower()) {
      Serial.println("[rtc] heure invalide : reglage sur la date de compilation");
      rtcClockAdjust(compiled);
    }
    return true;
  }

  //------------------------------------------------------------------
  // 2) PCF85063A de la carte
  //------------------------------------------------------------------
  if (boardI2C.probe(PCF85063A_ADDR)) {
    s_type = RTC_PCF85063A;
    Serial.println("[rtc] PCF85063A detecte");

    if (!pcf85063aRead(probe) || !probe.isValid()) {
      Serial.println("[rtc] heure invalide : reglage sur la date de compilation");
      pcf85063aWrite(compiled);
    } else if (rtcClockLostPower()) {
      Serial.println("[rtc] perte d'alimentation : reglage sur la date de compilation");
      pcf85063aWrite(compiled);
    }
    return true;
  }

  //------------------------------------------------------------------
  // 3) Horloge logicielle
  //------------------------------------------------------------------
  s_type = RTC_NONE;
  Serial.println("[rtc] aucun circuit detecte : horloge logicielle");

  return false;
}

//---------------------------------------------------------------------

DateTime rtcClockNow()
{
  DateTime dt;
  uint32_t seconds = (uint32_t)(millis() / 1000);

  switch (s_type) {
    case RTC_DS3231:
      if (ds3231Read(dt)) return dt;
      break;

    case RTC_PCF85063A:
      if (pcf85063aRead(dt)) return dt;
      break;

    default:
      break;
  }

  return DateTime((uint32_t)(s_softBase + seconds));
}

//---------------------------------------------------------------------

bool rtcClockAdjust(DateTime dt)
{
  if (!dt.isValid()) return false;

  switch (s_type) {
    case RTC_DS3231:    return ds3231Write(dt);
    case RTC_PCF85063A: return pcf85063aWrite(dt);

    default:
      // Horloge logicielle : on cale l'origine sur millis()
      s_softBase = (int32_t)dt.unixtime() - (int32_t)(millis() / 1000);
      return true;
  }
}

//---------------------------------------------------------------------

bool rtcClockIsHardware()
{
  return s_type != RTC_NONE;
}

//---------------------------------------------------------------------

bool rtcClockLostPower()
{
  uint8_t status;
  bool    lost = false;

  switch (s_type) {
    case RTC_DS3231:
      if (boardI2C.readRegister(DS3231_ADDR, DS3231_REG_STATUS, &status, 1)) {
        lost = (status & 0x80) != 0;        // OSF
        if (lost) {
          status &= (uint8_t)~0x80;         // efface le drapeau
          boardI2C.writeRegister(DS3231_ADDR, DS3231_REG_STATUS, &status, 1);
        }
      }
      break;

    case RTC_PCF85063A:
      if (boardI2C.readRegister(PCF85063A_ADDR, PCF85063A_REG_TIME, &status, 1)) {
        lost = (status & 0x80) != 0;        // OS
      }
      break;

    default:
      lost = true;                          // horloge logicielle : toujours "perdue"
      break;
  }

  return lost;
}
