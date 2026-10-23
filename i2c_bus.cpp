
#include "i2c_bus.h"

BoardI2C boardI2C;

//------------------------------------------------------------------------

bool BoardI2C::writeRegister(uint8_t address, uint8_t reg, const uint8_t *data, uint8_t len)
{
  uint8_t buffer[8];

  if (len > sizeof(buffer) - 1) len = sizeof(buffer) - 1;

  buffer[0] = reg;
  for (uint8_t i = 0; i < len; i++) buffer[i + 1] = data[i];

  return i2c_master_write_to_device(BOARD_I2C_PORT, address, buffer, len + 1,
                                    pdMS_TO_TICKS(50)) == ESP_OK;
}

//------------------------------------------------------------------------

bool BoardI2C::readRegister(uint8_t address, uint8_t reg, uint8_t *data, uint8_t len)
{
  return i2c_master_write_read_device(BOARD_I2C_PORT, address, &reg, 1, data, len,
                                      pdMS_TO_TICKS(50)) == ESP_OK;
}

//------------------------------------------------------------------------

bool BoardI2C::probe(uint8_t address)
{
  return addressPresent(address);
}
