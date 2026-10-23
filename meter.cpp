#include <stdint.h>
#include "meter.h"

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
  _frame->fillSprite(TFT_WHITE);

  // Cadre en degrade (8 px d'epaisseur)
  _frame->fillRectHGradient(            0,  0,   8, METER_H, TFT_LIGHTGREY, TFT_DARKGREY);
  _frame->fillRectHGradient(METER_W - 8,  0,   8, METER_H, TFT_LIGHTGREY, TFT_DARKGREY);
  _frame->fillRectVGradient(            0,  0, METER_W,     8, TFT_DARKGREY,  TFT_LIGHTGREY);
  _frame->fillRectVGradient(            0, METER_H - 8, METER_W, 8, TFT_DARKGREY, TFT_LIGHTGREY);

  _frame->setTextFont(2);

  // Piste du cadran, puis les zones colorees (8 px de large)
  _frame->drawArc(METER_CX, METER_CY, METER_R, METER_R, 120, 240, TFT_BLACK, TFT_WHITE);

  for (int i = 0; i < _nb_zones; i++) {
    int16_t s = map(_zone_start[i], _start, _end, 120, 240);
    int16_t e = map(_zone_end[i], _start, _end, 120, 240);

    _frame->drawArc(METER_CX, METER_CY, METER_R, METER_R - 8, s, e, _zone_color[i], TFT_BLACK);
  }

  const int16_t inc      = 6;    // pas des graduations (degres)
  const int16_t tl_long  = 26;   // longueur des grandes graduations
  const int16_t tl_short = 14;   // longueur des petites

  // Chiffres de l'echelle : police vectorielle, un peu plus grande que la
  // police bitmap 2 utilisee a l'origine.
  _frame->setFreeFont(&FreeSansBold12pt7b);
  _frame->setTextColor(TFT_BLACK);

  // Graduations de -60 a +60 degres autour de la verticale
  for (int i = -60; i < 61; i += inc) {
    float sx = cos((i - 90) * 0.0174532925);
    float sy = sin((i - 90) * 0.0174532925);

    int16_t tl = (i % 30 != 0) ? tl_short : tl_long;

    int16_t x0 = sx * (METER_R + tl) + METER_CX;
    int16_t y0 = sy * (METER_R + tl) + METER_CY;
    int16_t x1 = sx * METER_R + METER_CX;
    int16_t y1 = sy * METER_R + METER_CY;

    _frame->drawLine(x0, y0, x1, y1, TFT_BLACK);

    // Une graduation sur cinq porte une valeur
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
      _frame->drawString(buf, x0, y0 - 3);
    }
  }

  _frame->setFreeFont(&FreeMonoBoldOblique9pt7b);
  _frame->setTextColor(TFT_BLACK);
  _frame->setTextDatum(TC_DATUM);
  _frame->drawString(_label1, METER_CX, 112);

  // Valeurs (humidite, temperature) : c'est la lecture principale du cadran,
  // elles passent en 18pt alors que l'echelle reste en 12pt, et sont placees
  // au plus pres du bas du cadre.
  _frame->setFreeFont(&FreeSansBold18pt7b);
  _frame->setTextDatum(TL_DATUM);
  _frame->drawString(_label2, 16, 184);
  _frame->setTextDatum(TR_DATUM);
  _frame->drawString(_label3, METER_W - 16, 184);

  _frame->drawRect(0, 0, METER_W - 1, METER_H - 1, TFT_BLACK);
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void Meter::move_needle()
{
  float sdeg = map(_ivalue, _start, _end, -60, 60);  // Map value to angle

  _needle->fillSprite(TFT_BLACK);
  _needle->drawWedgeLine(6, 0, 6, METER_R, 2, 7, TFT_RED);
  _needle->fillCircle(6, METER_R, 6, TFT_NAVY);
  _needle->pushRotated(_frame, sdeg, TFT_BLACK);  
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
