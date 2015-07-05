# ESP8266HueEmulator
This sketch emulates a Philips Hue bridge running on an ESP8266 using the Arduino IDE. Right now this is a proof-of-concept; contributions are highly welcome. Hue client apps can [discover](../../wiki/Discovery) the emulated bridge and begin talking to it using the Hue protocol.

![phihue_e27_starterset_430x300 jpg](https://cloud.githubusercontent.com/assets/2480569/8511601/e692e61c-231f-11e5-842d-4fedd6f900b4.jpg)

__Optionally__, vou can use a strip of WS2812b NeoPixels and attach it to GPIO2. The sketch talks to a strip of NeoPixels connected to GPIO2 of the ESP8266 with no additional circuitry. Right now the sketch uses the NeoPixels to tell that it is powered on, connected to the WLAN etc., but so far I haven't figured out yet how to make the sketch actually process Hue commands to steer the NeoPixels. (FIXME)

To make this work, the sketch advertises its service with the so-called "Simple Service Discovery Protocol" (SSDP) that is also used as discovery protocol of Universal Plug and Play (UPnP). This sketch uses the ESP8266SSDP library from https://github.com/me-no-dev/Arduino

Please note that currently only the bare minimum to advertise the emulated Hue bridge is implemented, but it is enough so that the http://chromaforhue.com OS X app can discover and communicate with the emulated bridge.
