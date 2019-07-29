Lightning Pay Station
================================================================

![Lightning Pay Station](lightning-pos.jpg)

Many thanks to [arcbtc](https://github.com/arcbtc), his 
[OpenNode Tutorial](https://github.com/arcbtc/bitcoin2019conf)
and gift of working kit totally bootstrapped this project.

The goal of this project is to build an inexpensive open source
point-of-sale terminal for lightning network payments.  The
terminal can connect to:
1. OpenNode Accounts
2. BTCPay's Invoice API
3. LND's REST Interface

The unit is designed to be attached to a wall or used standalone on a
counter.  It contains an internal battery and can operate for many
hours on battery power.  It requires a WiFi network to connect to the
invoice api.

The current software supports three "presets" for commonly purchased
item descriptions and prices and an "other" item with a dynamically
specified price.

The [Parts List](parts-list.md) describes all needed parts.

The case can be 3D printed from the provided STL files.

The [Assembly Instructions](assembly.md) show how to put it together.

## Create Account at OpenNode

Create an account at [OpenNode](https://app.opennode.co/join/fad8135d-ed69-4811-840c-bfa4e30df563).

Navigate to Settings -> Integrations -> API keys.

Select "Add key" and set the permissions to "Invoices".  Make a note
of the Invoice API key string.

## Setup Arduino IDE

Start with the [SparkFun Software Setup
Directions](https://learn.sparkfun.com/tutorials/esp32-thing-plus-hookup-guide#software-setup)
to install the IDE and establish basic functionality.

Next, from "Manage Libraries" install:
```
* GxEPD2
* Arduinojson
* QRCode
* AdaFruit GFX
* Keypad
* base64
```

Install pyserial:

    pip install --user pyserial

## Compile and Upload

To configure the PoS terminal please copy config.h.template to
config.h and edit as appropriate for your setup.

Press the "Upload" button to compile and load into the Arduino.
