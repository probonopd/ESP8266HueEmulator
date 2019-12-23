/*
ESP8266 Simple Service Discovery
Copyright (c) 2015 Hristo Gochkov

Original (Arduino) version by Filippo Sallemi, July 23, 2014.
Can be found at: https://github.com/nomadnt/uSSDP

License (MIT license):
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.

*/
// #define LWIP_OPEN_SRC
#include <functional>
#include "SSDP.h"
#include "WiFiUdp.h"
#include "debug.h"

extern "C" {
  #include "osapi.h"
  #include "ets_sys.h"
  #include "user_interface.h"
}

#include "lwip/opt.h"
#include "lwip/udp.h"
#include "lwip/inet.h"
#include "lwip/igmp.h"
#include "lwip/mem.h"
#include "include/UdpContext.h"

//#define DEBUG_SSDP  Serial

#define SSDP_INTERVAL     1200
#define SSDP_PORT         1900
#define SSDP_METHOD_SIZE  10
#define SSDP_URI_SIZE     2
#define SSDP_BUFFER_SIZE  64
#define SSDP_MULTICAST_TTL 2
static const IPAddress SSDP_MULTICAST_ADDR(239, 255, 255, 250);



static const char* _ssdp_response_template =
  "HTTP/1.1 200 OK\r\n"
  "EXT:\r\n";

static const char* _ssdp_notify_template =
  "NOTIFY * HTTP/1.1\r\n"
  "HOST: 239.255.255.250:1900\r\n"
  "NTS: ssdp:alive\r\n";

static const char* _ssdp_packet_template =
  "%s" // _ssdp_response_template / _ssdp_notify_template
  "CACHE-CONTROL: max-age=%u\r\n" // SSDP_INTERVAL
  "SERVER: FreeRTOS/6.0.5, UPnP/1.0, %s/%s\r\n" // _modelName, _modelNumber
  "USN: uuid:%s\r\n" // _uuid
  "%s: %s\r\n"  // "NT" or "ST", _deviceType
  "LOCATION: http://%u.%u.%u.%u:%u/%s\r\n" // WiFi.localIP(), _port, _schemaURL
  "\r\n";

static const char* _ssdp_schema_template =
  "HTTP/1.1 200 OK\r\n"
  "Content-Type: text/xml\r\n"
  "Connection: close\r\n"
  "Access-Control-Allow-Origin: *\r\n"
  "\r\n"
  "<?xml version=\"1.0\"?>"
  "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">"
    "<specVersion>"
      "<major>1</major>"
      "<minor>0</minor>"
    "</specVersion>"
    "<URLBase>http://%u.%u.%u.%u:%u/</URLBase>" // WiFi.localIP(), _port
    "<device>"
      "<deviceType>%s</deviceType>"
      "<friendlyName>%s</friendlyName>"
      "<presentationURL>%s</presentationURL>"
      "<serialNumber>%s</serialNumber>"
      "<modelName>%s</modelName>"
      "<modelNumber>%s</modelNumber>"
      "<modelURL>%s</modelURL>"
      "<manufacturer>%s</manufacturer>"
      "<manufacturerURL>%s</manufacturerURL>"
      "<UDN>uuid:%s</UDN>"
    "</device>"
//    "<iconList>"
//      "<icon>"
//        "<mimetype>image/png</mimetype>"
//        "<height>48</height>"
//        "<width>48</width>"
//        "<depth>24</depth>"
//        "<url>icon48.png</url>"
//      "</icon>"
//      "<icon>"
//       "<mimetype>image/png</mimetype>"
//       "<height>120</height>"
//       "<width>120</width>"
//       "<depth>24</depth>"
//       "<url>icon120.png</url>"
//      "</icon>"
//    "</iconList>"
  "</root>\r\n"
  "\r\n";


struct SSDPTimer {
  ETSTimer timer;
};

SSDPClass::SSDPClass() :
_server(0),
_timer(new SSDPTimer),
_port(80),
_ttl(SSDP_MULTICAST_TTL),
_respondToPort(0),
_pending(false),
_delay(0),
_process_time(0),
_notify_time(0)
{
  _uuid[0] = '\0';
  _modelNumber[0] = '\0';
  sprintf(_deviceType, "urn:schemas-upnp-org:device:Basic:1");
  _friendlyName[0] = '\0';
  _presentationURL[0] = '\0';
  _serialNumber[0] = '\0';
  _modelName[0] = '\0';
  _modelURL[0] = '\0';
  _manufacturer[0] = '\0';
  _manufacturerURL[0] = '\0';
  sprintf(_schemaURL, "ssdp/schema.xml");
}

SSDPClass::~SSDPClass(){
  delete _timer;
}

