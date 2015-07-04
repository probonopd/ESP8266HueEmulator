# ESP8266HueEmulator
This sketch emulates a Philips Hue bridge running on an ESP8266. Right now this is a proof-of-concept. Hue client apps can discover the emulated bridge and begin talking to it using the Hue protocol.

Please note that currently only the bare minimum to advertise the emulated Hue bridge is implemented, but it is enough so that the http://chromaforhue.com OS X app can discover and communicate with the emulated bridge.
