# DeviceLocatorEsp32

This project acts as the Client for https://github.com/xPasquale1/HeatmapCreator. A Esp32 running this program can be located indoors.
The Esp32 must have a Wifi-Chip integrated.

## Installation

Visual Studio Code + Platform IO Plugin

## Usage

The Esp32 will always try to connect to a Wifi Network. This Network can currently only be configured in code and can't be changed during runtime.
The default Wifi Network the Esp32 will connect to can be configured in the file src/constants.h through the constants WIFISSID0 and WIFIPASSWORD0.
