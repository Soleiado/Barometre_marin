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
//  L'enregistrement est automatique des qu'une carte est presente (aucun
//  reglage manuel). Quand la carte est pleine a 90 %, le fichier le plus
//  ancien est supprime. L'historique peut etre recupere au demarrage
//  (voir sdlogRecover).
//=====================================================================

#ifndef _INC_SDLOG_H
#define _INC_SDLOG_H

#include <stdint.h>

// Charge le reglage et monte la carte. Des qu'une carte est presente,
// l'enregistrement est actif automatiquement (plus de reglage manuel).
// Retourne true si la carte est montee.
bool sdlogInit();

// Recuperation de l'historique depuis la carte SD apres un redemarrage.
// A appeler une fois au demarrage, apres sdlogInit(). Retourne true si
// l'historique a ete reconstruit.
bool sdlogRecover();

// A appeler a chaque lecture : ajoute une ligne toutes les 15 min.
void sdlogUpdate(float pres, float tempC, int16_t hum);

bool sdlogEnabled();            // carte presente et enregistrement actif
bool sdlogPresent();            // carte montee et accessible

// Occupation de la carte en % (0..100), ou -1 si indisponible.
int  sdlogPercentUsed();

#endif // _INC_SDLOG_H
