/**
 * Emulate Philips Hue Bridge ; so far the Hue app finds the emulated Bridge and gets its config
 * and switch on 3 NeoPixels with it so far (TODO)
 **/

// TODO: Change SSDP to get rid of local copies of ESP8266SSDP use the new scheme from the Arduino IDE instead
// https://github.com/esp8266/Arduino/commit/f5ba04d46c0b6df75348f005e68411a856f89e48
// can then get rid of the SSDP.update();

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiUDP.h>
#include "ESP8266SSDP.h"
#include <NeoPixelBus.h> // NeoPixelAnimator branch
#include <ArduinoJson.h>

#include "/secrets.h" // Delete this line and populate the following
//const char* ssid = "********";
//const char* password = "********";

// Settings for the NeoPixels
#define pixelCount 30 // Strip has 30 NeoPixels
#define pixelPin 2 // Strip is attached to GPIO2 on ESP-01
#define colorSaturation 254
RgbColor red = RgbColor(colorSaturation, 0, 0);
RgbColor green = RgbColor(0, colorSaturation, 0);
RgbColor blue = RgbColor(0, 0, colorSaturation);
RgbColor white = RgbColor(colorSaturation);
RgbColor black = RgbColor(0);
unsigned int transitionTime = 800; // by default there is a transition time to the new state of 400 milliseconds

NeoPixelBus strip = NeoPixelBus(pixelCount, pixelPin);
NeoPixelAnimator animator(&strip); // NeoPixel animation management object

/// X, Y, Z and x, y are values to desvribe colors
float X;
float Y;
float Z;
float x;
float y;

// Determines wheter a client is already authorized
bool isAuthorized = false;

byte mac[6]; // MAC address
String macString;
String ipString;

ESP8266WebServer HTTP(80);

String client = "e7x4kuCaC8h885jo"; // The client name that the sketch gives to the app
// FIXME: Parse this out of what is being sent by the app.

