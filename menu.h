#ifndef _INC_MENU_H
#define _INC_MENU_H 

#include <stdint.h>
#include <math.h>
//#include "Free_Fonts.h" // Include the header file attached to this sketch

#include <TFT_eSPI.h>              // Hardware-specific library
#include <TFT_eWidget.h>           // Widget library
#include "Definition.h"

#define BUTTON_W        220
#define BUTTON_H        30

#define MAX_NB_OPTION_MENU 7

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

class GUIItem {
public:
  GUIItem(TFT_eSPI *tft, int16_t id);
  void          initItem(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t outline = TFT_BLACK, uint16_t fill = TFT_LIGHTGREY, uint16_t textColor = TFT_BLACK, uint8_t textSize = 1);
  void          setItemLabel(String label, uint16_t fgcolor, uint16_t bgcolor);
  void          setItemLimit(int32_t min, int32_t max);
  void          incItemValue();
  void          decItemValue();
  void          setItemValue(int32_t value);
  int32_t       getItemValue();
  bool          isItemPress(int16_t x, int16_t y);
  bool          isItemFocus();
  void          setItemFocus(bool focus);
  void          drawItem();

protected:
  ButtonWidget  *_item;  
  TFT_eSPI      *_tft;
  int16_t       _itemId;
  String        _label;
  uint16_t      _labelFgColor;
  uint16_t      _labelBgColor;
  uint16_t      _frameOutline;
  uint16_t      _frameFill;
  uint16_t      _textColor;
  uint8_t       _textSize;
  uint16_t      _x;
  uint16_t      _y;
  uint16_t      _w;
  uint16_t      _h;
  int32_t       _value;
  int32_t       _min;
  int32_t       _max;
  bool          _focus;
};

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

class GUI {
public:
  void setFrameColor(uint16_t outline, uint16_t fill);
  virtual void drawGUI();
  virtual uint16_t checkTouch(uint16_t t_x, uint16_t t_y);
  virtual void initGUI(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t outline, uint16_t fill, uint16_t textColor, uint8_t textSize);

protected:
  TFT_eSPI     *_tft;
  int16_t      _GUIId;
  uint16_t     _frameFill;
  uint16_t     _frameOutline;
  uint16_t     _x;
  uint16_t     _y;
  uint16_t     _w;
  uint16_t     _h;
  bool         _focus;
};

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

class Menu : public GUI {
public:
  Menu(TFT_eSPI *tft, int16_t id, int8_t nbButtons);
  void setButtonLabel(uint8_t buttonId, String label);
  void initGUI(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t outline, uint16_t fill, uint16_t textColor, uint8_t textSize);
  //------------------------------
  void drawGUI();
  uint16_t checkTouch(uint16_t t_x, uint16_t t_y);

protected:
  uint8_t      _nbButtons;
  uint16_t     _buttonPress;
  uint16_t     _buttonRelease;
  uint16_t     _buttonW;
  uint16_t     _buttonH;
  uint16_t     _buttonFill;
  uint16_t     _buttonOutline;
  String       _buttonLabel[MAX_NB_OPTION_MENU];
  ButtonWidget *_menuButton[MAX_NB_OPTION_MENU];
};

//--------------------------------------------------------------------------------------
//
//
//
//--------------------------------------------------------------------------------------

class Form: public GUI {
public:
  Form(TFT_eSPI *tft, int16_t id);

  void initGUI(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t outline, uint16_t fill, uint16_t textColor, uint8_t textSize);
  void drawGUI();
  uint16_t checkTouch(uint16_t t_x, uint16_t t_y);
  
  void setYear(int16_t value);
  void setMonth(int16_t value);
  void setDay(int16_t value);
  void setHour(int16_t value);
  void setMin(int16_t value);
  int16_t getYear();
  int16_t getMonth();
  int16_t getDay();
  int16_t getHour();
  int16_t getMin();

  void setFocusValue(int16_t value);
  void incFocusValue();
  void decFocusValue();
  int16_t getFocusValue();

  void setMessage(String message);

protected:
  ButtonWidget *_formButton[4];
  String       _buttonLabel[4];
  uint8_t      _nbButtons;
  uint16_t     _buttonPress;
  uint16_t     _buttonRelease;
  uint16_t     _buttonFill;
  uint16_t     _buttonOutline;

  GUIItem      *_items[5];
  uint8_t      _nbItems;
  uint8_t      _idItemFocus;
  
  String       _message;
};

#endif
