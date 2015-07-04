# ESP8266HueEmulator
This sketch emulates a Philips Hue bridge running on an ESP8266. Right now this is a proof-of-concept. Hue client apps can discover the emulated bridge and begin talking to it using the Hue protocol.

To make this work, the sketch advertises its service with the so-called "Simple Service Discovery Protocol" (SSDP) that is also used as discovery protocol of Universal Plug and Play (UPnP). This sketch uses a slighly patched version of the uSSDP library from https://github.com/me-no-dev/uSSDP (I have also renamed the files in order to not conflict with an installed uSSDP library; in the future I hope I do not need to bundle a local copy.)

In the sketch you will find something like `String client = "e7x4kuCaC8h885jo";`. This needs to be changed by you to the client string that the client app actually sends. This is a quick and dirty hack, the sketch should parse this out of what is being sent by the app instead.

Please note that currently only the bare minimum to advertise the emulated Hue bridge is implemented, but it is enough so that the http://chromaforhue.com OS X app can discover and communicate with the emulated bridge.
