
#ifndef _INC_SCREEN_H
#define _INC_SCREEN_H

//=====================================================================
//  Ecran de la carte Waveshare ESP32-S3-Touch-LCD-4.3B
//
//  La dalle est un ecran RGB parallele 800x480 (ST7262) pilote par
//  ESP32_Display_Panel ; TFT_eSPI ne sait pas piloter ce type d'ecran.
//
//  Toute l'interface du projet est dessinee avec TFT_eSPI dans un
//  "canvas" memoire de 800x480 place en PSRAM (Screen herite de
//  TFT_eSprite). Le canvas fait exactement la taille de la dalle : il est
//  recopie tel quel dans le framebuffer, sans agrandissement ni mise a
//  l'echelle (1 pixel du canvas = 1 pixel de la dalle).
//
//  Les coordonnees tactiles sont deja dans le repere du canvas.
//=====================================================================

#include <Arduino.h>

// TFT_eSPI 2.5.43 teste CONFIG_SPIRAM_SUPPORT, symbole disparu depuis
// IDF 5 (il s'appelle desormais CONFIG_SPIRAM) : sans cela les sprites
// seraient alloues en RAM interne et un canvas de 800x480 echouerait.
#if defined(CONFIG_SPIRAM) && !defined(CONFIG_SPIRAM_SUPPORT)
  #define CONFIG_SPIRAM_SUPPORT 1
#endif

#include <TFT_eSPI.h>

// Geometrie : le canvas a la taille native de la dalle
#define UI_W        800
#define UI_H        480
#define PANEL_W     800
#define PANEL_H     480

// Frequence pixel de la dalle. Celle de la carte est 16 MHz ; une baisse a
// 12 MHz a ete essayee, elle donne un ecran entierement blanc (horloge
// refusee par le controleur RGB), d'ou le retour a 16 MHz.
#define PANEL_PCLK_HZ   (16 * 1000 * 1000)

// Hauteur du bounce buffer de la dalle, en lignes (la carte en utilise 10).
// C'est la memoire tampon interne qui absorbe les pauses du bus PSRAM : plus
// elle est haute, plus la dalle tolere une longue interruption (traitement
// WiFi, copie d'ecran) avant de perdre le fil. En contrepartie, un decrochage
// eventuel decale l'image d'autant de lignes : 20 lignes = 64 Ko de RAM
// interne, soit une marge doublee par rapport au reglage de la carte.
#define PANEL_BOUNCE_LINES  20

// Lignes de blanking vertical (8 sur la carte). Ce temps mort a ete porte a
// 400 lignes pour soulager le bus memoire, mais il s'est revele contre-productif :
// plus il est long, plus la dalle peut se recalier au mauvais endroit quand
// elle perd la synchronisation (decalage observe : ~200 px, soit la moitie de
// 400). On revient donc a une valeur proche de celle de la carte, et on garde
// la marge ailleurs (bounce buffer et copie d'ecran par tranches).
#define PANEL_VBP_LINES     16

// Plus de zoom : 1 pixel du canvas = 1 pixel de la dalle
#define UI_ZOOM_NUM 1
#define UI_ZOOM_DEN 1
#define UI_OFF_X    0
#define UI_OFF_Y    0

class Screen : public TFT_eSprite {
public:
  Screen();

  void     init();                                          // carte + canvas
  void     setRotation(uint8_t rotation) {}                   // canvas toujours dans le meme sens
  uint8_t  getRotation() { return 0; }

  uint8_t  getTouch(uint16_t *x, uint16_t *y, uint16_t threshold = 600);

  void     flush();                                           // canvas -> framebuffer de la dalle
  void     setBacklight(bool on);
  bool     backlightIsOn() { return _backlightOn; }

  // Diagnostic : le bounce buffer de la dalle a-t-il pu etre active ?
  static bool bounceBufferActive();
  static int  pixelClockHz();
  static int  setPixelClock(uint32_t hz);
  static bool ready();

  // Diagnostic : rythme de balayage. `stalls` compte les images qui ont mis
  // beaucoup plus de temps que prevu, signe que le bus memoire a ete bloque
  // et que la dalle a manque de donnees (c'est ce qui decale l'image).
  static void frameStats(uint32_t *frames, uint32_t *stalls, uint32_t *maxUs);

  // Reinitialise uniquement la dalle (pilote + framebuffer) : remet l'image
  // en place sans redemarrer la carte, donc sans perdre l'historique.
  static bool reinit();
  static void restartPanel();          // resynchronise la dalle (anti-decalage)
  static int  restartError();          // code d'erreur du dernier essai

private:
  void     clearPanel();

  bool     _backlightOn = false;
};

//---------------------------------------------------------------------
// Sortie du CH422G (expandeur I2C de la carte) : regle le niveau d'une
// sortie EXIO tout en preservant les autres. Utilise pour le retro
// eclairage (EXIO2) et pour le chip-select de la carte SD (EXIO4).
//---------------------------------------------------------------------
void ch422SetIOBit(uint8_t bit, bool high);

#endif // _INC_SCREEN_H
