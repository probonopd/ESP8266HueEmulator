/*
  uDevice.h - Library that implement Device for SSDP protocol.
  Created by Filippo Sallemi, July 23, 2014.
  Released into the public domain.
*/

#ifndef uDEVICE_H
#define uDEVICE_H

#include <Arduino.h>

#define UUIDBASE_SIZE           24
#define PRESENTATIONURL_SIZE    32
#define FRIENDLYNAME_SIZE       32
#define MODELNAME_SIZE          32
#define MODELNUMBER_SIZE        32
#define SERIALNUMBER_SIZE       32
#define MANUFACTURER_SIZE       32
#define MANUFACTURERURL_SIZE    32

typedef enum { BASIC, MANAGEABLE, SOLARPROTECTIONBLIND, DIGITALSECURITYCAMERA, HVAC, LIGHTINGCONTROL } device_t;

class uDevice{

  public:
    uDevice();
    ~uDevice();
    
    void begin(const char *base, byte *mac, device_t deviceType);
    void begin(const char *base, byte *mac){
      begin(base, mac, BASIC);
    }
    
    char *uuid();

    device_t deviceType();
    void deviceType(device_t deviceType);

    char *presentationURL();
    void presentationURL(char *presentationURL);

    char *friendlyName();
    void friendlyName(char *friendlyName);

    char *modelName();
    void modelName(char *modelName);

    char *modelNumber();
    void modelNumber(char *modelNumber);

    char *serialNumber();
    void serialNumber(char *serialNumber);

    char *manufacturer();
    void manufacturer(char *manufacturer);

    char *manufacturerURL();
    void manufacturerURL(char *manufacturerURL);

  private:
    byte *_mac;
    char *_base;

    device_t _deviceType;
    char *_modelNumber;

    char *_presentationURL;
    char *_friendlyName;
    char *_manufacturer;
    char *_manufacturerURL;
    char *_modelName;
    char *_serialNumber;
};

#endif