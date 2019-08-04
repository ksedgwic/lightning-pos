// Copyright 2019 Bonsai Software, Inc.  All Rights Reserved.

/**
 *  Flux Capacitor PoS Terminal - a point of sale terminal which can
 *  accept bitcoin via lightning network
 *
 *  Epaper PIN MAP: [
 *      VCC - 3.3V
 *      GND - GND
 *      SDI - GPIO18
 *     SCLK - GPIO5
 *       CS - GPIO21
 *      D/C - GPIO17
 *    Reset - GPIO16
 *     Busy - GPIO4
 *  ]
 *
 *  Keypad Matrix PIN MAP: [
 *     Pin8 - GPIO13
 *        7 - GPIO12
 *        6 - GPIO27
 *        5 - GPIO33
 *        4 - GPIO15
 *        3 - GPIO32
 *        2 - GPIO14
 *     Pin1 - GPIO22
 *  ]
 *
 *  BLUE LED PIN MAP: [
 *    POS (long leg)  - GPIO25
 *    NEG (short leg) - GND
 *  ]
 *
 *  GREEN LED PIN MAP: [
 *    POS (long leg)  - GPIO26
 *    NEG (short leg) - GND
 *  ]
 *
 */

#include <WiFiClientSecure.h>

#include <ArduinoJson.h> //Use version 5.3.0!
#include <GxEPD2_BW.h>
#include <qrcode.h>
#include <string.h>

#include <Keypad.h>

#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

struct wifi_conf_t {
    String ssid;
    String pass;
};

struct preset_t {
    String title;
    float price;
};

#include "config.h"

// fiat/btc price
double g_rate;
unsigned long g_rate_tstamp = 0;

struct payreq_t {
    String id;
    String invoice;
};

//Set keypad
const byte rows_ = 4;
const byte cols_ = 4;
char keys_[rows_][cols_] = {
                         {'1','2','3','A'},
                         {'4','5','6','B'},
                         {'7','8','9','C'},
                         {'*','0','#','D'}
};
byte rowPins_[rows_] = {13, 12, 27, 33};
byte colPins_[cols_] = {15, 32, 14, 22};
Keypad g_keypad = Keypad( makeKeymap(keys_), rowPins_, colPins_, rows_, cols_ );

GxEPD2_BW<GxEPD2_154, GxEPD2_154::HEIGHT> g_display(
        GxEPD2_154(
                   /*CS=*/   21,
                   /*DC=*/   17,
                   /*RST=*/  16,
                   /*BUSY=*/ 4));

char g_keybuf[20];
unsigned long g_sats;
double g_fiat;
int g_preset = -1;

void setup() {
    pinMode(25, OUTPUT);	// Blue LED
    digitalWrite(25, HIGH);
    
    g_display.init(115200);

    displayText(20, 100, "Loading ...");

    Serial.begin(115200);

    while (!Serial);
    
    setupNetwork();
    Serial.println("connected");

    pinMode(26, OUTPUT);	// Green LED

    // Refresh the exchange rate.
    checkrate();
}

void loop() {
    g_preset = -1;
    displayMenu();
    while (g_preset == -1) {
        char key = g_keypad.getKey();
        switch (key) {
        case NO_KEY:
            break;
        case 'A':
        case 'B':
        case 'C':
        case 'D':
            g_preset = key - 'A';
            break;
        default:
            break;
        }
    }

    memset(g_keybuf, 0, sizeof(g_keybuf));

    // Collect amount, if none return to main menu.
    if (!keypadamount()) {
        return;
    }
    Serial.printf("pay %d %f %lu\n", g_preset, g_fiat, g_sats);

    payreq_t payreq;
    if (cfg_invoice_api == "OPN") {
        payreq = opn_createinvoice();
    } else if (cfg_invoice_api == "BTP") {
        payreq = btp_createinvoice();
    } else if (cfg_invoice_api == "LND") {
        payreq = lnd_createinvoice();
    }
    
    if (payreq.id == "") {
        return;
    }

    if (!displayQR(&payreq)) {
        return;
    }

    waitForPayment(&payreq);
}

