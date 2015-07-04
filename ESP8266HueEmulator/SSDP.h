/*
  uSSDP.h - Library that implement SSDP protocol.
  Created by Filippo Sallemi, July 23, 2014.
  Released into the public domain.
*/

#ifndef uSSDP_H
#define uSSDP_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include "ssdpDevice.h"

#define DEBUG           1

#define SSDP_INTERVAL   1200
#define SSDP_PORT       1900

// Sizes
#define METHOD_SIZE     10
#define URI_SIZE        2
#define BUFFER_SIZE     48

class uSSDP{

  typedef enum {NONE, SEARCH, NOTIFY} method_t;

  public:
    uSSDP();
    ~uSSDP();

    void begin(uDevice *device);
    uint8_t process();
    void send(method_t method);
    void schema(WiFiClient *client);

  private:
    WiFiUDP _server;

    uDevice *_device;

    bool _pending;
    unsigned short _delay;
    unsigned long _process_time;
    unsigned long _notify_time;
};

#endif
