/**
 * Emulate Philips Hue Bridge ; so far the Hue app finds the emulated Bridge and
 * requests URLs like /api/UjBZ0nvTLu7aMdOe but in the end it does not succeed "finding" the emulated bridge
 * (even if the app is talking to it)
 **/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiUDP.h>
#include "SSDP.h"

const char* ssid = "********";
const char* password = "********";

ESP8266WebServer HTTP(80);
uDevice device;
uSSDP SSDP;

String client = "e7x4kuCaC8h885jo"; // "UjBZ0nvTLu7aMdOe"; // The client string that the client app sends. Need to enter here what the app sends.
// FIXME: Parse this out of what is being sent by the app.

void handleAllOthers() {
  Serial.println("===");
  String requestedUri = HTTP.uri();
  Serial.print("requestedUri: ");
  Serial.println(requestedUri);


  if ( requestedUri.endsWith("/config") )
  {
    String longstr = "[{"
                     " \"name\": \"Philips hue\","
                     " \"zigbeechannel\": 15,"
                     " \"mac\": \"00:17:88:00:00:00\","
                     " \"dhcp\": true,"
                     " \"ipaddress\": \"192.168.0.16\","
                     " \"netmask\": \"255.255.255.0\","
                     " \"gateway\": \"192.168.0.1\","
                     " \"proxyaddress\": \"none\","
                     " \"proxyport\": 0,"
                     " \"UTC\": \"2014-07-17T09:27:35\","
                     " \"localtime\": \"2014-07-17T11:27:35\","
                     " \"timezone\": \"Europe/Madrid\","
                     " \"whitelist\": {"
                     " \"" + client + "\": {"
                     " \"last use date\": \"2015-06-17T07:21:38\","
                     " \"create date\": \"2014-04-08T08:55:10\","
                     " \"name\": \"Chroma for Hue\""
                     " },"
                     " \"pAtwdCV8NZId25Gk\": {"
                     " \"last use date\": \"2014-05-07T18:28:29\","
                     " \"create date\": \"2014-04-09T17:29:16\","
                     " \"name\": \"MyApplication\""
                     " },"
                     " \"gDN3IaPYSYNPWa2H\": {"
                     " \"last use date\": \"2014-05-07T09:15:21\","
                     " \"create date\": \"2014-05-07T09:14:38\","
                     " \"name\": \"iPhone Web 1\""
                     " }"
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
                     " \"signedon\": true,"
                     " \"incoming\": false,"
                     " \"outgoing\": true,"
                     " \"communication\": \"connected\""
                     " }"
                     "}]";
    HTTP.send(200, "text/plain", longstr);
    Serial.println(longstr);
  }
  
  else if (requestedUri.endsWith(client))
  {
    String str = "[{\"success\":{\"username\": \"" + client + "\"}}]";
    HTTP.send(200, "text/plain", str);
    Serial.println(str);
  }
   else if (requestedUri.endsWith("/api"))
   // On the real bridge, the link button on the bridge must have been recently pressed for the command to execute successfully.
   // We try to execute successfully regardless of a button for now.
  {
    String str = "[{\"success\":{\"username\": \"" + client + "\"}}]";
    HTTP.send(200, "text/plain", str);
    Serial.println(str);
  }


  else
  {
    HTTP.send(404, "text/plain", "File not found");
    Serial.println("FIXME: To be implemented");
  }
}

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("WiFi Failed");
    while (1) delay(100);
  }

  Serial.print("Starting HTTP at ");
  Serial.print(WiFi.localIP());
  Serial.print(":");
  Serial.println(80);

  HTTP.onNotFound(handleAllOthers);

  HTTP.on("/ssdp/schema.xml", HTTP_GET, []() {
    WiFiClient client = HTTP.client();
    SSDP.schema(&client);
    client.stop();
  });

  HTTP.on("/description.xml", HTTP_GET, []() {
    String desc = "<root><specVersion><major>1</major><minor>0</minor></specVersion><URLBase>http://192.168.1.35:80/</URLBase><device><deviceType>urn:schemas-upnp-org:device:Basic:1</deviceType><friendlyName>Philips hue (192.168.1.35)</friendlyName><manufacturer>Royal Philips Electronics</manufacturer><manufacturerURL>http://www.philips.com</manufacturerURL><modelDescription>Philips hue Personal Wireless Lighting</modelDescription><modelName>Philips hue bridge 2012</modelName><modelNumber>929000226503</modelNumber><modelURL>http://www.meethue.com</modelURL><serialNumber>00178817122c</serialNumber><UDN>uuid:2f402f80-da50-11e1-9b23-00178817122c</UDN><presentationURL>index.html</presentationURL><iconList><icon><mimetype>image/png</mimetype><height>48</height><width>48</width><depth>24</depth><url>hue_logo_0.png</url></icon><icon><mimetype>image/png</mimetype><height>120</height><width>120</width><depth>24</depth><url>hue_logo_3.png</url></icon></iconList></device></root>";
    HTTP.send(200, "text/plain", desc);
    Serial.println(desc);
  });


  HTTP.begin();

  byte mac[6];
  char base[UUIDBASE_SIZE];
  WiFi.macAddress(mac);
  sprintf(base, "esp8266x-%02x%02x-%02x%02x-%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  Serial.printf("Starting uSSDP: BASE: %s, MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", base, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  device.begin((const char*)base, mac);
  device.serialNumber((char*)"00178817122c");
  device.manufacturer((char*)"Royal Philips Electronics");
  device.manufacturerURL((char*)"http://www.philips.com");
  device.modelName((char*)"Philips hue bridge 2012");
  device.modelNumber("929000226503");
  device.serialNumber(base);
  device.friendlyName((char*)"Philips hue ()");
  device.presentationURL((char*)"index.html");
  SSDP.begin(&device);
  Serial.println("SSDP Started");
}

void loop() {
  HTTP.handleClient();
  SSDP.process();
  delay(1);
}