bool SSDPClass::begin(){
  _pending = false;

  String mac =  WiFi.macAddress();
  mac.replace(":", "");
  mac.toLowerCase();
  sprintf(_uuid, "38323636-4558-4dda-9188-%s", mac.c_str());

#ifdef DEBUG_SSDP
  DEBUG_SSDP.printf("SSDP UUID: %s\r\n", (char *)_uuid);
#endif

  if (_server) {
    _server->unref();
    _server = 0;
  }

  _server = new UdpContext;
  _server->ref();

  ip_addr_t ifaddr;
  ifaddr.addr = WiFi.localIP();
  ip_addr_t multicast_addr;
  multicast_addr.addr = (uint32_t) SSDP_MULTICAST_ADDR;
  if (igmp_joingroup(&ifaddr, &multicast_addr) != ERR_OK ) {
    DEBUGV("SSDP failed to join igmp group");
    return false;
  }

  if (!_server->listen(IP_ADDR_ANY, SSDP_PORT)) {
    return false;
  }

  _server->setMulticastInterface(ifaddr);
  _server->setMulticastTTL(_ttl);
  _server->onRx(std::bind(&SSDPClass::_update, this));
  if (!_server->connect(&multicast_addr, SSDP_PORT)) {
    return false;
  }

  _startTimer();

  return true;
}

void SSDPClass::_send(ssdp_method_t method){
  char buffer[1460];
  ip_addr_t ip = WiFi.localIP();

  int len;
  if (_messageFormatCallback) {
    len = _messageFormatCallback(this, buffer, sizeof(buffer), method != NONE, SSDP_INTERVAL, _modelName, _modelNumber, _uuid, _deviceType, ip.addr, _port, _schemaURL);
  } else {
    len = snprintf(buffer, sizeof(buffer),
      _ssdp_packet_template,
      (method == NONE)?_ssdp_response_template:_ssdp_notify_template,
      SSDP_INTERVAL,
      _modelName, _modelNumber,
      _uuid,
      (method == NONE)?"ST":"NT",
      _deviceType,
      IP2STR(&ip), _port, _schemaURL
    );
  }

  _server->append(buffer, len);

  ip_addr_t remoteAddr;
  uint16_t remotePort;
  if(method == NONE) {
    remoteAddr.addr = _respondToAddr;
    remotePort = _respondToPort;
#ifdef DEBUG_SSDP
    DEBUG_SSDP.print("Sending Response to ");
#endif
  } else {
    remoteAddr.addr = SSDP_MULTICAST_ADDR;
    remotePort = SSDP_PORT;
#ifdef DEBUG_SSDP
    DEBUG_SSDP.println("Sending Notify to ");
#endif
  }
#ifdef DEBUG_SSDP
  DEBUG_SSDP.print(IPAddress(remoteAddr.addr));
  DEBUG_SSDP.print(":");
  DEBUG_SSDP.println(remotePort);
#endif

  _server->send(&remoteAddr, remotePort);
}

void SSDPClass::schema(WiFiClient client){
  ip_addr_t ip = WiFi.localIP();
  client.printf(_ssdp_schema_template,
    IP2STR(&ip), _port,
    _deviceType,
    _friendlyName,
    _presentationURL,
    _serialNumber,
    _modelName,
    _modelNumber,
    _modelURL,
    _manufacturer,
    _manufacturerURL,
    _uuid
  );
}

// writes the next token into token if token is not NULL
// returns -1 on message end, otherwise returns
int SSDPClass::_getNextToken(String *token, bool break_on_space, bool break_on_colon) {
	if (token) *token = "";
	bool token_found = false;
	int cr_found = 0;
	while (_server->getSize() > 0) {
		char next = _server->read();
		switch (next) {
		case '\r':
		case '\n':
			cr_found++;
			if (cr_found == 3) {
				// end of message reached
				return -1;
			}
			if (token_found) {
				// end of token reached
				return _server->getSize();
			}
			continue;
		case ' ':
			// only treat spaces as part of text if they're not leading
			if (!token_found) {
				cr_found = 0;
				continue;
			}
      if (!break_on_space) {
        break;
      }
      cr_found = 0;
      // end of token reached
      return _server->getSize();
		case ':':
			// only treat colons as part of text if they're not leading
      if (!token_found) {
        cr_found = 0;
        continue;
      }
      if (!break_on_colon) {
        break;
      }
      cr_found = 0;
      // end of token reached
      return _server->getSize();
		default:
			cr_found = 0;
			token_found = true;
			break;
		}

		if (token) {
		  (*token) += next;
		}
	}
	return 0;
}

void SSDPClass::_bailRead() {
    while (_getNextToken(NULL, true, true) > 0);
    _pending = false;
    _delay = 0;
}

