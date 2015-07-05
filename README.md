# ESP8266HueEmulator
This sketch emulates a Philips Hue bridge running on an ESP8266. Right now this is a proof-of-concept. Hue client apps can discover the emulated bridge and begin talking to it using the Hue protocol.

To make this work, the sketch advertises its service with the so-called "Simple Service Discovery Protocol" (SSDP) that is also used as discovery protocol of Universal Plug and Play (UPnP). This sketch uses the ESP8266SSDP library from https://github.com/me-no-dev/Arduino

Please note that currently only the bare minimum to advertise the emulated Hue bridge is implemented, but it is enough so that the http://chromaforhue.com OS X app can discover and communicate with the emulated bridge.
