#pragma once
#include <Arduino.h>
#include <ModbusMaster.h>

class DevOxygen {
public:
  DevOxygen(HardwareSerial* hwSerial, uint8_t slaveId = 1)
    : _serial(hwSerial), _slaveID(slaveId),
      _oxygenPct(0), _oxygenMgl(0), _temperature(0),
      _lastReadSuccess(false), _lastReadTime(0) {}

  void begin(unsigned long baudRate = 9600) {
    _serial->begin(baudRate, SERIAL_8N1);
    delay(100);
    _modbus.begin(_slaveID, *_serial);
  }

  bool update() {
    uint8_t result = _modbus.readHoldingRegisters(0x0000, 6);
    if (result == _modbus.ku8MBSuccess) {
      uint32_t raw0, raw1, raw2;

      raw0 = ((uint32_t)_modbus.getResponseBuffer(0) << 16)
           | _modbus.getResponseBuffer(1);
      memcpy(&_oxygenPct, &raw0, sizeof(float));
      _oxygenPct *= 100.0f;  // ✅ 0.85 → 85.00 %

      raw1 = ((uint32_t)_modbus.getResponseBuffer(2) << 16)
           | _modbus.getResponseBuffer(3);
      memcpy(&_oxygenMgl, &raw1, sizeof(float));

      raw2 = ((uint32_t)_modbus.getResponseBuffer(4) << 16)
           | _modbus.getResponseBuffer(5);
      memcpy(&_temperature, &raw2, sizeof(float));

      _lastReadSuccess = true;
      _lastReadTime = millis();
      return true;
    }
    _lastReadSuccess = false;
    return false;
  }

  float getOxygenPct()      const { return _oxygenPct; }
  float getOxygenMgl()      const { return _oxygenMgl; }
  float getTemperature()    const { return _temperature; }
  bool  isLastReadSuccess() const { return _lastReadSuccess; }

private:
  HardwareSerial* _serial;
  ModbusMaster    _modbus;
  uint8_t         _slaveID;
  float           _oxygenPct, _oxygenMgl, _temperature;
  bool            _lastReadSuccess;
  unsigned long   _lastReadTime;
};