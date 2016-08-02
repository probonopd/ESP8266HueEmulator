#include "LightHandler.h"
#include <aJSON.h> // Replace avm/pgmspace.h with pgmspace.h there and set #define PRINT_BUFFER_LEN 4096 ################# IMPORTANT
#include <NeoPixelBus.h> // NeoPixelAnimator branch

LightHandler *lightHandlers[MAX_LIGHT_HANDLERS]; // interfaces exposed to the outside world

bool setLightHandler(int index, LightHandler *handler) {
  if (index >= MAX_LIGHT_HANDLERS || index < 0) return false;
  lightHandlers[index] = handler;
  return true;
}

LightHandler *getLightHandler(int numberOfTheLight) {
  if (numberOfTheLight >= MAX_LIGHT_HANDLERS || numberOfTheLight < 0) {
    return nullptr;
  }

  if (!lightHandlers[numberOfTheLight]) {
    return new LightHandler();
  }

  return lightHandlers[numberOfTheLight];
}


// ==============================================================================================================
// Color Conversion
// ==============================================================================================================
// TODO: Consider switching to something along the lines of
// https://github.com/patdie421/mea-edomus/blob/master/src/philipshue_color.c
// and/ or https://github.com/kayno/arduinolifx/blob/master/color.h
// for color coversions instead
// ==============================================================================================================

// Based on http://stackoverflow.com/questions/22564187/rgb-to-philips-hue-hsb
// The code is based on this brilliant note: https://github.com/PhilipsHue/PhilipsHueSDK-iOS-OSX/commit/f41091cf671e13fe8c32fcced12604cd31cceaf3

RgbColor getXYtoRGB(float x, float y, int brightness) {
  float bright_y = ((float)brightness) / y;
  float X = x * bright_y;
  float Z = (1 - x - y) * bright_y;

  // convert to RGB (0.0-1.0) color space
  float R = X * 1.4628067 - brightness * 0.1840623 - Z * 0.2743606;
  float G = -X * 0.5217933 + brightness * 1.4472381 + Z * 0.0677227;
  float B = X * 0.0349342 - brightness * 0.0968930 + Z * 1.2884099;

  // apply inverse 2.2 gamma
  float inv_gamma = 1.0 / 2.4;
  float linear_delta = 0.055;
  float linear_interval = 1 + linear_delta;
  float r = R <= 0.0031308 ? 12.92 * R : (linear_interval) * pow(r, inv_gamma) - linear_delta;
  float g = G <= 0.0031308 ? 12.92 * G : (linear_interval) * pow(g, inv_gamma) - linear_delta;
  float b = B <= 0.0031308 ? 12.92 * B : (linear_interval) * pow(b, inv_gamma) - linear_delta;

  return RgbColor(r * colorSaturation,
                  g * colorSaturation,
                  b * colorSaturation);
}

int getHue(HsbColor hsb) {
  return hsb.H * 360 * 182.04;
}

int getSaturation(HsbColor hsb) {
  return hsb.S * colorSaturation;
}

RgbColor getMirektoRGB(int mirek) {
  int hectemp = 10000/mirek;
  int r, g, b;
  if (hectemp <= 66) {
    r = colorSaturation;
    g = 99.4708025861 * log(hectemp) - 161.1195681661;
    b = hectemp <= 19 ? 0 : (138.5177312231 * log(hectemp - 10) - 305.0447927307);
  } else {
    r = 329.698727446 * pow(hectemp - 60, -0.1332047592);
    g = 288.1221695283 * pow(hectemp - 60, -0.0755148492);
    b = colorSaturation;
  }
  r = r > colorSaturation ? colorSaturation : r;
  g = g > colorSaturation ? colorSaturation : g;
  b = b > colorSaturation ? colorSaturation : b;
  return RgbColor(r, g, b);
}

HueLightInfo parseHueLightInfo(HueLightInfo currentInfo, aJsonObject *parsedRoot) {
  HueLightInfo newInfo;
  aJsonObject* onState = aJson.getObjectItem(parsedRoot, "on");
  if (onState) {
    newInfo.on = onState->valuebool;
  }

  // pull brightness
  aJsonObject* briState = aJson.getObjectItem(parsedRoot, "bri");
  if (briState) {
    newInfo.brightness = briState->valueint;
  }

  // pull effect
  aJsonObject* effectState = aJson.getObjectItem(parsedRoot, "effect");
  if (effectState) {
    const char *effect = effectState->valuestring;
    if (!strcmp(effect, "colorloop")) {
      newInfo.effect = EFFECT_COLORLOOP;
    } else {
      newInfo.effect = EFFECT_NONE;
    }
  }
  // pull alert
  aJsonObject* alertState = aJson.getObjectItem(parsedRoot, "alert");
  if (alertState) {
    const char *alert = alertState->valuestring;
    if (!strcmp(alert, "select")) {
      newInfo.alert = ALERT_SELECT;
    } else if (!strcmp(alert, "lselect")) {
      newInfo.alert = ALERT_LSELECT;
    } else {
      newInfo.alert = ALERT_NONE;
    }
  }

  aJsonObject* hueState = aJson.getObjectItem(parsedRoot, "hue");
  aJsonObject* satState = aJson.getObjectItem(parsedRoot, "sat");
  aJsonObject* ctState = aJson.getObjectItem(parsedRoot, "ct");
  aJsonObject* xyState = aJson.getObjectItem(parsedRoot, "xy");
  if (xyState) {
    aJsonObject* elem0 = aJson.getArrayItem(xyState, 0);
    aJsonObject* elem1 = aJson.getArrayItem(xyState, 1);
    if (!elem0 || !elem1) {
      // XXX NOPE
    }
    HsbColor hsb = getXYtoRGB(elem0->valuefloat, elem1->valuefloat, newInfo.brightness);
    newInfo.hue = getHue(hsb);
    newInfo.saturation = getSaturation(hsb);
  } else if (ctState) {
    int mirek = ctState->valueint;
    if (mirek > 500 || mirek < 153) {
      // XXX error
    }

    HsbColor hsb = getMirektoRGB(mirek);
    newInfo.hue = getHue(hsb);
    newInfo.saturation = getSaturation(hsb);
  } else if (hueState || satState) {
    if (hueState) newInfo.hue = hueState->valueint;
    if (satState) newInfo.saturation = satState->valueint;
  }
  return newInfo;
}