void SSDPClass::_parseIncoming() {
    _respondToAddr = _server->getRemoteAddress();
    _respondToPort = _server->getRemotePort();

    typedef enum {START, MAN, ST, MX, UNKNOWN} headers;
    headers header = START;

    String token;
    // get message type
    int res = _getNextToken(&token, true, false);
    if (res <= 0) {
        _bailRead();
        return;
    }
    
    if (token == "M-SEARCH") {
    } else if (token == "NOTIFY") {
    	 // incoming notifies are not currently handled
    	_bailRead();
    	return;
    } else {
        _bailRead();
        return;
    }

    // get URI
    res = _getNextToken(&token, true, false);
    if (res <= 0) {
        _bailRead();
        return;
    }
    if (token != "*") {
        _bailRead();
        return;
    }

    // eat protocol (HTTP/1.1)
    res = _getNextToken(NULL, false, false);
    if (res <= 0) {
        _bailRead();
        return;
    }

    while(_server->getSize() > 0){
      res = _getNextToken(&token, header == START, header == START);
      if (res < 0 && header == START) {
        break;
      }

      switch(header){
      case START:
        if(token.equalsIgnoreCase("MAN")) header = MAN;
        else if(token.equalsIgnoreCase("ST")) header = ST;
        else if(token.equalsIgnoreCase("MX")) header = MX;
        else {
          header = UNKNOWN;
#ifdef DEBUG_SSDP
          DEBUG_SSDP.printf("Found unknown header '%s'\r\n", token.c_str());
#endif
        }
        break;
      case MAN:
#ifdef DEBUG_SSDP
        DEBUG_SSDP.printf("MAN: %s\r\n", token.c_str());
#endif
        header = START;
        break;
      case ST:
        if(token != "ssdp:all" && token != _deviceType){
#ifdef DEBUG_SSDP
          DEBUG_SSDP.printf("REJECT: %s\r\n", token.c_str());
#endif
        }
        _pending = true;
        _process_time = millis();
        header = START;
        break;
      case MX:
        _delay = random(0, atoi(token.c_str())) * 1000L;
        header = START;
        break;
      case UNKNOWN:
        header = START;
#ifdef DEBUG_SSDP
        DEBUG_SSDP.printf("Value for unkown header: %s\r\n", token.c_str());
#endif
        break;
      }
    }
    if (header != START) {
      // something broke during parsing of the message
       _bailRead();
    }
}

void SSDPClass::_update(){
  if(!_pending && _server->next()) {
    _parseIncoming();
  }

  if(_pending && (millis() - _process_time) > _delay){
    _pending = false; _delay = 0;
    _send(NONE);
  } else if(_notify_time == 0 || (millis() - _notify_time) > (SSDP_INTERVAL * 1000L)){
    _notify_time = millis();
    _send(NOTIFY);
  }

  if (_pending) {
    while (_server->next())
      _server->flush();
  }

}

void SSDPClass::setSchemaURL(const char *url){
  strlcpy(_schemaURL, url, sizeof(_schemaURL));
}

void SSDPClass::setHTTPPort(uint16_t port){
  _port = port;
}

void SSDPClass::setDeviceType(const char *deviceType){
  strlcpy(_deviceType, deviceType, sizeof(_deviceType));
}

void SSDPClass::setName(const char *name){
  strlcpy(_friendlyName, name, sizeof(_friendlyName));
}

void SSDPClass::setURL(const char *url){
  strlcpy(_presentationURL, url, sizeof(_presentationURL));
}

void SSDPClass::setSerialNumber(const char *serialNumber){
  strlcpy(_serialNumber, serialNumber, sizeof(_serialNumber));
}

void SSDPClass::setSerialNumber(const uint32_t serialNumber){
  snprintf(_serialNumber, sizeof(uint32_t)*2+1, "%08X", serialNumber);
}

void SSDPClass::setModelName(const char *name){
  strlcpy(_modelName, name, sizeof(_modelName));
}

void SSDPClass::setModelNumber(const char *num){
  strlcpy(_modelNumber, num, sizeof(_modelNumber));
}

void SSDPClass::setModelURL(const char *url){
  strlcpy(_modelURL, url, sizeof(_modelURL));
}

void SSDPClass::setManufacturer(const char *name){
  strlcpy(_manufacturer, name, sizeof(_manufacturer));
}

void SSDPClass::setManufacturerURL(const char *url){
  strlcpy(_manufacturerURL, url, sizeof(_manufacturerURL));
}

void SSDPClass::setTTL(const uint8_t ttl){
  _ttl = ttl;
}

void SSDPClass::_onTimerStatic(SSDPClass* self) {
  self->_update();
}

void SSDPClass::_startTimer() {
  ETSTimer* tm = &(_timer->timer);
  const int interval = 1000;
  os_timer_disarm(tm);
  os_timer_setfn(tm, reinterpret_cast<ETSTimerFunc*>(&SSDPClass::_onTimerStatic), reinterpret_cast<void*>(this));
  os_timer_arm(tm, interval, 1 /* repeat */);
}

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_SSDP)
SSDPClass SSDP;
#endif
