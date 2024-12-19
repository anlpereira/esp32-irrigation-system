his repository contains the Arduino code for an ESP32-based irrigation system with a web interface.

## Project Description

This project is an irrigation system with a ESP32-C3, that is intended to do the followings:
* It has a pump witch is activated once or twice per day for 5 sec max on D10;
* I has a Soil Misture Sensor on PIN A1 that is used to measure the humidity of Soil once per minute:
* It has a temperature Sensor, DS18b20, on pin D4 that measures the temp every minute too;
* It has a PWM output on PIN D9, that uses AnalogWrite function, to activate a few grow leds that is intended to be working all day long like 8 a.m to 8 p.m in a configuration that imitates the Sun Light, I mean it starts with low brigthness and increases until 90% then decreases.
* I want you use ESPmDNS.h to setup a esp32.local server to show the informations of Temperature, Soil Moisture and Time (-3*3600). Add a Button on webpage that allows I turn on the pump only for 5 seconds too. Is it possible to show one graph with time and humidity of all day long?
* Use Canvas javascript to draw the graph on webpage.
* Use auto-update every minute on webpage;

## Code
The code can be found on `esp32-irrigation-system.ino`
