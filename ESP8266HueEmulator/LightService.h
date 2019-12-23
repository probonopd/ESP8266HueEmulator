#include <Arduino.h>
#include <ESP8266WebServer.h>

enum HueColorType {
  TYPE_HUE_SAT, TYPE_CT, TYPE_XY
};

enum HueAlert {
  ALERT_NONE, ALERT_SELECT, ALERT_LSELECT
};

enum HueEffect {
  EFFECT_NONE, EFFECT_COLORLOOP
};

enum class HueBulbType {
  EXTENDED_COLOR_LIGHT, DIMMABLE_LIGHT
};


struct HueLightInfo {
  bool on = false;
  int brightness = 0;
  HueColorType type = TYPE_HUE_SAT;
  HueBulbType bulbType = HueBulbType::EXTENDED_COLOR_LIGHT;
  int hue = 0, saturation = 0;
  HueAlert alert = ALERT_NONE;
  HueEffect effect = EFFECT_NONE;
  unsigned int transitionTime = 800; // by default there is a transition time to the new state of 400 milliseconds
};

class aJsonObject;
class String;
bool parseHueLightInfo(HueLightInfo currentInfo, aJsonObject *parsedRoot, HueLightInfo *newInfo);

class LightHandler {
  public:
    // These functions include light number as a single LightHandler could conceivably service several lights
    virtual void handleQuery(int lightNumber, HueLightInfo info, aJsonObject* raw) {}
    virtual HueLightInfo getInfo(int lightNumber) {
      HueLightInfo info;
      return info;
    }
    virtual String getFriendlyName(int lightNumber) const {
      return "Hue LightStrips " + ((String) (lightNumber + 1));
    }
};

// Max number of exposed lights is directly related to aJSON PRINT_BUFFER_LEN, 14 for 4096
#define MAX_LIGHT_HANDLERS 2
#define COLOR_SATURATION 255.0f

class LightServiceClass {
    const char* _friendlyName;

    public:
      LightServiceClass();
      LightHandler *getLightHandler(int numberOfTheLight);
      bool setLightsAvailable(int numLights);
      int getLightsAvailable();
      bool setLightHandler(int index, LightHandler *handler);
      void begin();
      void begin(ESP8266WebServer *svr);
      void update();
    private:
      int currentNumLights = MAX_LIGHT_HANDLERS;
};

extern LightServiceClass LightService;
