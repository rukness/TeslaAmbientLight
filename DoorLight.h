#ifndef DOOR_LIGHT_H
#define DOOR_LIGHT_H
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "CarLight.h"

const int numPixelsFront = 136;
const int numPixelsRear = 100;
const int numPixelsPillar = 25;
const int numPixels = numPixelsFront + numPixelsPillar + numPixelsRear;
const bool pocketLedAddress = false;

class DoorLight {
public:
  DoorLight();
  void init(byte doorNum, byte ledPin, byte pocketLedPin);
  void setColorByCarState(CarLight& carLight);
protected:
  int _numPixels;
  Adafruit_NeoPixel* _strip;
  Adafruit_NeoPixel* _pocketStrip;
  byte _doorNum;
  byte _ledPin;
  byte _pocketLedPin;
  byte _oldBrightness;
  byte _stripBrightness;
  byte _brightness;
  unsigned int _transition = 300;
  int _firstPixel = 0;
  int _lastPixel = numPixelsFront - 1;
  float _currentColor[3][numPixels];
  uint32_t _oldColor[numPixels];
  float _targetColor[3][numPixels];
  uint32_t _oldTargetColor[numPixels];
  unsigned long _colorTransitionLastMs;
  unsigned long _targetStateMs;
  void _setTargetColor(int i, float r, float g, float b);
  void _setCurrentColor(int i, float r, float g, float b);
  void _updateTargetColor(int i, float r, float g, float b);
  void _fadeColor();
  void _fadeBrightness();
  void _pushColorToStrip();
};
#endif