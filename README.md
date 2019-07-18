![Lightning PoS](lightning-pos.jpg)

Lightning Point-of-Sale Terminal
================================================================

Many thanks to [arcbtc](https://github.com/arcbtc), his 
[OpenNode Tutorial](https://github.com/arcbtc/bitcoin2019conf)
and gift of working kit totally bootstrapped this project.

The goal of this project is to build an inexpensive open source
point-of-sale terminal for lightning network payments.  The current
design connects to OpenNode's API and presents invoices on demand.
Connecting to BTCPay servers is in development.

The unit is designed to be attached to a wall or used standalone on a
counter.  It contains an internal battery and can operate for many
hours on battery power.  It requires a WiFi network to connect to the
invoice api.

The current software supports three "presets" for commonly purchased
item descriptions and prices and an "other" item with a dynamically
specified price.

The [Parts List](parts-list.md) describes all needed parts.

The case can be 3D printed from the provided STL files.

## Assembly Directions

*directions coming soon*

## Setup Arduino IDE

Start with the [SparkFun Software Setup
Directions](https://learn.sparkfun.com/tutorials/esp32-thing-plus-hookup-guide#software-setup)
to install the IDE and establish basic functionality.

Next, from "Manage Libraries" install:
* GxEPD2
* ArduinoJson
* QRCode
* AdaFruit GFX
* Keypad

## Compile and Upload

To configure the PoS terminal please copy config.h.template to
config.h and edit as appropriate for your situation.

Press the "Upload" button to compile and load into the Arduino.
