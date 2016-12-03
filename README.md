# ESP8266HueEmulator [![Build Status](https://travis-ci.org/probonopd/ESP8266HueEmulator.svg)](https://travis-ci.org/probonopd/ESP8266HueEmulator)
This sketch emulates a Philips Hue bridge running on an ESP8266 using the Arduino IDE. Right now this is a proof-of-concept; contributions are highly welcome. Hue client apps can [discover](../../wiki/Discovery) the emulated bridge and begin talking to it using the Hue protocol.

![phihue_e27_starterset_430x300 jpg](https://cloud.githubusercontent.com/assets/2480569/8511601/e692e61c-231f-11e5-842d-4fedd6f900b4.jpg)

__Optionally__, vou can use a strip of individually addressable WS2812b NeoPixels and attach it to GPIO2. The sketch talks to a strip of NeoPixels connected to GPIO2 of the ESP8266 with no additional circuitry. Right now the sketch uses the NeoPixels to tell that it is powered on, connected to the WLAN etc., and can switch on the first 3 NeoPixels using a Hue client (e.g., the iOS app).

To make this work, the sketch advertises its service with the so-called "Simple Service Discovery Protocol" (SSDP) that is also used as discovery protocol of Universal Plug and Play (UPnP). This sketch uses the ESP8266SSDP library from https://github.com/me-no-dev/Arduino

Please note that currently only the bare minimum to advertise the emulated Hue bridge is implemented, but it is enough so that the http://chromaforhue.com OS X app can discover and communicate with the emulated bridge.

## Usage

* I used [Arduino-1.6.11.hourly201608161225.esp497d19d-x86_64.AppImage](https://bintray.com/probono/AppImages/Arduino#files) which conveniently comes with both the Arduino IDE and esp8266/Arduino ready-to-use; otherwise use a recent version of the Arduino IDE and [esp8266/Arduino](https://github.com/esp8266/Arduino) 
* In the Arduino IDE, Open the Library Manager and search for "NeoPixelBus by Makuna" and install
* Download https://github.com/interactive-matter/aJson/archive/master.zip and install the library via the Arduino IDE
* Important: Change `#define PRINT_BUFFER_LEN 256` to `#define PRINT_BUFFER_LEN 4096` in `aJson/aJSON.h`
* Edit the sketch to contain your WLAN credentials
* Load the sketch onto your ESP-01 or other ESP8266 device
* Optionally connect the DATA line of your WS2812b NeoPixels to pin GPIO2 (you do not really need this in order to test communication between the sketch and Hue client apps)
* Watch the output of the sketch in a serial console
* Connect to the emulated bridge by using a Hue client app
* Switch on one of the lights
* Continue watching the output of the sketch in a serial console
* Implement more of the protocol
* Contribute pull requests ;-)

## Compilation

Everything in one go:

```
mkdir -p $HOME/Arduino/libraries/
cd $HOMEArduino/libraries/
git clone --branch 2.1.4 https://github.com/Makuna/NeoPixelBus.git
git clone https://github.com/interactive-matter/aJson.git
sed -i -e 's|#define PRINT_BUFFER_LEN 256|#define PRINT_BUFFER_LEN 4096|g'  aJson/aJSON.h
cd -
git clone https://github.com/probonopd/ESP8266HueEmulator.git
sed -i -e 's|#include "/secrets.h"|//#include "/secrets.h"|g' ESP8266HueEmulator/ESP8266HueEmulator/ESP8266HueEmulator.ino
sed -i -e 's|//const char|const char|g' ESP8266HueEmulator/ESP8266HueEmulator/ESP8266HueEmulator.ino
```

## Credits

* Philips for providing open Hue APIs that are not restricted for use on Philips-branded hardware (as far as I can see by looking at their liberal [Terms and Conditions of Use](https://github.com/probonopd/ESP8266HueEmulator/wiki/Discovery#terms-and-conditions-of-use))
* igrr for bringing the ESP8266 platform to the Arduino IDE
* me-no-dev for porting the uSSDP library to the ESP8266 platform and helping me make this work
* Makuna for the [NeoPixelBus](https://github.com/Makuna/NeoPixelBus) library supporting WS2812b NeoPixels on the ESP8266
* interactive-matter for the [aJson](https://github.com/interactive-matter/aJson) library
