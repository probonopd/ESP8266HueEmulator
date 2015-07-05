/**
 * Emulate Philips Hue Bridge ; so far the Hue app finds the emulated Bridge and
 * requests URLs like /api/UjBZ0nvTLu7aMdOe but in the end it does not succeed "finding" the emulated bridge
 * (even if the app is talking to it)
 **/


// To get to the arguments sent by the HTTP client, use:
// for (uint8_t i=0; i<server.args();i++) Serial.printf("ARG[%u]: %s=%s\n", i, server.argName(i), server.arg(i));


#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiUDP.h>
#include "ESP8266SSDP.h"
#include <NeoPixelBus.h>
#include <ArduinoJson.h>

// The follwing is needed in order to fill ipString with a String that contains the IP address
extern "C" {
#include "ip_addr.h"
}
char ipChars[16];

#include "/secrets.h" // Delete this line and populate the following
//const char* ssid = "********";
//const char* password = "********";

// Settings for the NeoPixels
#define pixelCount 30 // Strip has 30 NeoPixels
#define colorSaturation 128
NeoPixelBus strip = NeoPixelBus(pixelCount, 2); // Strip is attached to GPIO2 on ESP-01
RgbColor red = RgbColor(colorSaturation, 0, 0);
RgbColor green = RgbColor(0, colorSaturation, 0);
RgbColor blue = RgbColor(0, 0, colorSaturation);
RgbColor white = RgbColor(colorSaturation);
RgbColor black = RgbColor(0);

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


String client = "e7x4kuCaC8h885jo"; // "UjBZ0nvTLu7aMdOe"; // The client string that the client app sends. Need to enter here what the app sends.
// FIXME: Parse this out of what is being sent by the app.

String getLightString(int lightNumber) {
  String lightString = "\"" + String(lightNumber) + "\": {"
                       "\"state\": {"
                       "\"on\": true,"
                       "\"bri\": 144,"
                       "\"hue\": 13088,"
                       "\"sat\": 212,"
                       "\"xy\": [0.5128,0.4147],"
                       "\"ct\": 467,"
                       "\"alert\": \"none\","
                       "\"effect\": \"none\","
                       "\"colormode\": \"xy\","
                       "\"reachable\": true"
                       "},"
                       "\"type\": \"Extended color light\","
                       "\"name\": \"Hue Lamp 1\","
                       "\"modelid\": \"LCT001\","
                       "\"swversion\": \"66009461\","
                       "\"pointsymbol\": {"
                       "\"1\": \"none\","
                       "\"2\": \"none\","
                       "\"3\": \"none\","
                       "\"4\": \"none\","
                       "\"5\": \"none\","
                       "\"6\": \"none\","
                       "\"7\": \"none\","
                       "\"8\": \"none\""
                       "}"
                       "}";
  return lightString;
}


