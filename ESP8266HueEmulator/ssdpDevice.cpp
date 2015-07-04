/*
  uSSDP.cpp - Library that implement SSDP protocol.
  Created by Filippo Sallemi, July 23, 2014.
  Released into the public domain.
*/

#include "ssdpDevice.h"
#include <ESP8266WiFi.h>

extern "C" {
#include "mem.h"
}

uDevice::uDevice():
_base(0),
_mac(0),
_presentationURL(0),
_friendlyName(0),
_modelName(0),
_modelNumber(0),
_serialNumber(0),
_manufacturer(0),
_manufacturerURL(0)
{
  _deviceType = BASIC;
}

void uDevice::begin(const char *base, byte *mac, device_t deviceType){
  _base = (char*)os_malloc(UUIDBASE_SIZE);
  _mac = (byte*)os_malloc(6);
  _presentationURL = (char*)os_malloc(PRESENTATIONURL_SIZE);
  _friendlyName = (char*)os_malloc(FRIENDLYNAME_SIZE);
  _modelName = (char*)os_malloc(MODELNAME_SIZE);
  _modelNumber = (char*)os_malloc(MODELNUMBER_SIZE);
  _serialNumber = (char*)os_malloc(SERIALNUMBER_SIZE);
  _manufacturer = (char*)os_malloc(MANUFACTURER_SIZE);
  _manufacturerURL = (char*)os_malloc(MANUFACTURERURL_SIZE);

  _deviceType = deviceType;
  memcpy(_mac, mac, 6);
  strcpy(_base, base);
}

uDevice::~uDevice(){
	os_free(_base);
  os_free(_mac);
  os_free(_presentationURL);
  os_free(_friendlyName);
  os_free(_modelName);
  os_free(_modelNumber);
  os_free(_serialNumber);
  os_free(_manufacturer);
  os_free(_manufacturerURL);
}

char *uDevice::uuid(){
  char *_uuid = (char*)os_malloc(37);
  sprintf(_uuid, "%s-%02X%02X%02X%02X%02X%02X", _base, _mac[0], _mac[1], _mac[2], _mac[3], _mac[4], _mac[5]);
	return _uuid;
}

device_t uDevice::deviceType(){
  return _deviceType;
}

void uDevice::deviceType(device_t deviceType){
  _deviceType = deviceType;
}

char *uDevice::presentationURL(){
  return _presentationURL;
}

void uDevice::presentationURL(char *presentationURL){
  strcpy(_presentationURL, presentationURL);
}

char *uDevice::friendlyName(){
  return _friendlyName;
}

void uDevice::friendlyName(char *friendlyName){
  strcpy(_friendlyName, friendlyName);
}

char *uDevice::modelName(){
  return _modelName;
}

void uDevice::modelName(char *modelName){
  strcpy(_modelName, modelName);
}

char *uDevice::modelNumber(){
  return _modelNumber;
}

void uDevice:: modelNumber(char * modelNumber){
  strcpy(_modelNumber, modelNumber);
}

char *uDevice::serialNumber(){
  return _serialNumber;
}

void uDevice::serialNumber(char *serialNumber){
  strcpy(_serialNumber, serialNumber);
}

char *uDevice::manufacturer(){
  return _manufacturer;
}

void uDevice::manufacturer(char *manufacturer){
  strcpy(_manufacturer, manufacturer);
}


char *uDevice::manufacturerURL(){
  return _manufacturerURL;
}

void uDevice::manufacturerURL(char *manufacturerURL){
  strcpy(_manufacturerURL, manufacturerURL);
}