void setupNetwork() {
    int nconfs = sizeof(cfg_wifi_confs) / sizeof(wifi_conf_t);
    Serial.printf("scanning %d wifi confs\n", nconfs);
    int ndx = 0;
    while (true) {
        const char* ssid = cfg_wifi_confs[ndx].ssid.c_str();
        const char* pass = cfg_wifi_confs[ndx].pass.c_str();

        Serial.printf("trying %s\n", ssid);
        displayText(10, 100, String("Trying ") + ssid);

        WiFi.begin(ssid, pass);

        // Poll the status for a while.
        for (int nn = 0; nn < 50; ++nn) {
            if (WiFi.status() == WL_CONNECTED) {
                Serial.printf("connected to %s\n", ssid);
                return;
            }
            delay(100);
        }

        // Try the next access point, wrap.
        if (++ndx == nconfs) {
            ndx = 0;
        }
    }
}

void displayText(int col, int row, String txt) {
    g_display.firstPage();
    do
    {
        g_display.setRotation(1);
        g_display.setPartialWindow(0, 0, 200, 200);
        g_display.fillScreen(GxEPD_WHITE);
        g_display.setFont(&FreeSansBold9pt7b);
        g_display.setTextColor(GxEPD_BLACK);
        g_display.setCursor(col, row);
        g_display.println(txt);
    }
    while (g_display.nextPage());
}

void displayMenu() {
    Serial.printf("displayMenu\n");
    g_display.firstPage();
    do
    {
        g_display.setRotation(1);
        g_display.setPartialWindow(0, 0, 200, 200);
        g_display.fillScreen(GxEPD_WHITE);
        g_display.setFont(&FreeSansBold18pt7b);
        g_display.setTextColor(GxEPD_BLACK);
        g_display.setCursor(0, 37);
        g_display.println(cfg_title[0]);
        g_display.println(cfg_title[1]);
        g_display.setFont(&FreeSansBold9pt7b);
        for (int ndx = 0; ndx < 4; ++ndx) {
            char but = 'A' + ndx;
            g_display.printf("  %c - %s\n",
                             but, cfg_presets[ndx].title.c_str());
        }
    }
    while (g_display.nextPage());
}

int applyPreset() {
    checkrate();
    String centstr = String(long(cfg_presets[g_preset].price * 100));
    memset(g_keybuf, 0, sizeof(g_keybuf));
    memcpy(g_keybuf, centstr.c_str(), centstr.length());
    Serial.printf("applyPreset %d g_keybuf=%s\n", g_preset, g_keybuf);
    displayAmountPage();
    showPartialUpdate(g_keybuf);
    return centstr.length();
}

//Function for keypad
unsigned long keypadamount() {
    // Refresh the exchange rate.
    applyPreset();
    int checker = 0;
    while (checker < sizeof(g_keybuf)) {
        char key = g_keypad.getKey();
        switch (key) {
        case NO_KEY:
            break;
        case '#':
            displayText(10, 100, "Generating QR ...");
            return true;
        case '*':
            if (cfg_presets[g_preset].price != 0.00) {
                // Pressing '*' with preset value returns to main menu.
                return false;
            } else {
                // Otherwise clear value and stay on screen.
                checker = applyPreset();
                break;
            }
        case 'A':
        case 'B':
        case 'C':
        case 'D':
            g_preset = key - 'A';
            checker = applyPreset();
            break;
        default:
            g_keybuf[checker] = key;
            checker++;
            Serial.printf("g_keybuf=%s\n", g_keybuf);
            showPartialUpdate(g_keybuf);
            break;
        }
    }
    // Only get here when we overflow the g_keybuf.
    return false;
}

