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

#include <bip39.h>

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

Bip39 bip39;

void do_bip39_stuff() {
    char * entropy = "123456";
    Sha256Class sha256;
    sha256.init();
    for(uint8_t ii=0; ii < strlen(entropy); ii++) {
        sha256.write(entropy[ii]);
    }
    uint8_t payload[16];
    memcpy(payload, sha256.result(), sizeof(payload));
    bip39.setPayloadBytes(sizeof(payload));
    bip39.setPayload(sizeof(payload), (uint8_t *)payload);
    for (int ndx = 0; ndx < 12; ++ndx) {
        uint16_t word = bip39.getWord(ndx);
        Serial.printf("%d %s\n", ndx, bip39.getMnemonic(word));
    }
}

void setup() {
    pinMode(25, OUTPUT);	// Blue LED
    digitalWrite(25, HIGH);
    
    g_display.init(115200);

    displayText(20, 100, "Loading ...");

    Serial.begin(115200);

    while (!Serial);
    
    do_bip39_stuff();
    
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
            g_display.printf("  %c - %s\n", but, cfg_presets[ndx].title.c_str());
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
    
//Set other Arduino Strings used
String g_qrline = "";
String g_hexvalues = "";

// QR maker function
void qrmmaker(String xxx){
    int str_len = xxx.length() + 1;
    char xxxx[str_len];
    xxx.toCharArray(xxxx, str_len);

    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(11)];
    qrcode_initText(&qrcode, qrcodeData, 11, 0, xxxx);

    int une = 0;

    g_qrline = "";

    for (uint8_t y = 0; y < qrcode.size; y++) {

        // Each horizontal module
        for (uint8_t x = 0; x < qrcode.size; x++) {
            g_qrline += (qrcode_getModule(&qrcode, x, y) ? "111": "000");
        }
        g_qrline += "1";
        for (uint8_t x = 0; x < qrcode.size; x++) {
            g_qrline += (qrcode_getModule(&qrcode, x, y) ? "111": "000");
        }
        g_qrline += "1";
        for (uint8_t x = 0; x < qrcode.size; x++) {
            g_qrline += (qrcode_getModule(&qrcode, x, y) ? "111": "000");
        }
        g_qrline += "1";
    }
}

//Char for holding the QR byte array
unsigned char PROGMEM g_singlehex[4209];

// Display QRcode
bool displayQR(payreq_t * payreqp) {
    String setoffour = "";
    String result = "";
 
    //Char dictionary for conversion from 1s and 0s
    const char ref[2][16][5]={
        {
         "0000","0001","0010","0011","0100","0101","0110","0111",
         "1000","1001","1010","1011","1100","1101","1110","1111"
        },
        {
         "0","1","2","3","4","5","6","7","8","9","a","b","c","d","e","f"
        }
    };

    g_hexvalues = "";

    qrmmaker(payreqp->invoice);

    for (int i = 0;  i < g_qrline.length(); i+=4) {
        int tmp = i;
        setoffour = g_qrline.substring(tmp, tmp+4);

        for (int z = 0; z < 16; z++){
            if (setoffour == ref[0][z]){
                g_hexvalues += ref[1][z];
            }
        }
    }

    g_qrline = "";

    //for loop to build the epaper friendly char singlehex byte array
    //image of the QR
    for (int i = 0;  i < 4209; i++) {
        int tmp = i;
        int pmt = tmp*2;
        result = "0x" + g_hexvalues.substring(pmt, pmt+2) + ",";
        g_singlehex[tmp] =
            (unsigned char)strtol(g_hexvalues.substring(pmt, pmt+2).c_str(),
                                  NULL, 16);
    }

    g_display.firstPage();
    do
    {
        g_display.setPartialWindow(0, 0, 200, 200);
        g_display.fillScreen(GxEPD_WHITE);
        g_display.drawBitmap( 7, 7, g_singlehex, 184, 183, GxEPD_BLACK);

    }
    while (g_display.nextPage());

    return true;
}

// Handle outcome
void waitForPayment(payreq_t * payreqp) {
    int counta = 0;
    bool ispaid = false;
    if (cfg_invoice_api == "OPN") {
        ispaid = opn_checkpayment(payreqp->id);
    } else if (cfg_invoice_api == "BTP") {
        ispaid = btp_checkpayment(payreqp->id);
    } else if (cfg_invoice_api == "LND") {
        ispaid = lnd_checkpayment(payreqp->id);
    }
    
    while (counta < 40) {
        if (!ispaid) {
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
        }
        else
        {
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


