#include <stdint.h>
#include "meter.h"
#include "theme.h"

//--------------------------------------------------------------------------------------
// Geometrie du cadran (canvas 800x480).
//
// Le cadran est une demi-couronne de 240 degres : le centre est en bas au
// milieu, l'aiguille part du centre et balaie -60..+60 degres. La taille du
// sprite (METER_W x METER_H) est definie dans meter.h, car elle sert aussi
// de zone de touche dans Barometre_marin.ino.
//--------------------------------------------------------------------------------------

#define METER_CX  (METER_W / 2)              // centre : 160
#define METER_CY  193                        // centre (bas du cadran)
#define METER_R   130                        // rayon de la piste

//--------------------------------------------------------------------------------------

Meter::Meter(TFT_eSPI *tft)
{
  _tft    = tft;     // Pointer to tft class so we can call member functions

  _frame  = new TFT_eSprite(_tft);
  _needle = new TFT_eSprite(_tft);

  _start   = 0;
  _end     = 100;
  _nb_step = 20;
  _nb_mark = 5;
  _nb_zones = 0;
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void Meter::setRange(int16_t start, int16_t end, int16_t nb_step)
{
  _start    = start;
  _end      = end;
  _nb_step  = nb_step;
  _nb_mark  = nb_step / 4;

  _nb_zones = 0;
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void Meter::setZone(int16_t start, int16_t end, uint32_t color)
{
  if (_nb_zones < 3) {
    _zone_start[_nb_zones]  = start < _start ? _start : start;
    _zone_end[_nb_zones]    = end > _end ? _end : end;
    _zone_color[_nb_zones]  = color;
    _nb_zones++;
  }
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void Meter::setLabel(const char* label1, const char* label2, const char* label3)
{
  _label1 = label1;  
  _label2 = label2;  
  _label3 = label3;  
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void Meter::setLabel1(const char* label)
{
  _label1 = label;  
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void Meter::setLabel2(const char* label)
{
  _label2 = label;  
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void Meter::setLabel3(const char* label)
{
  _label3 = label;  
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void Meter::setValue(int16_t value)
{
  _type_value = false;
  _ivalue = value;
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void Meter::setValue(float value)
{
  _type_value = true;
  _fvalue = value;
  _ivalue = (int16_t) value;
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void Meter::setOrigin(int16_t orig_x, int16_t orig_y)
{
  _orig_x = orig_x;
  _orig_y = orig_y;
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void Meter::draw_meter_frame()
{
  // Fond transparent (le canvas principal est deja sombre), puis panneau
  // arrondi et discret.
  _frame->fillSprite(THEME_BG);
  _frame->fillRoundRect(0, 0, METER_W, METER_H, 16, THEME_PANEL);
  _frame->drawRoundRect(0, 0, METER_W - 1, METER_H - 1, 16, THEME_GRID);

  // Piste du cadran : arc fin
  _frame->drawArc(METER_CX, METER_CY, METER_R, METER_R - 3, 120, 240,
                  THEME_TRACK, THEME_PANEL);

  // Zones colorees (3 px), bouts arrondis
  for (int i = 0; i < _nb_zones; i++) {
    int16_t s = map(_zone_start[i], _start, _end, 120, 240);
    int16_t e = map(_zone_end[i], _start, _end, 120, 240);

    _frame->drawSmoothArc(METER_CX, METER_CY, METER_R, METER_R - 3, s, e,
                          _zone_color[i], THEME_PANEL, true);
  }

  const int16_t inc      = 15;   // pas des graduations (degres)
  const int16_t tl_long  = 18;   // longueur des grandes graduations
  const int16_t tl_short = 9;    // longueur des petites

  // Graduations et chiffres : fins et discrets
  _frame->setFreeFont(&FreeSansBold9pt7b);
  _frame->setTextColor(THEME_DIM);

  for (int i = -60; i < 61; i += inc) {
    float sx = cos((i - 90) * 0.0174532925);
    float sy = sin((i - 90) * 0.0174532925);

    int16_t tl = (i % 30 != 0) ? tl_short : tl_long;

    int16_t x0 = sx * (METER_R + tl) + METER_CX;
    int16_t y0 = sy * (METER_R + tl) + METER_CY;
    int16_t x1 = sx * METER_R + METER_CX;
    int16_t y1 = sy * METER_R + METER_CY;

    _frame->drawLine(x0, y0, x1, y1, (i % 30 == 0) ? THEME_DIM : THEME_GRID);

    // Une graduation sur deux porte une valeur
    if (i % 30 == 0) {
      char buf[10];

      switch (i / 30) {
        case -2: sprintf(buf, "%d", _start);                             break;
        case -1: sprintf(buf, "%d", _start +     (_end - _start) / 4);   break;
        case  0: sprintf(buf, "%d", _start + 2 * (_end - _start) / 4);   break;
        case  1: sprintf(buf, "%d", _start + 3 * (_end - _start) / 4);   break;
        default: sprintf(buf, "%d", _end);                               break;
      }

      _frame->setTextDatum(BC_DATUM);
      _frame->drawString(buf, x0, y0 - 2);
    }
  }

  // Libelle du cadran : discret
  _frame->setFreeFont(&FreeSansBold9pt7b);
  _frame->setTextColor(THEME_DIM);
  _frame->setTextDatum(TC_DATUM);
  _frame->drawString(_label1, METER_CX, 118);

  // Valeur courante : grande et lumineuse
  _frame->setFreeFont(&FreeSansBold18pt7b);
  _frame->setTextColor(THEME_TEXT);
  _frame->setTextDatum(TL_DATUM);
  _frame->drawString(_label2, 16, 184);
  _frame->setTextDatum(TR_DATUM);
  _frame->drawString(_label3, METER_W - 16, 184);
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void Meter::move_needle()
{
  float sdeg = map(_ivalue, _start, _end, -60, 60);  // Map value to angle

  // Aiguille fine, ambre, avec un petit moyeu.
  _needle->fillSprite(THEME_BG);
  _needle->drawWedgeLine(6, 0, 6, METER_R, 1, 3, THEME_AMBER);
  _needle->fillCircle(6, METER_R, 4, THEME_AMBER);
  _needle->pushRotated(_frame, sdeg, THEME_BG);
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void Meter::drawMeter()
{
  _frame->createSprite(METER_W, METER_H);
  _needle->createSprite(12, METER_R + 10);

  _frame->setPivot(METER_CX, METER_CY);
  _needle->setPivot(6, METER_R);

  draw_meter_frame();
  move_needle();
  // Le canvas de l'ecran est un TFT_eSprite : on pousse le cadre dedans
  // (pushSprite viserait la dalle, que TFT_eSPI ne pilote plus).
  _frame->pushToSprite(static_cast<TFT_eSprite *>(_tft), _orig_x, _orig_y);

  _frame->deleteSprite();
  _needle->deleteSprite();
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void Meter::drawMeter(int16_t x, int16_t y)
{
  _orig_x = x;
  _orig_y = y;

  drawMeter();
}

//--------------------------------------------------------------------------------------