void handleAllOthers() {
  Serial.println("===");
  String requestedUri = HTTP.uri();
  Serial.print("requestedUri: ");
  Serial.println(requestedUri);

String longstr = "{" // No [] around this one!
                     " \"name\": \"Philips hue\","
                     " \"zigbeechannel\": 0," // As per spec, 0 is allowed
                     " \"mac\": \"" + macString + "\","
                     " \"dhcp\": true,"
                     " \"ipaddress\": \"" + ipString + "\","
                     " \"netmask\": \"255.255.255.0\"," // TODO: FIXME
                     " \"gateway\": \"192.168.0.1\"," // TODO: FIXME
                     " \"proxyaddress\": \"none\","
                     " \"proxyport\": 0,"
                     " \"UTC\": \"2014-07-17T09:27:35\","
                     //" \"localtime\": \"2014-07-17T11:27:35\"," // This was added in a bridge update at the beginning of 2014
                     " \"timezone\": \"\","
                     //" \"timezone\": \"Europe/Berlin\","
                     " \"whitelist\": {"
                     " \"" + client + "\": {"
                     " \"last use date\": \"2015-06-17T07:21:38\","
                     " \"create date\": \"2014-04-08T08:55:10\","
                     " \"name\": \"Chroma for Hue\""
                     " }"
                     //  " \"pAtwdCV8NZId25Gk\": {"
                     //   " \"last use date\": \"2014-05-07T18:28:29\","
                     //   " \"create date\": \"2014-04-09T17:29:16\","
                     //   " \"name\": \"MyApplication\""
                     //   " },"
                     //  " \"gDN3IaPYSYNPWa2H\": {"
                     //  " \"last use date\": \"2014-05-07T09:15:21\","
                     //  " \"create date\": \"2014-05-07T09:14:38\","
                     //  " \"name\": \"iPhone Web 1\""
                     //  " }"
                     " },"
                     " \"swversion\": \"01012917\","
                     " \"apiversion\": \"1.3.0\","
                     " \"swupdate\": {"
                     " \"updatestate\": 0,"
                     " \"url\": \"\","
                     " \"text\": \"\","
                     " \"notify\": false"
                     " },"
                     " \"linkbutton\": true,"
                     " \"portalservices\": false,"
                     " \"portalconnection\": \"connected\","
                     " \"portalstate\": {"
                     " \"signedon\": false,"
                     " \"incoming\": false,"
                     " \"outgoing\": true,"
                     " \"communication\": \"disconnected\""
                     " }"
                     "}";

  if ( requestedUri.endsWith("/config") )
  {
    

    //if (isAuthorized == false) longstr = "[{\"swversion\":\"01008227\",\"apiversion\":\"1.2.1\",\"name\":\"Smartbridge 1\",\"mac\":\"" + macString + "\",}]";
    //HTTP.send(200, "text/plain", longstr);

    StaticJsonBuffer<200> jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();
    root["name"] = "Philips hue";
    root["zigbeechannel"] = "0"; // As per spec, 0 is allowed
    root["mac"] = macString.c_str();
    root["dhcp"] = "true";
    root["ipaddress"] = ipString.c_str();
    root["netmask"] = "255.255.255.0"; // TODO: FIXME
    root["gateway"] = "192.168.0.1"; // TODO: FIXME
    root["proxyaddress"] = "none";
    root["proxyport"] = "0";
    //JsonObject& whitelist = root.createNestedObject();
    //whitelist["name"] = "XXXXXXX";
    //JsonObject& clientobj = whitelist.createNestedObject();
    //char buffer[256];
    //root.prettyPrintTo(buffer, sizeof(buffer));
    //HTTP.send(200, "text/plain", "[" + String(buffer) + "]");
    //Serial.println("[" + String(buffer) + "]");

    HTTP.send(200, "text/plain", longstr);
    Serial.println(longstr);
    Serial.println("I assume there is an error in my response since after this the iOS app says Bridge disconnected");
  }

  else if (requestedUri.endsWith(client))
  {
    HTTP.send(200, "text/plain", "{ \"config\": " + longstr + ", \"lights\": " + "{ " + getLightString(1) + "}}" );
    Serial.println("{ \"config\": " + longstr + ", \"lights\": " + "{ " + getLightString(1) + "}}" );
    Serial.println("FIXME: Need to respond with a COMPLETE json as in https://github.com/probonopd/ESP8266HueEmulator/wiki/Hue-API#get-all-information-about-the-bridge");
  }


  else if (requestedUri.endsWith("UjBZ0nvTLu7aMdOe")) // FIXME: remove this!!!
  {
    HTTP.send(200, "text/plain", "{ \"config\": " + longstr + ", \"lights\": " + "{ " + getLightString(1) + "}}" );
    Serial.println("{ \"config\": " + longstr + ", \"lights\": " + "{ " + getLightString(1) + "}}" );
    Serial.println("FIXME: Need to respond with a COMPLETE json as in https://github.com/probonopd/ESP8266HueEmulator/wiki/Hue-API#get-all-information-about-the-bridge");
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
  else if (requestedUri.endsWith("/lights"))
    // We simulate one bulb per NeoPixel (TODO: more clever ideas?)
  {
    String str = "[{ " + getLightString(1) + "}]";
    HTTP.send(200, "text/plain", str);
    Serial.println(str);
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
  }
}

void setup() {

  // this resets all the neopixels to an off state
  strip.Begin();
  strip.Show();

  // Show that the NeoPixels are alive
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

  // ipString = String(WiFi.localIP());


  os_sprintf(ipChars, IPSTR, IP2STR(WiFi.localIP()));
  ipString = String(ipChars);


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
  HTTP.handleClient();
  SSDP.update();
  delay(1);
}


void rgb2xy(int R, int G, int B) {

  // Convert the RGB values to XYZ using the Wide RGB D65 conversion formula
  float X = R * 0.664511f + G * 0.154324f + B * 0.162028f;
  float Y = R * 0.283881f + G * 0.668433f + B * 0.047685f;
  float Z = R * 0.000088f + G * 0.072310f + B * 0.986039f;

  // Calculate the xy values from the XYZ values
  float x = X / (X + Y + Z);
  float y = Y / (X + Y + Z);
}

void infoLight(RgbColor color) {
  // Flash the strip in the selected color. White = booted, green = WLAN connected, red = WLAN could not connect
  for (int i = 0; i < 30; i++)
  {
    strip.SetPixelColor(i, color);
    strip.Show();
    delay(10);
    strip.SetPixelColor(i, black);
    strip.Show();
  }
}
