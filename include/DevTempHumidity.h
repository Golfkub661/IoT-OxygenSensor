#pragma once
#include <Arduino.h>
#include <ModbusMaster.h>

class DevTempHumidity {
public:
  DevTempHumidity(HardwareSerial* hwSerial, uint8_t slaveId = 2)
    : _serial(hwSerial), _slaveID(slaveId),
      _temperature(0), _humidity(0),
      _lastReadSuccess(false), _lastReadTime(0) {}

  void begin() {
    _modbus.begin(_slaveID, *_serial);
  }

  bool update() {
    uint8_t result = _modbus.readInputRegisters(0x0001, 2);
    if (result == _modbus.ku8MBSuccess) {
      int16_t  rawTemp = (int16_t)_modbus.getResponseBuffer(0);
      uint16_t rawHumi = _modbus.getResponseBuffer(1);
      _temperature = rawTemp / 10.0f;
      _humidity    = rawHumi / 10.0f;
      _lastReadSuccess = true;
      _lastReadTime = millis();
      return true;
    }
    _lastReadSuccess = false;
    return false;
  }

  float getTemperature()    const { return _temperature; }
  float getHumidity()       const { return _humidity; }
  bool  isLastReadSuccess() const { return _lastReadSuccess; }

private:
  HardwareSerial* _serial;
  ModbusMaster    _modbus;
  uint8_t         _slaveID;
  float           _temperature, _humidity;
  bool            _lastReadSuccess;
  unsigned long   _lastReadTime;
};