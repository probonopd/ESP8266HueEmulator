/**
 * Emulate Philips Hue Bridge ; so far the Hue app finds the emulated Bridge and gets its config
 * and switch NeoPixels with it
 **/

// TODO: Change SSDP to get rid of local copies of ESP8266SSDP use the new scheme from the Arduino IDE instead
// https://github.com/esp8266/Arduino/commit/f5ba04d46c0b6df75348f005e68411a856f89e48
// can then get rid of the SSDP.update();

// The following MUST be changed in aJSON.h, otherwise JSON will be cut off
// #define PRINT_BUFFER_LEN 2048

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <ESP8266SSDP.h>
#include <NeoPixelBus.h>
#include <NeoPixelAnimator.h> // instead of NeoPixelAnimator branch
#include <aJSON.h> // Replace avm/pgmspace.h with pgmspace.h there and set #define PRINT_BUFFER_LEN 4096 ################# IMPORTANT
#include "LightHandler.h"
#include <math.h>

#include "/secrets.h" // Delete this line and populate the following
//const char* ssid = "********";
//const char* password = "********";

// Settings for the NeoPixels
#define pixelCount 6 // Max number of exposed lights is directly related to aJSON PRINT_BUFFER_LEN, 14 for 4096
#define pixelPin 2 // Strip is attached to GPIO2 on ESP-01
RgbColor red = RgbColor(colorSaturation, 0, 0);
RgbColor green = RgbColor(0, colorSaturation, 0);
RgbColor white = RgbColor(colorSaturation);
RgbColor black = RgbColor(0);
unsigned int transitionTime = 800; // by default there is a transition time to the new state of 400 milliseconds

NeoPixelBus<NeoGrbFeature, NeoEsp8266Uart800KbpsMethod> strip(pixelCount, pixelPin);
NeoPixelAnimator animator(pixelCount, NEO_MILLISECONDS); // NeoPixel animation management object

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

// Determines whether a client is already authorized
bool isAuthorized = false;

byte mac[6]; // MAC address
String macString;
String ipString;
String netmaskString;
String gatewayString;

ESP8266WebServer HTTP(80);

// The username of the client (currently we authorize all clients simulating a pressed button on the bridge)
String client;

void sendJson(aJsonObject *root)
{
  // Take aJsonObject and print it to Serial and to WiFi
  // From https://github.com/pubnub/msp430f5529/blob/master/msp430f5529.ino
  char *msgStr = aJson.print(root);
  aJson.deleteItem(root);
  Serial.println(millis());
  Serial.println(msgStr);
  HTTP.send(200, "text/plain", msgStr);
  free(msgStr);
}

