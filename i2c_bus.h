
#ifndef _INC_I2C_BUS_H
#define _INC_I2C_BUS_H

//=====================================================================
//  Acces I2C de la carte Waveshare ESP32-S3-Touch-LCD-4.3B
//
//  Le bus I2C de la carte (SDA=8, SCL=9) est initialise par
//  ESP32_Display_Panel, qui utilise le pilote I2C "legacy" d'ESP-IDF
//  sur le port 0 (le tactile GT911 et l'expandeur CH422G sont dessus).
//
//  La classe Wire d'Arduino ne peut pas etre utilisee pour autant :
//  depuis le coeur ESP32 4.0 (IDF 5.4) elle installe le nouveau pilote
//  i2c_master sur le meme port, ce qui est incompatible avec le pilote
//  legacy deja installe par le panneau.
//
//  BoardI2C rejoue donc l'API de Wire directement sur le pilote legacy
//  du port 0, ce qui permet de partager le bus avec le panneau.
//=====================================================================

#include <Arduino.h>
#include <driver/i2c.h>

#define BOARD_I2C_PORT   I2C_NUM_0
#define BOARD_I2C_SDA    8
#define BOARD_I2C_SCL    9
#define BOARD_I2C_FREQ   400000

class BoardI2C {
public:
  // Le bus est deja configure par ESP32_Display_Panel : rien a faire
  void   begin() {}
  void   begin(int, int, uint32_t = BOARD_I2C_FREQ) {}
  void   setClock(uint32_t) {}

  void   beginTransmission(uint8_t address) { _address = address; _txLen = 0; }

  size_t write(uint8_t data) {
    if (_txLen < sizeof(_tx)) _tx[_txLen++] = data;
    return 1;
  }
  size_t write(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) write(data[i]);
    return len;
  }

  uint8_t endTransmission(bool = true) {
    if (_txLen == 0) {
      // Aucun octet a envoyer : c'est une sonde d'adresse (scan du bus)
      return addressPresent(_address) ? 0 : 2;
    }
    esp_err_t err = i2c_master_write_to_device(BOARD_I2C_PORT, _address,
                                               _tx, _txLen, pdMS_TO_TICKS(50));
    return (err == ESP_OK) ? 0 : 1;
  }

  // Motif "Wire" : beginTransmission(adresse); write(registre); endTransmission();
  // puis requestFrom(adresse, nb). Les octets ecrits servent d'adresse de registre.
  size_t requestFrom(uint8_t address, size_t len, bool = true) {
    _rxLen = 0;
    _rxIdx = 0;
    if (len > sizeof(_rx)) len = sizeof(_rx);

    esp_err_t err;
    if (_txLen == 0) {
      err = i2c_master_read_from_device(BOARD_I2C_PORT, address, _rx, len, pdMS_TO_TICKS(50));
    } else {
      err = i2c_master_write_read_device(BOARD_I2C_PORT, address, _tx, _txLen,
                                         _rx, len, pdMS_TO_TICKS(50));
    }
    if (err == ESP_OK) _rxLen = (uint8_t)len;
    return _rxLen;
  }

  int    available() { return (int)(_rxLen - _rxIdx); }
  int    read()      { return (_rxIdx < _rxLen) ? _rx[_rxIdx++] : -1; }

  // Presence d'un composant sur le bus (adresse seule, sans donnee)
  bool   addressPresent(uint8_t address) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (uint8_t)((address << 1) | I2C_MASTER_WRITE), true);
    i2c_master_stop(cmd);

    esp_err_t err = i2c_master_cmd_begin(BOARD_I2C_PORT, cmd, pdMS_TO_TICKS(20));
    i2c_cmd_link_delete(cmd);

    return (err == ESP_OK);
  }

  // Ecriture brute d'un registre
  bool   writeRegister(uint8_t address, uint8_t reg, const uint8_t *data, uint8_t len);
  // Lecture brute d'un registre
  bool   readRegister(uint8_t address, uint8_t reg, uint8_t *data, uint8_t len);
  // Presence d'un composant sur le bus
  bool   probe(uint8_t address);

private:
  uint8_t _address = 0;
  uint8_t _tx[8]   = {0};
  uint8_t _txLen   = 0;
  uint8_t _rx[32]  = {0};
  uint8_t _rxLen   = 0;
  uint8_t _rxIdx   = 0;
};

extern BoardI2C boardI2C;

#endif // _INC_I2C_BUS_H
