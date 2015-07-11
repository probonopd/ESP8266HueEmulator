/**
 * Emulate Philips Hue Bridge ; so far the Hue app finds the emulated Bridge and gets its config
 * and switch on 3 NeoPixels with it so far (TODO)
 **/

// TODO: Change SSDP to get rid of local copies of ESP8266SSDP use the new scheme from the Arduino IDE instead
// https://github.com/esp8266/Arduino/commit/f5ba04d46c0b6df75348f005e68411a856f89e48
// can then get rid of the SSDP.update();

// The following MUST be changed in aJSON.h, otherwise JSON will be cut off
// #define PRINT_BUFFER_LEN 2048

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiUDP.h>
#include "ESP8266SSDP.h"
#include <NeoPixelBus.h> // NeoPixelAnimator branch
#include <aJSON.h> // Replace avm/pgmspace.h with pgmspace.h there
#include "xy.h"
#include <math.h>


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

// Determines wheter a client is already authorized
bool isAuthorized = false;

byte mac[6]; // MAC address
String macString;
String ipString;

ESP8266WebServer HTTP(80);

// The username of the client (currently we authorize all clients simulating a pressed button on the bridge)
String client;

void sendJson(aJsonObject *root)
{
  // Take aJsonObject and print it to Serial and to WiFi
  // From https://github.com/pubnub/msp430f5529/blob/master/msp430f5529.ino
  char *msgStr = aJson.print(root);
  aJson.deleteItem(root);
  Serial.println(msgStr);
  HTTP.send(200, "text/plain", msgStr);
  free(msgStr);
}