void displayAmountPage() {
    g_display.firstPage();
    do
    {
        g_display.setRotation(1);
        g_display.setPartialWindow(0, 0, 200, 200);
        g_display.fillScreen(GxEPD_WHITE);
        g_display.setFont(&FreeSansBold12pt7b);
        g_display.setTextColor(GxEPD_BLACK);

        g_display.setCursor(0, 30);
        g_display.println(" " + cfg_presets[g_preset].title);

        g_display.setCursor(0, 60);
        if (cfg_presets[g_preset].price == 0.00) {
            g_display.println(" Enter Amount");
        } else {
            g_display.println();
        }
        
        g_display.setCursor(0, 95);
        g_display.println(" " + cfg_opn_currency.substring(3) + ": ");
        g_display.println(" Sats: ");

        g_display.setFont(&FreeSansBold9pt7b);
        g_display.setCursor(0, 160);
        if (cfg_presets[g_preset].price == 0.00) {
            g_display.println("   Press * to clear");
        } else {
            g_display.println("   Press * to cancel");
        }
        g_display.println("   Press # to submit");

    }
    while (g_display.nextPage());
}

// Display current amount
void showPartialUpdate(String centsStr) {

    g_fiat = centsStr.toFloat() / 100.0;

    // Convert fiat to sats, add 0.9999 and truncate (ceiling)
    g_sats = long((g_fiat * 100e6 / g_rate) + 0.9999);

    g_display.firstPage();
    do
    {
        g_display.setRotation(1);
        g_display.setFont(&FreeSansBold12pt7b);
        g_display.setTextColor(GxEPD_BLACK);

        // g_display.fillRect(box_x, box_y, box_w, box_h, GxEPD_WHITE);
        g_display.setPartialWindow(70, 75, 120, 20);
        g_display.setCursor(70, 95);
        g_display.print(g_fiat);

    }
    while (g_display.nextPage());

    g_display.firstPage();
    do
    {
        g_display.setRotation(1);
        g_display.setFont(&FreeSansBold12pt7b);
        g_display.setTextColor(GxEPD_BLACK);

        // g_display.fillRect(box_x, box_y, box_w, box_h, GxEPD_WHITE);
        g_display.setPartialWindow(70, 105, 120, 20);
        g_display.setCursor(70, 125);
        g_display.print(g_sats);

    }
    while (g_display.nextPage());
}
    
bool displayQR(payreq_t * payreqp) {
    // This code from https://github.com/arcbtc/koopa/blob/master/main.ino
    
    // auto detect best qr code size
    int qrSize = 10;
    int ec_lvl = 3;
    int const sizes[18][4] = {
                        /* https://github.com/ricmoo/QRCode */
                        /* 1 */ { 17, 14, 11, 7 },
                        /* 2 */ { 32, 26, 20, 14 },
                        /* 3 */ { 53, 42, 32, 24 },
                        /* 4 */ { 78, 62, 46, 34 },
                        /* 5 */ { 106, 84, 60, 44 },
                        /* 6 */ { 134, 106, 74, 58 },
                        /* 7 */ { 154, 122, 86, 64 },
                        /* 8 */ { 192, 152, 108, 84 },
                        /* 9 */ { 230, 180, 130, 98 },
                        /* 10 */ { 271, 213, 151, 119 },
                        /* 11 */ { 321, 251, 177, 137 },
                        /* 12 */ { 367, 287, 203, 155 },
                        /* 13 */ { 425, 331, 241, 177 },
                        /* 14 */ { 458, 362, 258, 194 },
                        /* 15 */ { 520, 412, 292, 220 },
                        /* 16 */ { 586, 450, 322, 250 },
                        /* 17 */ { 644, 504, 364, 280 },
    };
    int len = payreqp->invoice.length();
    for(int ii=0; ii<18; ii++){
        qrSize = ii+1;
        if(sizes[ii][ec_lvl] > len){
            break;
        }
    }

    Serial.printf("len = %d, ec_lvl = %d, qrSize = %d\n",
                  len, ec_lvl, qrSize);

    // Create the QR code
    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(qrSize)];
    qrcode_initText(&qrcode, qrcodeData, qrSize, ec_lvl,
                    payreqp->invoice.c_str());

    int width = 17 + 4*qrSize;
    int scale = 190/width;
    int padding = (200 - width*scale)/2;

    g_display.firstPage();
    do
    {
        g_display.setPartialWindow(0, 0, 200, 200);
        g_display.fillScreen(GxEPD_WHITE);
        // for every pixel in QR code we draw a rectangle with size `scale`
        for (uint8_t y = 0; y < qrcode.size; y++) {
            for (uint8_t x = 0; x < qrcode.size; x++) {
                if(qrcode_getModule(&qrcode, x, y)){
                    g_display.fillRect(padding+scale*x,
                                       padding+scale*y,
                                       scale, scale, GxEPD_BLACK);
                }
            }
        }
    }
    while (g_display.nextPage());
}

