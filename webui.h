
#ifndef _INC_WEBUI_H
#define _INC_WEBUI_H

//=====================================================================
//  Interface web (WiFi)
//
//  webInit()  : a appeler une fois dans setup(), apres l'ecran.
//  webLoop()  : a appeler a chaque tour de loop() (non bloquant).
//
//  Le barometre rejoint le reseau domestique dont les identifiants sont
//  enregistres dans la NVS via la page /wifi. Si aucun identifiant n'est
//  connu (ou si le reseau ne repond pas), il cree le point d'acces de
//  secours "Barometre" / "barometre123" joignable sur
//  http://192.168.4.1/ ; la meme page /wifi y permet de saisir les
//  identifiants. Une fois connecte, la page est aussi accessible via
//  http://barometre.local/.
//=====================================================================

void webInit();
void webLoop();

// Charge les reglages enregistres (unite de temperature, sortie NMEA).
// A appeler dans setup() avant le premier dessin de l'ecran.
void webSettingsLoad();

#endif // _INC_WEBUI_H