void handleAllOthers() {
  Serial.println("===");
  Serial.println(millis());
  String requestedUri = HTTP.uri();
  Serial.print("requestedUri: ");
  Serial.println(requestedUri);

  if ( requestedUri.endsWith("/config") )
  {
    aJsonObject *root;
    root = aJson.createObject();
    addConfigJson(root);
    sendJson(root);
  }

  else if ((requestedUri.startsWith("/api") and (requestedUri.lastIndexOf("/") == 4 )))
  {
    // Serial.println("Respond with complete json as in https://github.com/probonopd/ESP8266HueEmulator/wiki/Hue-API#get-all-information-about-the-bridge");
    aJsonObject *root;
    root = aJson.createObject();
    aJsonObject *groups;
    aJson.addItemToObject(root, "groups", groups = aJson.createObject());
    aJsonObject *scenes;
    aJson.addItemToObject(root, "scenes", scenes = aJson.createObject());
    aJsonObject *config;
    aJson.addItemToObject(root, "config", config = aJson.createObject());
    addConfigJson(config);
    aJsonObject *lights;
    aJson.addItemToObject(root, "lights", lights = aJson.createObject());
    for (int i = 0; i < MAX_LIGHT_HANDLERS; i++)
      addLightJson(lights, i, getLightHandler(i));
    aJsonObject *schedules;
    aJson.addItemToObject(root, "schedules", schedules = aJson.createObject());
    sendJson(root);
  }

  else if (requestedUri.endsWith("/api"))
    // On the real bridge, the link button on the bridge must have been recently pressed for the command to execute successfully.
    // We try to execute successfully regardless of a button for now.
  {
    isAuthorized = true; // FIXME: Instead, we should persist (save) the username and put it on the whitelist
    client = subStr(requestedUri.c_str(), "/", 1);
    Serial.println("CLIENT: ");
    Serial.println(client);

    String str = "[{\"success\":{\"username\": \"" + client + "\"}}]";
    HTTP.send(200, "text/plain", str);
    Serial.println(str);
  }

  else if (requestedUri.endsWith("/state"))
  {
    // For this to work we need a patched version of esp8266/libraries/ESP8266WebServer/src/Parsing.cpp which hopefully lands in the official channel soon
    // https://github.com/me-no-dev/Arduino/blob/d4894b115e3bbe753a47b1645a55cab7c62d04e2/hardware/esp8266com/esp8266/libraries/ESP8266WebServer/src/Parsing.cpp
    if (HTTP.arg("plain") == "")
    {
      Serial.println("You need to use a newer version of the ESP8266WebServer library from https://github.com/me-no-dev/Arduino/blob/d4894b115e3bbe753a47b1645a55cab7c62d04e2/hardware/esp8266com/esp8266/libraries/ESP8266WebServer/src/Parsing.cpp");
      yield();
    }
    Serial.println(HTTP.arg("plain"));
    int numberOfTheLight = atoi(subStr(requestedUri.c_str(), "/", 4)) - 1; // The number of the light to be switched; they start with 1
    if (numberOfTheLight == -1) {
      numberOfTheLight = atoi(subStr(requestedUri.c_str(), "/", 3)) - 1;
    }
    Serial.print("Number of the light --> ");
    Serial.println(numberOfTheLight);
    aJsonObject* parsedRoot = aJson.parse(( char*) HTTP.arg("plain").c_str());
    LightHandler *handler = getLightHandler(numberOfTheLight);
    if (!handler) {
      // XXX throw an error?
    }
    HueLightInfo currentInfo = handler->getInfo(numberOfTheLight);
    if (parsedRoot) {
      HueLightInfo newInfo = parseHueLightInfo(currentInfo, parsedRoot);
      aJson.deleteItem(parsedRoot);
      handler->handleQuery(numberOfTheLight, newInfo);
    }

    aJsonObject *root;
    root = aJson.createObject();
    addLightJson(root, numberOfTheLight, handler);
    sendJson(root);
  }

  else if (requestedUri == "/description.xml")
  {
    WiFiClient client = HTTP.client();
    String str = "<root><specVersion><major>1</major><minor>0</minor></specVersion><URLBase>http://" + ipString + ":80/</URLBase><device><deviceType>urn:schemas-upnp-org:device:Basic:1</deviceType><friendlyName>Philips hue (" + ipString + ")</friendlyName><manufacturer>Royal Philips Electronics</manufacturer><manufacturerURL>http://www.philips.com</manufacturerURL><modelDescription>Philips hue Personal Wireless Lighting</modelDescription><modelName>Philips hue bridge 2012</modelName><modelNumber>929000226503</modelNumber><modelURL>http://www.meethue.com</modelURL><serialNumber>00178817122c</serialNumber><UDN>uuid:2f402f80-da50-11e1-9b23-00178817122c</UDN><presentationURL>index.html</presentationURL><iconList><icon><mimetype>image/png</mimetype><height>48</height><width>48</width><depth>24</depth><url>hue_logo_0.png</url></icon><icon><mimetype>image/png</mimetype><height>120</height><width>120</width><depth>24</depth><url>hue_logo_3.png</url></icon></iconList></device></root>";
    HTTP.send(200, "text/plain", str);
    Serial.println(str);
    Serial.println("I assume this is working since with this, Chroma for Hue finds a Bridge, so does the Hue iOS app. In constrast, without this they say no Bridge found.");
  }

  else
  {
    HTTP.send(404, "text/plain", "File not found");
    Serial.println("FIXME: To be implemented");

    // Print what the client has POSTed
    for (uint8_t i = 0; i < HTTP.args(); i++) Serial.printf("ARG[%u]: %s=%s\n", i, HTTP.argName(i).c_str(), HTTP.arg(i).c_str());

  }
  Serial.println(millis());
}

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

  macString = String(WiFi.macAddress());
  ipString = StringIPaddress(WiFi.localIP());
  netmaskString = StringIPaddress(WiFi.subnetMask());
  gatewayString = StringIPaddress(WiFi.gatewayIP());

  Serial.print("Starting HTTP at ");
  Serial.print(WiFi.localIP());
  Serial.print(":");
  Serial.println(80);

  HTTP.onNotFound(handleAllOthers);

  HTTP.begin();

  // Show that we are connected
  infoLight(green);

  Serial.printf("Starting SSDP...\n");
  SSDP.begin();
  SSDP.setSchemaURL((char*)"description.xml");
  SSDP.setHTTPPort(80);
  SSDP.setName((char*)"Philips hue clone");
  SSDP.setSerialNumber((char*)"001788102201");
  SSDP.setURL((char*)"index.html");
  SSDP.setModelName((char*)"IpBridge");
  SSDP.setModelNumber((char*)"0.1");
  SSDP.setModelURL((char*)"http://www.meethue.com");
  SSDP.setManufacturer((char*)"Royal Philips Electronics");
  SSDP.setManufacturerURL((char*)"http://www.philips.com");
  SSDP.setDeviceType((char*)"upnp:rootdevice");
  Serial.println("SSDP Started");

  // setup pixels as lights
  for (int i = 0; i < MAX_LIGHT_HANDLERS && i < pixelCount; i++) {
    setLightHandler(i, new PixelHandler());
  }
}