// Handle outcome
void waitForPayment(payreq_t * payreqp) {
    int counta = 0;
    int ispaid = 0;
    if (cfg_invoice_api == "OPN") {
        ispaid = opn_checkpayment(payreqp->id);
    } else if (cfg_invoice_api == "BTP") {
        ispaid = btp_checkpayment(payreqp->id);
    } else if (cfg_invoice_api == "LND") {
        ispaid = lnd_checkpayment(payreqp->id);
    }
    
    while (counta < 40) {
        switch (ispaid) {
        case -1: // error while checking, not paid after reconnect
            // Need to redisplay the qr code
            displayQR(payreqp);
            // fallthrough on purpose
        case 0: // not paid yet
            // Delay, checking for abort.
            for (int nn = 0; nn < 10; ++nn) {
                if (g_keypad.getKey() == '*') {
                    return;
                }
                delay(10);
            }
            if (cfg_invoice_api == "OPN") {
                ispaid = opn_checkpayment(payreqp->id);
            } else if (cfg_invoice_api == "BTP") {
                ispaid = btp_checkpayment(payreqp->id);
            } else if (cfg_invoice_api == "LND") {
                ispaid = lnd_checkpayment(payreqp->id);
            }
            counta++;
            break;
        case 1: // paid
            // Display big success message.
            g_display.firstPage();
            do
            {
                g_display.setRotation(1);
                g_display.setPartialWindow(0, 0, 200, 200);
                g_display.fillScreen(GxEPD_WHITE);
                g_display.setFont(&FreeSansBold18pt7b);
                g_display.setTextColor(GxEPD_BLACK);
                g_display.setCursor(50, 80);
                g_display.println("Got it");
                g_display.setCursor(4, 130);
                g_display.println("Thank you!");
            }
            while (g_display.nextPage());
            
            digitalWrite(26, HIGH);
            delay(8000);
            digitalWrite(26, LOW);
            delay(500);
            counta = 40;
            break;
        }
    }
}

///////////////////////////// GET/POST REQUESTS///////////////////////////

void checkrate() {
    // If we have a prior rate that is not wrapped and is fresh enough
    // we're done.
    unsigned long now = millis();
    Serial.printf("checkrate %lu %lu\n", g_rate_tstamp, now);
    if (g_rate_tstamp != 0 &&	/* not first time */
        now > g_rate_tstamp &&	/* wraps after 50 days */
        now - g_rate_tstamp < (10 * 60 * 1000) /* 10 min old */) {
        return;
    }
            
    if (cfg_rate_feed == "OPN") {
        opn_rate();
    } else if (cfg_rate_feed == "BTP") {
        btp_rate();
    } else if (cfg_rate_feed == "CMC") {
        cmc_rate();
    } else {
        Serial.printf("unknown cfg_rate_feed \"%s\"\n", cfg_rate_feed);
        return;	// TODO - what to do here?
    }
    
    g_rate_tstamp = now;
}


