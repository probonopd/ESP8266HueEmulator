/*
  uSSDP.cpp - Library that implement SSDP protocol.
  Created by Filippo Sallemi, July 23, 2014.
  Released into the public domain.
*/

#include "SSDP.h"

extern "C" {
#include "ip_addr.h"
#include "user_interface.h"
}
#include "lwip/igmp.h"

static const IPAddress SSDP_MULTICAST_ADDR(239, 255, 255, 250);

uSSDP::uSSDP(): _device(0) {
  _pending = false;
}

uSSDP::~uSSDP(){
  delete _device;
}

void uSSDP::begin(uDevice *device){
  _device = device;
  _pending = false;
  
  struct ip_info staIpInfo;
  wifi_get_ip_info(STATION_IF, &staIpInfo);
  ip_addr_t ifaddr;
  ifaddr.addr = staIpInfo.ip.addr;
  ip_addr_t multicast_addr;
  multicast_addr.addr = (uint32_t) SSDP_MULTICAST_ADDR;
  igmp_joingroup(&ifaddr, &multicast_addr);
  
  _server.begin(SSDP_PORT);
}

uint8_t uSSDP::process(){
  if(!_pending && _server.parsePacket() > 0){
    method_t method = NONE;

    typedef enum {METHOD, URI, PROTO, KEY, VALUE, ABORT} states;
    states state = METHOD;

    typedef enum {START, MAN, ST, MX} headers;
    headers header = START;

    uint8_t cursor = 0;
    uint8_t cr = 0;

    char buffer[BUFFER_SIZE] = {0};
    
    while(_server.available() > 0){
      char c = _server.read();

      (c == '\r' || c == '\n') ? cr++ : cr = 0;

      switch(state){
        case METHOD:
          if(c == ' '){
            if(strcmp(buffer, "M-SEARCH") == 0) method = SEARCH;
            else if(strcmp(buffer, "NOTIFY") == 0) method = NOTIFY;
            
            if(method == NONE) state = ABORT;
            else state = URI; 
            cursor = 0;

          }else if(cursor < METHOD_SIZE - 1){ buffer[cursor++] = c; buffer[cursor] = '\0'; }
          break;
        case URI:
          if(c == ' '){
            if(strcmp(buffer, "*")) state = ABORT;
            else state = PROTO; 
            cursor = 0; 
          }else if(cursor < URI_SIZE - 1){ buffer[cursor++] = c; buffer[cursor] = '\0'; }
          break;
        case PROTO:
          if(cr == 2){ state = KEY; cursor = 0; }
          break;
        case KEY:
          if(cr == 4){ _pending = true; _process_time = millis(); }
          else if(c == ' '){ cursor = 0; state = VALUE; }
          else if(c != '\r' && c != '\n' && c != ':' && cursor < BUFFER_SIZE - 1){ buffer[cursor++] = c; buffer[cursor] = '\0'; }
          break;
        case VALUE:
          if(cr == 2){
            switch(header){
              case MAN:
                //Serial.print("MAN: ");
                //Serial.println(buffer);
                //strncpy(_head.man, buffer, HEAD_VAL_SIZE);
                break;
              case ST:
                if(strcmp(buffer, "ssdp:all")){
                  state = ABORT;
                  #if DEBUG > 0
                  Serial.print("REJECT: ");
                  Serial.println(buffer);
                  #endif
                }
                break;
              case MX:
                _delay = random(0, atoi(buffer)) * 1000L;
                break;
            }

            if(state != ABORT){ state = KEY; header = START; cursor = 0; }
          }else if(c != '\r' && c != '\n'){
            if(header == START){
              if(strncmp(buffer, "MA", 2) == 0) header = MAN;
              else if(strcmp(buffer, "ST") == 0) header = ST;
              else if(strcmp(buffer, "MX") == 0) header = MX;
            }
            
            if(cursor < BUFFER_SIZE - 1){ buffer[cursor++] = c; buffer[cursor] = '\0'; }
          }
          break;
        case ABORT:
          _pending = false; _delay = 0;
          break;
      }
    }
    
    _server.flush();
  }

  if(_pending && (millis() - _process_time) > _delay){
    _pending = false; _delay = 0;
    send(NONE);
  }else if(_notify_time == 0 || (millis() - _notify_time) > (SSDP_INTERVAL * 1000L)){
    _notify_time = millis();
    send(NOTIFY);
  }
}

