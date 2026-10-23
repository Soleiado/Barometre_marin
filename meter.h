
#ifndef _INC_METER_H
#define _INC_METER_H 

#include <stdint.h>
#include <math.h>
#include <TFT_eSPI.h> // Hardware-specific library

// Taille du sprite d'un cadran. Sert aussi de zone de touche pour ouvrir
// l'historique plein ecran (voir HUM_METER_X/Y dans Barometre_marin.ino).
#define METER_W  320
#define METER_H  222

class Meter {
public:
  Meter(TFT_eSPI *tft);

  void setRange(int16_t start, int16_t end, int16_t nb_step);
  void setZone(int16_t start, int16_t end, uint32_t color);
  void setLabel(const char* label1, const char* label2="", const char* label3="");
  void setLabel1(const char* label);
  void setLabel2(const char* label);
  void setLabel3(const char* label);
  void setValue(int16_t value);
  void setValue(float value);
  void setOrigin(int16_t orig_x, int16_t orig_y);
  void drawMeter();
  void drawMeter(int16_t x, int16_t y);

  TFT_eSPI    *_tft;
  TFT_eSprite *_frame;
  TFT_eSprite *_needle;

private:
  String  _label1;
  String  _label2;
  String  _label3;
  bool    _type_value;
  int16_t _ivalue;
  float   _fvalue;
  int16_t _orig_x;
  int16_t _orig_y;
  int16_t _start;
  int16_t _end;
  int16_t _nb_step;
  int16_t _nb_mark;

  int16_t _nb_zones;
  int16_t _zone_start[3];
  int16_t _zone_end[3];
  uint32_t _zone_color[3];

  void draw_meter_frame();
  void move_needle();
};

#endif