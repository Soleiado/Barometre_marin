
#ifndef _INC_NMEA_H
#define _INC_NMEA_H

// Ouvre le pilote NMEA 2000 (bus CAN dedie). A appeler une fois au
// demarrage pour que le barometre s'annonce sur le bus ; l'emission des
// PGN se fait ensuite dans nmeaOutput().
void nmea2KInit();

void nmeaOutput(int32_t mode, int8_t humidity, float temp, float pres);

#endif