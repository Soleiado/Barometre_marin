//=====================================================================
//  navdata.h - donnees de navigation recues du bus NMEA 2000
//
//  Le barometre ecoute le bus CAN et memorise les PGN suivants :
//    130306  Wind Data        (vent apparent / vrai)
//    128259  Speed             (vitesse surface)
//    128267  Water Depth       (profondeur)
//    129026  COG & SOG         (cap fond et vitesse fond)
//    127250  Vessel Heading    (cap vrai)
//
//  Unites d'affichage : vitesses en nœuds, angles en degres (0..360),
//  profondeur en metres. La valeur 0 est la valeur par defaut : une donnee
//  non encore recue vaut 0 (voir "updated" pour savoir si le bus repond).
//=====================================================================

#ifndef _INC_NAVDATA_H
#define _INC_NAVDATA_H

#include <stdint.h>

struct NavData {
  float    windSpeedApp  = 0;    // vent apparent, nœuds
  float    windAngleApp  = 0;    // vent apparent, degres (0 = face, sens horaire)
  float    windSpeedTrue = 0;    // vent vrai, nœuds
  float    windAngleTrue = 0;    // vent vrai, degres (0 = nord, sens horaire)
  float    sog           = 0;    // vitesse fond, nœuds
  float    stw           = 0;    // vitesse surface, nœuds
  float    depth         = 0;    // profondeur, metres
  float    heading       = 0;    // cap vrai, degres (0 = nord)
  float    cog           = 0;    // route fond, degres
  uint32_t updated       = 0;    // millis() de la derniere reception
};

extern NavData nav;

#endif // _INC_NAVDATA_H
