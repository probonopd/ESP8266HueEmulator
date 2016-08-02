/**
 * Emulate Philips Hue Bridge ; so far the Hue app finds the emulated Bridge and gets its config
 * and switch NeoPixels with it
 **/

#include <ESP8266WiFi.h>
#include <NeoPixelBus.h>
#include <NeoPixelAnimator.h> // instead of NeoPixelAnimator branch
#include "LightService.h"

// these are only used in LightHandler.cpp, but it seems that the IDE only scans the .ino and real libraries for dependencies
#include <ESP8266WebServer.h>
#include <ESP8266SSDP.h>
#include <aJSON.h> // Replace avm/pgmspace.h with pgmspace.h there and set #define PRINT_BUFFER_LEN 4096 ################# IMPORTANT

#include "/secrets.h" // Delete this line and populate the following
//const char* ssid = "********";
//const char* password = "********";

RgbColor red = RgbColor(COLOR_SATURATION, 0, 0);
RgbColor green = RgbColor(0, COLOR_SATURATION, 0);
RgbColor white = RgbColor(COLOR_SATURATION);
RgbColor black = RgbColor(0);
unsigned int transitionTime = 800; // by default there is a transition time to the new state of 400 milliseconds

// Settings for the NeoPixels
#define pixelCount 30
#define pixelPin 2 // Strip is attached to GPIO2 on ESP-01
NeoPixelBus<NeoGrbFeature, NeoEsp8266Uart800KbpsMethod> strip(pixelCount, pixelPin);
NeoPixelAnimator animator(pixelCount, NEO_MILLISECONDS); // NeoPixel animation management object

HsbColor getHsb(int hue, int sat, int bri) {
  float H, S, B;
  H = hue / 182.04 / 360.0;
  S = sat / COLOR_SATURATION;
  B = bri / COLOR_SATURATION;
  return HsbColor(H, S, B);
}

class PixelHandler : public LightHandler {
  private:
    HueLightInfo _info;
  public:
    void handleQuery(int lightNumber, HueLightInfo newInfo) {
      // define the effect to apply, in this case linear blend
      HslColor newColor = HslColor(getHsb(newInfo.hue, newInfo.saturation, newInfo.brightness));
      HslColor originalColor = strip.GetPixelColor(lightNumber);
      _info = newInfo;
      if (newInfo.on)
      {
        AnimUpdateCallback animUpdate = [ = ](const AnimationParam & param)
        {
          // progress will start at 0.0 and end at 1.0
          HslColor updatedColor = HslColor::LinearBlend<NeoHueBlendShortestDistance>(originalColor, newColor, param.progress);
          strip.SetPixelColor(lightNumber, updatedColor);
        };
        animator.StartAnimation(lightNumber, transitionTime, animUpdate);
      }
      else
      {
        AnimUpdateCallback animUpdate = [ = ](const AnimationParam & param)
        {
          // progress will start at 0.0 and end at 1.0
          HslColor updatedColor = HslColor::LinearBlend<NeoHueBlendShortestDistance>(originalColor, black, param.progress);
          strip.SetPixelColor(lightNumber, updatedColor);
        };
        animator.StartAnimation(lightNumber, transitionTime, animUpdate);
      }
    }

    HueLightInfo getInfo(int lightNumber) { return _info; }
};

void setup() {

  // this resets all the neopixels to an off state
  strip.Begin();
  strip.Show();

  // Show that the NeoPixels are alive
  delay(120); // Apparently needed to make the first few pixels animate correctly
  infoLight(white);

  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("WiFi Failed");
    // Show that we are connected
    infoLight(red);
    while (1) delay(100);
  }

  // Show that we are connected
  infoLight(green);

  LightService.begin();

  // setup pixels as lights
  for (int i = 0; i < MAX_LIGHT_HANDLERS && i < pixelCount; i++) {
    LightService.setLightHandler(i, new PixelHandler());
  }
}

void loop() {
  LightService.update();

  static unsigned long update_strip_time = 0;  //  keeps track of pixel refresh rate... limits updates to 33 Hz
  if (millis() - update_strip_time > 30)
  {
    if ( animator.IsAnimating() ) animator.UpdateAnimations();
    strip.Show();
    update_strip_time = millis();
  }
}

void infoLight(RgbColor color) {
  // Flash the strip in the selected color. White = booted, green = WLAN connected, red = WLAN could not connect
  for (int i = 0; i < pixelCount; i++)
  {
    strip.SetPixelColor(i, color);
    strip.Show();
    delay(10);
    strip.SetPixelColor(i, black);
    strip.Show();
  }
}