void handleAllOthers() {
  Serial.println("===");
  String requestedUri = HTTP.uri();
  Serial.print("requestedUri: ");
  Serial.println(requestedUri);

  if ( requestedUri.endsWith("/config") )
  {

    StaticJsonBuffer<1024> jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();
    addConfigJson(root);

    root.prettyPrintTo(Serial);
    WiFiClient client = HTTP.client();
    root.prettyPrintTo((Print&)client); // Thanks me-no-dev for the "(Print&)"

  }

  else if ((requestedUri.startsWith("/api") and (requestedUri.lastIndexOf("/") == 4 )))
  {
    // Serial.println("Respond with complete json as in https://github.com/probonopd/ESP8266HueEmulator/wiki/Hue-API#get-all-information-about-the-bridge");

    StaticJsonBuffer<2048> jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();
    JsonObject& groups = root.createNestedObject("groups");
    JsonObject& scenes = root.createNestedObject("scenes");
    JsonObject& config = root.createNestedObject("config");
    addConfigJson(config);
    JsonObject& lights = root.createNestedObject("lights");
    addLightJson(lights, 1); // I think this is prone to crashes; see https://github.com/bblanchon/ArduinoJson/issues/87
//    for (int i = 1; i < 2; i++)
//    {
//      addLightJson(root, i); // This crashes when i>1; see https://github.com/bblanchon/ArduinoJson/issues/87
//    }
    JsonObject& schedules = root.createNestedObject("schedules");
    root.prettyPrintTo(Serial);
    WiFiClient client = HTTP.client();
    root.prettyPrintTo((Print&)client); // Thanks me-no-dev for the "(Print&)"
  }

  else if (requestedUri.endsWith("/api"))
    // On the real bridge, the link button on the bridge must have been recently pressed for the command to execute successfully.
    // We try to execute successfully regardless of a button for now.
  {
    isAuthorized = true; // FIXME: Instead, we should persist (save) the username and put it on the whitelist
    String str = "[{\"success\":{\"username\": \"" + client + "\"}}]";
    HTTP.send(200, "text/plain", str);
    Serial.println(str);
  }

  else if (requestedUri.endsWith("/state"))
  {
    // For this to work we need a patched version of esp8266/libraries/ESP8266WebServer/src/Parsing.cpp which hopefully lands in the official channel soon
    // https://github.com/me-no-dev/Arduino/blob/d4894b115e3bbe753a47b1645a55cab7c62d04e2/hardware/esp8266com/esp8266/libraries/ESP8266WebServer/src/Parsing.cpp
    Serial.println(HTTP.arg("plain"));
    int numberOfTheLight = atoi(subStr(requestedUri.c_str(), "/", 4)) - 1; // The number of the light to be switched; they start with 1
    Serial.print("Number of the light --> ");
    Serial.println(numberOfTheLight);

    StaticJsonBuffer<200> jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(( char*) HTTP.arg("plain").c_str());
    if (!root.success()) {
      Serial.println("parseObject() failed");
      return;
    }
    bool onValue = root["on"];
    Serial.print("I should --> ");
    Serial.println(onValue);
    // define the effect to apply, in this case linear blend
    HslColor originalColor = strip.GetPixelColor(numberOfTheLight);

    if (onValue == true)
    {
      AnimUpdateCallback animUpdate = [ = ](float progress)
      {
        // progress will start at 0.0 and end at 1.0
        HslColor updatedColor = HslColor::LinearBlend(originalColor, white, progress);
        strip.SetPixelColor(numberOfTheLight, updatedColor);
      };
      animator.StartAnimation(numberOfTheLight, transitionTime, animUpdate);
    }
    if (onValue == false)
    {
      AnimUpdateCallback animUpdate = [ = ](float progress)
      {
        // progress will start at 0.0 and end at 1.0
        HslColor updatedColor = HslColor::LinearBlend(originalColor, black, progress);
        strip.SetPixelColor(numberOfTheLight, updatedColor);
      };
      animator.StartAnimation(numberOfTheLight, transitionTime, animUpdate);
    }

    JsonObject& lightJson = jsonBuffer.createObject();
    addLightJson(lightJson, numberOfTheLight);
    // lightJson.printTo(HTTP.client()); // FIXME: Why is this not working? Gives "no matching function for call to 'ArduinoJson::JsonObject::printTo(WiFiClient)'"
    char buffer[256];
    lightJson.printTo(buffer, sizeof(buffer));
    HTTP.send(200, "text/plain", buffer);
    Serial.println(buffer);
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
  ipString = StringIPaddress();

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
  SSDP.setModelName((char*)"Philips hue bridge 2012");
  SSDP.setModelNumber((char*)"929000226503");
  SSDP.setModelURL((char*)"http://www.meethue.com");
  SSDP.setManufacturer((char*)"Royal Philips Electronics");
  SSDP.setManufacturerURL((char*)"http://www.philips.com");
  Serial.println("SSDP Started");
}

void loop() {
  // FIXME: This seems to block everything while a request is being processed which takes about 2 seconds
  // Can we run this in a separate thread, in "the background"?
  // Makuna has a task library that "helps" manage non-preemptive tasks, but it would require
  // that HTTP.handleClient() and/or SSDP.update() be modified to do less in a loop at one time.
  // We might bring this question up on the esp8266/arduino chat to see if there is support
  // to thread off the networking stuff; but in general, we don't have a multitasking core.
  HTTP.handleClient();
  SSDP.update();

  static unsigned long update_strip_time = 0;  //  keeps track of pixel refresh rate... limits updates to 33 Hz
  if (millis() - update_strip_time > 30)
  {
    if ( animator.IsAnimating() ) animator.UpdateAnimations(100);
    strip.Show();
    update_strip_time = millis();
  }
}

void rgb2xy(int R, int G, int B)
{
  // Convert the RGB values to XYZ using the Wide RGB D65 conversion formula
  float X = R * 0.664511f + G * 0.154324f + B * 0.162028f;
  float Y = R * 0.283881f + G * 0.668433f + B * 0.047685f;
  float Z = R * 0.000088f + G * 0.072310f + B * 0.986039f;

  // Calculate the xy values from the XYZ values
  float x = X / (X + Y + Z);
  float y = Y / (X + Y + Z);
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
  for (i = 1, act = copy; i <= index; i++, act = NULL) {
    //Serial.print(".");
    sub = strtok_r(act, delim, &ptr);
    if (sub == NULL) break;
  }
  return sub;
}

String StringIPaddress()
{
  String LocalIP = "";
  IPAddress myaddr = WiFi.localIP();
  for (int i = 0; i < 4; i++)
  {
    LocalIP += String(myaddr[i]);
    if (i < 3) LocalIP += ".";
  }
  return LocalIP;
}

void addConfigJson(JsonObject& root)
{
  root["name"] = "Philips hue";
  root["swversion"] = "01005215";
  root["portalservices"] = false;
  root["zigbeechannel"] = "0"; // As per spec, 0 is allowed
  root["mac"] = macString.c_str();
  root["dhcp"] = "true";
  root["ipaddress"] = ipString.c_str();
  root["netmask"] = "255.255.255.0"; // TODO: FIXME
  root["gateway"] = "192.168.0.1"; // TODO: FIXME
  root["proxyaddress"] = "";
  root["proxyport"] = 0;
  root["UTC"] = "2012-10-29T12:05:00";
  JsonObject& whitelist = root.createNestedObject("whitelist");
  JsonObject& whitelistFirstEntry = whitelist.createNestedObject("e7x4kuCaC8h885jo"); // FIXME: Do not hardcode e7x4kuCaC8h885jo
  whitelistFirstEntry["name"] = "clientname#devicename";
  whitelistFirstEntry["last use date"] = "2015-07-05T16:48:18";
  whitelistFirstEntry["create date"] = "2015-07-05T16:48:17";
  JsonObject& swupdate = root.createNestedObject("swupdate");
  swupdate["text"] = "";
  swupdate["notify"] = false;
  swupdate["updatestate"] = 0;
  swupdate["url"] = "";
}

void addLightJson(JsonObject& root, int numberOfTheLight)
{
  String lightName = "" + (String) numberOfTheLight;
  JsonObject& light = root.createNestedObject(lightName.c_str());
  light["type"] = "Extended color light";
  light["name"] =  ("Hue Lamp " + (String) numberOfTheLight).c_str();
  light["modelid"] = "LCT001";
  JsonObject& state = light.createNestedObject("state");
  unsigned int brightness = strip.GetPixelColor(numberOfTheLight - 1).CalculateBrightness();
  if (brightness == 0)
  {
    state["on"] = false;
  }
  else
  {
    state["on"] = true;
  }
  state["bri"] = 254; // Can be 0-255 but should be 1-254 according to Philips API (why?)
  state["hue"] = 0; // Should between 0 and 65535. Both 0 and 65535 are red, 25500 is green and 46920 is blue.
  state["sat"] = 0;
  JsonArray& array = state.createNestedArray("xy");
  array.add(0.0);
  array.add(0.0);
  state["alert"] = "none";
  state["effect"] = "none";
  state["colormode"] = "hs";
  state["reachable"] = true;

}