void uSSDP::send(method_t method){
  char *modelNumber = _device->modelNumber();
  byte ssdp[4] = {239, 255, 255, 250};

	if(method == NONE){
    #if DEBUG > 0
    Serial.print("Sending Response to ");
    Serial.print(_server.remoteIP());
    Serial.print(":");
    Serial.println(_server.remotePort());
    #endif

    _server.beginPacket(_server.remoteIP(), _server.remotePort());
		_server.println("HTTP/1.1 200 OK");
		_server.println("EXT:");
		_server.println("ST: upnp:rootdevice");
	}else if(method == NOTIFY){
    #if DEBUG > 0
    Serial.println("Sending Notify to 239.255.255.250:1900");
    #endif

    _server.beginPacket(ssdp, SSDP_PORT);
		_server.println("NOTIFY * HTTP/1.1");
		_server.println("HOST: 239.255.255.250:1900");
		_server.println("NT: upnp:rootdevice");
		_server.println("NTS: ssdp:alive");
	}

	_server.print("CACHE-CONTROL: max-age=");
	_server.println(SSDP_INTERVAL);	

  _server.print("SERVER: Arduino/1.0 UPNP/1.1 ");
  _server.print(_device->modelName());
  _server.print("/");
  _server.print(_device->modelNumber());
  
  _server.println();
  
	_server.print("USN: uuid:");
	_server.println(_device->uuid());

	_server.print("LOCATION: http://");
	_server.print(WiFi.localIP());
	_server.println("/ssdp/schema.xml");
	_server.println();

	_server.endPacket();
}

void uSSDP::schema(WiFiClient *client){
  client->println("HTTP/1.1 200 OK");
  client->println("Content-Type: text/xml");
  client->println();

  client->println("<?xml version=\"1.0\"?>");
  client->println("<root xmlns=\"urn:schemas-upnp-org:device-1-0\">");
  client->println("\t<specVersion>");
  client->println("\t\t<major>1</major>");
  client->println("\t\t<minor>0</minor>");
  client->println("\t</specVersion>");
    
  client->println("\t<device>");
  client->println("\t\t<deviceType>urn:schemas-upnp-org:device:Basic:1</deviceType>");

  if(strlen(_device->presentationURL())){
    client->print("\t<presentationURL>");
    client->print(_device->presentationURL());
    client->print("</presentationURL>\r\n");
  }

  client->print("\t\t<friendlyName>");
  client->print(_device->friendlyName());
  client->print("</friendlyName>\r\n");

  client->print("\t\t<modelName>");
  client->print(_device->modelName());
  client->print("</modelName>\r\n");

  char *modelNumber = _device->modelNumber();

  if(strlen(_device->modelNumber())){
    client->print("\t\t<modelNumber>");
    client->print(_device->modelNumber());
    client->print("</modelNumber>\r\n");
  }

  if(strlen(_device->serialNumber())){
    client->print("\t\t<serialNumber>");
    client->print(_device->serialNumber());
    client->print("</serialNumber>\r\n");
  }

  client->print("\t\t<manufacturer>");
  client->print(_device->manufacturer());
  client->print("</manufacturer>\r\n");

  if(strlen(_device->manufacturerURL())){
    client->print("\t\t<manufacturerURL>");
    client->print(_device->manufacturerURL());
    client->print("</manufacturerURL>\r\n");
  }

  client->print("\t\t<UDN>uuid:");
  client->print(_device->uuid());
  client->print("</UDN>\r\n");

  client->println("\t</device>");
  client->println("</root>");
}