void handleAllOthers() {
  Serial.println("===");
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
    for (int i = 1; i <= 8; i++) // FIXME: Why does this not work for more than 8?
      addLightJson(lights, i);
    aJsonObject *schedules;
    aJson.addItemToObject(root, "schedules", schedules = aJson.createObject());
    sendJson(root);

  }

  else if (requestedUri.endsWith("/api"))
    // On the real bridge, the link button on the bridge must have been recently pressed for the command to execute successfully.
    // We try to execute successfully regardless of a button for now.
  {
    isAuthorized = true; // FIXME: Instead, we should persist (save) the username and put it on the whitelist
    client = subStr(requestedUri.c_str(), "/", 2);
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
    Serial.print("Number of the light --> ");
    Serial.println(numberOfTheLight);
    aJsonObject* parsedRoot = aJson.parse(( char*) HTTP.arg("plain").c_str());
    aJsonObject* onState = aJson.getObjectItem(parsedRoot, "on");
    bool onValue = onState->valuebool;

    aJsonObject* hueState = aJson.getObjectItem(parsedRoot, "hue");
    int hue = hueState->valueint;
    aJsonObject* satState = aJson.getObjectItem(parsedRoot, "sat");
    int sat = satState->valueint;
    aJsonObject* briState = aJson.getObjectItem(parsedRoot, "bri");
    int bri = briState->valueint;
    RgbColor rgb;
    rgb = hsb2rgb(hue, sat, bri);


    aJson.deleteItem(parsedRoot);
    Serial.print("I should --> ");
    Serial.println(onValue);
    // define the effect to apply, in this case linear blend
    HslColor originalColor = strip.GetPixelColor(numberOfTheLight);
    if (onValue == true)
    {
      AnimUpdateCallback animUpdate = [ = ](float progress)
      {
        // progress will start at 0.0 and end at 1.0
        HslColor updatedColor = HslColor::LinearBlend(originalColor, rgb, progress);
        strip.SetPixelColor(numberOfTheLight, updatedColor);
      };
      animator.StartAnimation(numberOfTheLight, transitionTime, animUpdate);
    }
    else
    {
      AnimUpdateCallback animUpdate = [ = ](float progress)
      {
        // progress will start at 0.0 and end at 1.0
        HslColor updatedColor = HslColor::LinearBlend(originalColor, black, progress);
        strip.SetPixelColor(numberOfTheLight, updatedColor);
      };
      animator.StartAnimation(numberOfTheLight, transitionTime, animUpdate);
    }

    aJsonObject *root;
    root = aJson.createObject();
    addLightJson(root, numberOfTheLight);
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

void addConfigJson(aJsonObject *root)
{
  aJson.addStringToObject(root, "name", "Philips hue");
  aJson.addStringToObject(root, "swversion", "01005215");
  aJson.addBooleanToObject(root, "portalservices", false);
  aJson.addStringToObject(root, "zigbeechannel", "0"); // As per spec, 0 is allowed
  aJson.addStringToObject(root, "mac", macString.c_str());
  aJson.addBooleanToObject(root, "dhcp", true);
  aJson.addStringToObject(root, "ipaddress", ipString.c_str());
  aJson.addStringToObject(root, "netmask", "255.255.255.0"); // TODO: FIXME
  aJson.addStringToObject(root, "gateway", "192.168.0.1"); // TODO: FIXME
  aJson.addStringToObject(root, "proxyaddress", "");
  aJson.addNumberToObject(root, "proxyport", 0);
  aJson.addStringToObject(root, "UTC", "2012-10-29T12:05:00");
  aJsonObject *whitelist;
  aJson.addItemToObject(root, "whitelist", whitelist = aJson.createObject());
  aJsonObject *whitelistFirstEntry;
  aJson.addItemToObject(whitelist, client.c_str(), whitelistFirstEntry = aJson.createObject());
  aJson.addStringToObject(whitelistFirstEntry, "name", "clientname#devicename");
  aJson.addStringToObject(whitelistFirstEntry, "last use date", "2015-07-05T16:48:18");
  aJson.addStringToObject(whitelistFirstEntry, "create date", "2015-07-05T16:48:17");
  aJsonObject *swupdate;
  aJson.addItemToObject(root, "swupdate", swupdate = aJson.createObject());
  aJson.addStringToObject(swupdate, "text", "");
  aJson.addBooleanToObject(swupdate, "notify", false);
  aJson.addNumberToObject(swupdate, "updatestate", 0);
  aJson.addStringToObject(swupdate, "url", "");
}

void addLightJson(aJsonObject* root, int numberOfTheLight)
{
  String lightName = "" + (String) numberOfTheLight;
  aJsonObject *light;
  aJson.addItemToObject(root, lightName.c_str(), light = aJson.createObject());
  aJson.addStringToObject(light, "type", "Extended color light");
  aJson.addStringToObject(light, "name",  ("Hue Lamp " + (String) numberOfTheLight).c_str());
  aJson.addStringToObject(light, "modelid", "LCT001");
  aJsonObject *state;
  aJson.addItemToObject(light, "state", state = aJson.createObject());
  unsigned int brightness = strip.GetPixelColor(numberOfTheLight - 1).CalculateBrightness();
  if (brightness == 0)
  {
    aJson.addBooleanToObject(state, "on", false);
  }
  else
  {
    aJson.addBooleanToObject(state, "on", true);
  }
  aJson.addNumberToObject(state, "bri", 254); // Should be 1-254 according to Philips API
  aJson.addNumberToObject(state, "hue", 0); // Should between 0 and 65535. Both 0 and 65535 are red, 25500 is green and 46920 is blue
  aJson.addNumberToObject(state, "sat", 0);
  double numbers[2] = {0.0, 0.0};
  aJson.addItemToObject(state, "xy", aJson.createFloatArray(numbers, 2));
  aJson.addStringToObject(state, "alert", "none");
  aJson.addStringToObject(state, "effect", "none");
  aJson.addStringToObject(state, "colormode", "hs");
  aJson.addBooleanToObject(state, "reachable", true);

}

// Is this ever needed? So far it is not being used
// Based on http://stackoverflow.com/questions/22564187/rgb-to-philips-hue-hsb
// The code is based on this brilliant note: https://github.com/PhilipsHue/PhilipsHueSDK-iOS-OSX/commit/f41091cf671e13fe8c32fcced12604cd31cceaf3

struct xy getRGBtoXY(RgbColor color)
{
  struct xy xy_instance;

  // https://github.com/PhilipsHue/PhilipsHueSDK-iOS-OSX/commit/f41091cf671e13fe8c32fcced12604cd31cceaf3
  //-For the hue bulb the corners of the triangle are:
  //-Red: 0.675, 0.322
  //-Green: 0.4091, 0.518
  //-Blue: 0.167, 0.04
  //-
  //-For LivingColors Bloom, Aura and Iris the triangle corners are:
  //-Red: 0.704, 0.296
  //-Green: 0.2151, 0.7106
  //-Blue: 0.138, 0.08

  float normalizedToOneRed, normalizedToOneGreen, normalizedToOneBlue;

  normalizedToOneRed = (color.R / 255);
  normalizedToOneGreen = (color.G / 255);
  normalizedToOneBlue = (color.B / 255);

  float red, green, blue;

  // Make red more vivid
  if (normalizedToOneRed > 0.04045) {
    red = (float) pow(
            (normalizedToOneRed + 0.055) / (1.0 + 0.055), 2.4);
  } else {
    red = (float) (normalizedToOneRed / 12.92);
  }

  // Make green more vivid
  if (normalizedToOneGreen > 0.04045) {
    green = (float) pow((normalizedToOneGreen + 0.055)
                        / (1.0 + 0.055), 2.4);
  } else {
    green = (float) (normalizedToOneGreen / 12.92);
  }

  // Make blue more vivid
  if (normalizedToOneBlue > 0.04045) {
    blue = (float) pow((normalizedToOneBlue + 0.055)
                       / (1.0 + 0.055), 2.4);
  } else {
    blue = (float) (normalizedToOneBlue / 12.92);
  }

  float X = (float) (red * 0.649926 + green * 0.103455 + blue * 0.197109);
  float Y = (float) (red * 0.234327 + green * 0.743075 + blue * 0.022598);
  float Z = (float) (red * 0.0000000 + green * 0.053077 + blue * 1.035763);

  float x = X / (X + Y + Z);
  float y = Y / (X + Y + Z);

  xy_instance.x = x;
  Serial.print("x = ");
  Serial.println(x);
  xy_instance.y = y;
  Serial.print("y = ");
  Serial.println(y);
}

// Is this ever needed? So far it is not being used
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

// Is this ever needed? So far it is not being used
void rgb2hsb(int R, int G, int B)
{
  int hue, sat, bri;
  RgbColor rgb = RgbColor(R, G, B);
  HslColor hsl = HslColor(rgb);
  hue = floor(hsl.H * 65535);
  Serial.print("hue = ");
  Serial.println(hue);
  sat = floor(hsl.S * 255);
  Serial.print("sat = ");
  Serial.println(sat);
  bri = floor(hsl.L * 2.55);
  Serial.print("bri = ");
  Serial.println(bri);
}

RgbColor hsb2rgb(int hue, int sat, int bri)
{
  Serial.print("hue = ");
  Serial.println(hue);
  Serial.print("sat = ");
  Serial.println(sat);
  Serial.print("bri = ");
  Serial.println(bri);

  double H, S, L;
  H = (hue / 65535) + 0.5;
  Serial.print("H = ");
  Serial.println(H);
  S = (sat / 255) ;
  Serial.print("S = ");
  Serial.println(S);
  L = bri / 2.55;
  Serial.print("L = ");
  Serial.println(L);
  HslColor hsl = HslColor(H, S, L);
  
  RgbColor rgb = RgbColor(hsl);
  Serial.print("R = ");
  Serial.println(rgb.R);
  Serial.print("G = ");
  Serial.println(rgb.G);
  Serial.print("B = ");
  Serial.println(rgb.B);
  Serial.println("FIXME: This is not quite working yet - why?");
  
  return rgb;
}

