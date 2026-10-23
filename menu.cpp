#include <stdint.h>
#include "menu.h"

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

GUIItem::GUIItem(TFT_eSPI *tft, int16_t id)
{
  _tft          = tft;
  _item         = new ButtonWidget(_tft);
  _itemId       = id;
  _frameFill    = TFT_LIGHTGREY;
  _frameOutline = TFT_BLACK;
  _x            = 0;
  _y            = 0;
  _w            = 0;
  _h            = 0;
  _value        = 0;
  _textColor    = TFT_BLACK;
  _textSize     = 2;
  _focus        = false;
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void GUIItem::initItem(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t outline, uint16_t fill, uint16_t textColor, uint8_t textSize)
{
  _frameFill    = fill;
  _frameOutline = outline;
  _x            = x;
  _y            = y;
  _w            = w;
  _h            = h;
  _value        = 0;
  _textColor    = textColor;
  _textSize     = textSize;

  _item->initButtonUL(_x, _y, _w, _h, outline, fill, textColor, "rien", textSize);
  _item->press(false);
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void GUIItem::setItemLabel(String label, uint16_t fgcolor, uint16_t bgcolor)
{
  _label        = label;
  _labelFgColor = fgcolor;
  _labelBgColor = bgcolor;
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void GUIItem::setItemLimit(int32_t min, int32_t max)
{
  _min = min;
  _max = max;
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void GUIItem::incItemValue()
{
  if (_value == _max)
    _value = _min;
  else
    _value++;
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void GUIItem::decItemValue()
{
  if (_value == _min)
    _value = _max;
  else
    _value--;
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void GUIItem::setItemValue(int32_t value)
{
  _value = value;
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

int32_t GUIItem::getItemValue()
{
  return _value;
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

bool GUIItem::isItemFocus()
{
  return _focus;
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void GUIItem::setItemFocus(bool focus)
{
  _focus = focus;
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

bool GUIItem::isItemPress(int16_t x, int16_t y)
{
  if (_item != NULL) 
    return _item->contains(x, y);

  return false;
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void GUIItem::drawItem()
{
  char buf[10];

  sprintf(buf, "%d", _value);

  if (_item != NULL) {
    _tft->setTextDatum(BL_DATUM);
    _tft->setTextColor(_labelFgColor, _labelBgColor);
    _tft->drawString(_label.c_str(), _x + 5, _y - 5);

    if (_focus) {
      _item->initButtonUL(_x, _y, _w, _h, _frameOutline, TFT_WHITE, _textColor, buf, _textSize);
      _item->drawSmoothButton(false, 3, TFT_BLACK, buf);
    } else {
      _item->initButtonUL(_x, _y, _w, _h, _frameOutline, _frameFill, _textColor, buf, _textSize);
      _item->drawSmoothButton(false, 1, TFT_BLACK, buf);
    }
  }
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void GUI::setFrameColor(uint16_t outline, uint16_t fill)
{
  _frameOutline = outline;
  _frameFill    = fill;
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

Menu::Menu(TFT_eSPI *tft, int16_t id, int8_t nbButtons)
{
  _tft          = tft;
  _GUIId        = id;
  _nbButtons    = nbButtons;
  _buttonPress  = _buttonRelease = 0;
  _frameOutline = TFT_BLACK;
  _frameFill    = TFT_WHITE;
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void Menu::setButtonLabel(uint8_t buttonId, String label)
{
  if (buttonId < _nbButtons)
    _buttonLabel[buttonId] = label;
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void Menu::initGUI(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t outline, uint16_t fill, uint16_t textColor, uint8_t textSize)
{
  _buttonFill     = fill;
  _buttonOutline  = outline;
  _h              = h;
  _w              = w;
  _buttonW        = _w - 20;
  _buttonH        = -6 + (_h - 20 + 6) / _nbButtons;

  if (x < 0) _x = -10 + (_tft->width() - _buttonW) / 2;
  if (y < 0) _y = -10 + (_tft->height() - (_buttonH + 6) * _nbButtons) / 2 ;
  
  x = _x + 10;
  y = _y + 10;

  for (int i = 0; i < _nbButtons; i++ ) {
    _menuButton[i] = new ButtonWidget(_tft);
    if (_menuButton[i] != NULL) {
      _menuButton[i]->initButtonUL(x, y, _buttonW, _buttonH, outline, fill, textColor, (char*)_buttonLabel[i].c_str(), textSize);
      _menuButton[i]->press(false);
    } else
      Serial.println("Erreur initMenu");
    y += (_buttonH + 6);
  }
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void Menu::drawGUI() {
  _tft->fillRect(_x, _y, _w, _h, _frameFill);
  _tft->drawRect(_x, _y, _w, _h, _frameOutline);

  for (int i = 0; i < _nbButtons; i++ ) {
    if (_menuButton[i] != NULL) {
      _menuButton[i]->press(false);
      _menuButton[i]->drawSmoothButton(false, 3, TFT_BLACK, _buttonLabel[i]); // 3 is outline width, TFT_BLACK is the surrounding background colour for anti-aliasing
    }
  }  
  _buttonPress = _buttonRelease  = 0;
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

uint16_t Menu::checkTouch(uint16_t t_x, uint16_t t_y)
{
  _buttonPress = _buttonRelease  = 0;

  for (uint8_t i = 0; i < _nbButtons; i++) {
    if (_menuButton[i] != NULL) {
      if (_menuButton[i]->contains(t_x, t_y)) {
        _menuButton[i]->press(true);
        if (_menuButton[i]->justPressed()) {
          _buttonPress = _GUIId*10 + i + 1;
          _menuButton[i]->drawSmoothButton(true);
        }
      } else {
        _menuButton[i]->press(false);
        if (_menuButton[i]->justReleased()) {
          _buttonRelease = _GUIId*10 + i + 1;
          _menuButton[i]->drawSmoothButton(false);
        }
      }
    }
  }

  return _buttonRelease;
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

Form::Form(TFT_eSPI *tft, int16_t id)
{
  _tft            = tft;
  _GUIId          = id;
  _frameOutline   = TFT_BLACK;
  _frameFill      = TFT_WHITE;

  _nbButtons      = 4;
  _buttonLabel[0] = "Cancel";
  _buttonLabel[1] = "Down";
  _buttonLabel[2] = "Up";
  _buttonLabel[3] = "Ok";

  _nbItems        = 5;
  _idItemFocus    = 0;
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void Form::initGUI(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t outline, uint16_t fill, uint16_t textColor, uint8_t textSize)
{
  _buttonFill     = fill;
  _buttonOutline  = outline;
  _x              = x;
  _y              = y;
  _w              = w;
  _h              = h;

  uint16_t buttonW = 128;
  uint16_t buttonH = 44;
  uint16_t X = x + (w - (_nbButtons * (10 + buttonW) - 10)) / 2;
  uint16_t Y = y + h - buttonH - 10;
  
  for (int i = 0; i < _nbButtons; i++ ) {
    _formButton[i] = new ButtonWidget(_tft);
    if (_formButton[i] != NULL) {
      if (i == 0)
        _formButton[i]->initButtonUL(X, Y, buttonW, buttonH, _buttonOutline, TFT_RED, textColor, (char*)_buttonLabel[i].c_str(), textSize);
      else if (i == 3)
        _formButton[i]->initButtonUL(X, Y, buttonW, buttonH, _buttonOutline, TFT_GREEN, textColor, (char*)_buttonLabel[i].c_str(), textSize);
      else  
        _formButton[i]->initButtonUL(X, Y, buttonW, buttonH, _buttonOutline, _buttonFill, textColor, (char*)_buttonLabel[i].c_str(), textSize);

      _formButton[i]->press(false);
    } else
      Serial.println("Erreur initForm");
    X += (buttonW + 10);
  }

  //------------------------------------------------------------------
  // Champs de saisie : deux lignes centrees (date, puis heure).
  //
  // Les cases sont dimensionnees a partir de la police courante, et
  // l'ecart entre deux cases est superieur a la largeur du libelle
  // (YYYY, MM, ...) pour qu'ils ne se chevauchent jamais.
  //------------------------------------------------------------------

  uint16_t boxW = _tft->textWidth("0000") + 66;   // largeur d'une case
  uint16_t boxH = _tft->fontHeight() + 26;        // hauteur d'une case
  uint16_t gap  = 26;                             // ecart entre deux cases

  // Ligne 1 : annee, mois, jour
  uint16_t rowW = 3 * boxW + 2 * gap;
  uint16_t xRow = _x + (_w - rowW) / 2;
  uint16_t yRow = _y + 78;

  _items[0] = new GUIItem(_tft, 0);
  _items[0]->setItemLimit(2023, 2050);
  _items[0]->initItem(xRow, yRow, boxW, boxH, TFT_BLACK, TFT_LIGHTGREY, TFT_BLACK, 2);
  _items[0]->setItemLabel("YYYY", _frameOutline, _frameFill);
  _items[0]->setItemFocus(true);

  _items[1] = new GUIItem(_tft, 1);
  _items[1]->setItemLimit(1, 12);
  _items[1]->initItem(xRow + (boxW + gap), yRow, boxW, boxH, TFT_BLACK, TFT_LIGHTGREY, TFT_BLACK, 2);
  _items[1]->setItemLabel("MM", _frameOutline, _frameFill);

  _items[2] = new GUIItem(_tft, 2);
  _items[2]->setItemLimit(1, 31);
  _items[2]->initItem(xRow + 2 * (boxW + gap), yRow, boxW, boxH, TFT_BLACK, TFT_LIGHTGREY, TFT_BLACK, 2);
  _items[2]->setItemLabel("DD", _frameOutline, _frameFill);

  // Ligne 2 : heure et minutes
  uint16_t rowW2 = 2 * boxW + gap;
  uint16_t xRow2 = _x + (_w - rowW2) / 2;
  uint16_t yRow2 = yRow + boxH + 44;

  _items[3] = new GUIItem(_tft, 3);
  _items[3]->setItemLimit(0, 23);
  _items[3]->initItem(xRow2, yRow2, boxW, boxH, TFT_BLACK, TFT_LIGHTGREY, TFT_BLACK, 2);
  _items[3]->setItemLabel("hh", _frameOutline, _frameFill);

  _items[4] = new GUIItem(_tft, 4);
  _items[4]->setItemLimit(0, 59);
  _items[4]->initItem(xRow2 + (boxW + gap), yRow2, boxW, boxH, TFT_BLACK, TFT_LIGHTGREY, TFT_BLACK, 2);
  _items[4]->setItemLabel("mm", _frameOutline, _frameFill);
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void Form::drawGUI()
{
  _tft->fillRect(_x, _y, _w, _h, _frameFill);
  _tft->drawRect(_x, _y, _w, _h, _frameOutline);

  // input fields

  for (int i = 0; i < _nbItems; i++ ) {
    if (_items[i] != NULL) {
      _items[i]->drawItem();
    }
  }  

  // control buttons

  for (int i = 0; i < _nbButtons; i++ ) {
    if (_formButton[i] != NULL) {
      _formButton[i]->press(false);
      _formButton[i]->drawSmoothButton(false, 3, TFT_BLACK, _buttonLabel[i]); // 3 is outline width, TFT_BLACK is the surrounding background colour for anti-aliasing
    }
  }  
  _buttonPress = _buttonRelease  = 0;

  if (_message != "") {
    uint16_t x = _x + _w / 2;
    uint16_t y = _y + _h - 100;      // juste au-dessus des boutons

    _tft->setTextDatum(TC_DATUM);
    _tft->setTextColor(TFT_RED);
    _tft->drawString(_message, x - 1, y);
    _tft->drawString(_message, x,     y);
    _tft->drawString(_message, x + 1, y);
  }
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

uint16_t Form::checkTouch(uint16_t t_x, uint16_t t_y)
{
  _buttonPress = _buttonRelease  = 0;

  for (uint8_t i = 0; i < _nbButtons; i++) {
    if (_formButton[i] != NULL) {
      if (_formButton[i]->contains(t_x, t_y)) {
        _formButton[i]->press(true);
        if (_formButton[i]->justPressed()) {
          _buttonPress = _GUIId*10 + i + 1;
          _formButton[i]->drawSmoothButton(true);
        }
      } else {
        _formButton[i]->press(false);
        if (_formButton[i]->justReleased()) {
          _buttonRelease = _GUIId*10 + i + 1;
          _formButton[i]->drawSmoothButton(false);
        }
      }
    }
  }

  if (_buttonRelease == 0) {
    uint8_t idItemFocus = _idItemFocus;

    for (uint8_t i = 0; i < _nbItems; i++) {
      if (_items[i] != NULL) {
        if (_items[i]->isItemPress(t_x, t_y)) idItemFocus = i;
      }
    }

    if (idItemFocus != _idItemFocus) {
      _items[_idItemFocus]->setItemFocus(false);
      _items[idItemFocus]->setItemFocus(true);
      _items[_idItemFocus]->drawItem();
      _items[idItemFocus]->drawItem();
      _idItemFocus = idItemFocus;
    }
  }

  return _buttonRelease;
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void Form::setFocusValue(int16_t value)
{
  if (_items[_idItemFocus] != NULL)
    _items[_idItemFocus]->setItemValue(value);
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void Form::incFocusValue()
{
  if (_items[_idItemFocus] != NULL)
    _items[_idItemFocus]->incItemValue();
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void Form::decFocusValue()
{
  if (_items[_idItemFocus] != NULL)
    _items[_idItemFocus]->decItemValue();
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void Form::setYear(int16_t value)
{
  if (_items[0] != NULL)
    _items[0]->setItemValue(value);
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void Form::setMonth(int16_t value)
{
  if (_items[1] != NULL)
    _items[1]->setItemValue(value);
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void Form::setDay(int16_t value)
{
  if (_items[2] != NULL)
    _items[2]->setItemValue(value);
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void Form::setHour(int16_t value)
{
  if (_items[3] != NULL)
    _items[3]->setItemValue(value);
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void Form::setMin(int16_t value)
{
  if (_items[4] != NULL)
    _items[4]->setItemValue(value);
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

int16_t Form::getFocusValue()
{
  if (_items[_idItemFocus] != NULL)
    return _items[_idItemFocus]->getItemValue();

  return 0;
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

int16_t Form::getYear()
{
  if (_items[0] != NULL)
    return _items[0]->getItemValue();

  return 0;
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

int16_t Form::getMonth()
{
  if (_items[1] != NULL)
    return _items[1]->getItemValue();

  return 0;
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

int16_t Form::getDay()
{
  if (_items[2] != NULL)
    return _items[2]->getItemValue();

  return 0;
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

int16_t Form::getHour()
{
  if (_items[3] != NULL)
    return _items[3]->getItemValue();

  return 0;
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

int16_t Form::getMin()
{
  if (_items[4] != NULL)
    return _items[4]->getItemValue();

  return 0;
}

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

void Form::setMessage(String message)
{
  _message = message;
}