enum HueColorType {
  TYPE_HUE_SAT, TYPE_CT, TYPE_XY
};

enum HueAlert {
  ALERT_NONE, ALERT_SELECT, ALERT_LSELECT
};

enum HueEffect {
  EFFECT_NONE, EFFECT_COLORLOOP
};

struct HueLightInfo {
  bool on = false;
  int brightness = 0;
  HueColorType type = TYPE_HUE_SAT;
  int hue = 0, saturation = 0;
  HueAlert alert = ALERT_NONE;
  HueEffect effect = EFFECT_NONE;
};

class aJsonObject;
bool parseHueLightInfo(HueLightInfo currentInfo, aJsonObject *parsedRoot, HueLightInfo *newInfo);

class LightHandler {
  public:
    // These functions include light number as a single LightHandler could conceivably service several lights
    virtual void handleQuery(int lightNumber, HueLightInfo info, aJsonObject* raw) {}
    virtual HueLightInfo getInfo(int lightNumber) {
      HueLightInfo info;
      return info;
    }
};

// Max number of exposed lights is directly related to aJSON PRINT_BUFFER_LEN, 14 for 4096
#define MAX_LIGHT_HANDLERS 6
#define COLOR_SATURATION 254

class LightServiceClass {
    public:
      LightHandler *getLightHandler(int numberOfTheLight);
      bool setLightsAvailable(int numLights);
      int getLightsAvailable();
      bool setLightHandler(int index, LightHandler *handler);
      void begin();
      void update();
    private:
      int currentNumLights = MAX_LIGHT_HANDLERS;
};

extern LightServiceClass LightService;
