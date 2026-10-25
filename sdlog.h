//=====================================================================
//  sdlog.h - enregistrement des mesures sur la carte SD (slot TF)
//
//  Le slot TF de la carte Waveshare ESP32-S3-Touch-LCD-4.3B est relie
//  en SPI : MOSI=11, SCK=12, MISO=13, et le chip-select (CS) est la
//  sortie EXIO4 de l'expandeur CH422G (pilote via screen.cpp).
//
//  Format : un fichier par jour, nomme AAAAMMJJ.csv, une ligne par
//  mesure toutes les 15 minutes :
//
//      AAAAMMJJHHMM,NNNN.N,-NN.N,NN
//      (date/heure, pression mb, temperature C, humidite %)
//
//  L'enregistrement est coupe par defaut (reglage NVS "log"). S'il est
//  active sans carte presente, il est remis a off. Quand la carte est
//  pleine a 90 %, le fichier le plus ancien est supprime.
//=====================================================================

#ifndef _INC_SDLOG_H
#define _INC_SDLOG_H

#include <stdint.h>

// Charge le reglage et monte la carte si l'enregistrement est active.
// Retourne true si la carte est montee.
bool sdlogInit();

// A appeler a chaque lecture : ajoute une ligne toutes les 15 min.
void sdlogUpdate(float pres, float tempC, int16_t hum);

// Active / coupe l'enregistrement (persiste en NVS). Retourne false si
// l'activation echoue (pas de carte) : dans ce cas l'option reste off.
bool sdlogSetEnabled(bool on);
bool sdlogEnabled();
bool sdlogPresent();            // carte montee et accessible

// Occupation de la carte en % (0..100), ou -1 si indisponible.
int  sdlogPercentUsed();

#endif // _INC_SDLOG_H