void loop() {
  HTTP.handleClient();

  static unsigned long update_strip_time = 0;  //  keeps track of pixel refresh rate... limits updates to 33 Hz
  if (millis() - update_strip_time > 30)
  {
    if ( animator.IsAnimating() ) animator.UpdateAnimations();
    strip.Show();
    update_strip_time = millis();
  }
}

// See https://github.com/PhilipsHue/PhilipsHueSDK-iOS-OSX/commit/00187a3db88dedd640f5ddfa8a474458dff4e1db for more needed color conversions

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

// Function to return a substring defined by a delimiter at an index
// From http://forum.arduino.cc/index.php?topic=41389.msg301116#msg301116
char* subStr(const char* str, char *delim, int index) {
  char *act, *sub, *ptr;
  static char copy[128]; // Length defines the maximum length of the c_string we can process
  int i;
  strcpy(copy, str); // Since strtok consumes the first arg, make a copy
  for (i = 1, act = copy; i <= index; i++, act = nullptr) {
    //Serial.print(".");
    sub = strtok_r(act, delim, &ptr);
    if (!sub) break;
  }
  return sub;
}

String StringIPaddress(IPAddress myaddr)
{
  String LocalIP = "";
  for (int i = 0; i < 4; i++)
  {
    LocalIP += String(myaddr[i]);
    if (i < 3) LocalIP += ".";
  }
  return LocalIP;
}

void addConfigJson(aJsonObject *root)
{
  aJson.addStringToObject(root, "name", "hue emulator");
  aJson.addStringToObject(root, "swversion", "0.1");
  // aJson.addBooleanToObject(root, "portalservices", false);
  // aJson.addStringToObject(root, "zigbeechannel", "0"); // As per spec, 0 is allowed
  aJson.addStringToObject(root, "mac", macString.c_str());
  aJson.addBooleanToObject(root, "dhcp", true);
  aJson.addStringToObject(root, "ipaddress", ipString.c_str());
  aJson.addStringToObject(root, "netmask", netmaskString.c_str());
  aJson.addStringToObject(root, "gateway", gatewayString.c_str());
  aJsonObject *whitelist;
  aJson.addItemToObject(root, "whitelist", whitelist = aJson.createObject());
  aJsonObject *whitelistFirstEntry;
  aJson.addItemToObject(whitelist, client.c_str(), whitelistFirstEntry = aJson.createObject());
  aJson.addStringToObject(whitelistFirstEntry, "name", "clientname#devicename");
  aJsonObject *swupdate;
  aJson.addItemToObject(root, "swupdate", swupdate = aJson.createObject());
  aJson.addStringToObject(swupdate, "text", "");
  aJson.addBooleanToObject(swupdate, "notify", false); // Otherwise client app shows update notice
  aJson.addNumberToObject(swupdate, "updatestate", 0);
  aJson.addStringToObject(swupdate, "url", "");
}

HsbColor getHsb(int hue, int sat, int bri) {
  float H, S, B;
  H = hue / 182.04 / 360.0;
  S = sat / colorSaturation;
  B = bri / colorSaturation;
  return HsbColor(H, S, B);
}
