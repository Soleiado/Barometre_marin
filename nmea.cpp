
#include <Arduino.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "nmea.h"
#include "nmea2k.h"
#include "definition.h"

// Sortie NMEA 0183 sur le port RS485 de la carte Waveshare
#define NMEA0183_PORT   Serial1
#define RS485_TX_PIN    44
#define RS485_RX_PIN    43

static void nmea0183Output(int32_t mode, int8_t humidity, float temp, float pres);

//------------------------------------------------------------------------

void nmeaOutput(int32_t mode, int8_t humidity, float temp, float pres)
{
  // Le bus CAN est dedie : la sortie NMEA 2000 est toujours active, quel
  // que soit le mode choisi pour le port RS485.
  nmea2KOutput(humidity, temp, pres);

  // 0 = aucune sortie NMEA 0183, 1 = N2K seul (deja emis ci-dessus)
  if (mode <= 1)
    return;

  nmea0183Output(mode, humidity, temp, pres);
}

//------------------------------------------------------------------------

void nmea0183Output(int32_t mode, int8_t humidity, float temp, float pres)
{
  char                 buffer[256];
  int16_t              temp1      = temp;
  int16_t              temp2      = (int16_t)((temp - (float)temp1) * 10.0);
  int16_t              pres1      = pres / 1000;
  int16_t              pres2      = (int16_t)(((pres/1000.0) - (float)pres1) * 10000.0);
  int8_t               checksum   = 0;
  static unsigned long nmeaDelay  = 0;
  static int32_t       speed_nmea = 0;

  if (millis() - nmeaDelay < NMEA_DELAY)
    return;

  if (mode != speed_nmea) {
    switch (mode) {
      case 4800:
      case 9600:
      case 19200:
      case 38400:
        NMEA0183_PORT.begin((uint32_t)mode, SERIAL_8N1, RS485_TX_PIN, RS485_RX_PIN);
        speed_nmea = mode;
        break;
      default:
        return;
    }
    speed_nmea = mode;
  }

  nmeaDelay = millis();

  //====================================================
  // XDR : Transducer Values
  //====================================================
  //             1 2   3 4       n
  // |   |   |   |       | \\
  // *  $--XDR,a,x.x,a,c--c, ..... *hh<CR><LF> \\
  // Field Number:
  // 1) Transducer Type
  // 2) Measurement Data
  // 3) Units of measurement
  // 4) Name of transducer
  // x) More of the same
  // n) Checksum

  // Example:
  // $WIXDR,C,19.52,C,TempAir*07
  // $WIXDR,P,1.02481,B,Barometer*37

  // Examples of NMEA0183 XDR Transducer Types
  // Type	Meaning
  // A	Angular displacement
  // C	Temperature
  // D	Depth
  // F	Frequency
  // H	Humidity
  // N	Force
  // P	Pressure
  // R	Flow

  // Examples of NMEA0183 XDR units of measurement
  // Unit	Meaning
  // B	Bar
  // C	Celsius
  // D	Degrees
  // M	Meter
  // N	Newton
  // P	Percent
  //====================================================

  sprintf(buffer, "$WIXDR,H,%d.0,P,AirHumidity,C,%d.%01d,C,AirTemp,P,%d.%04d,B,Barometer", humidity, temp1, temp2, pres1, pres2);
  NMEA0183_PORT.print(buffer);

  for (int i = 0; i < strlen(buffer); i++)
    checksum ^= buffer[i];

  sprintf(buffer, "*%02X\r\n", checksum);  
  NMEA0183_PORT.print(buffer);
}

