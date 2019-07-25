// Copyright 2019 Bonsai Software, Inc.  All Rights Reserved.

String cmc_host = "pro-api.coinmarketcap.com";
int cmc_port = 443;
String cmc_url = "/v1/cryptocurrency/quotes/latest";

void cmc_rate() {
    // Loop until we succeed
    while (true) {
        Serial.printf("cmc_rate updating BTC%s\n", cfg_cmc_currency.c_str());
        displayText(10, 100, "Updating BTC" + cfg_cmc_currency + " ...");

        WiFiClientSecure client;

        while (!client.connect(cmc_host.c_str(), cmc_port)) {
            Serial.printf("cmc_rate connect failed\n");
            setupNetwork();
        }

        String args = "?convert=" + cfg_cmc_currency + "&symbol=BTC";

        client.print(String("GET ") + cmc_url + args + " HTTP/1.1\r\n" +
                     "Host: " + cmc_host + "\r\n" +
                     "User-Agent: ESP32\r\n" +
                     "X-CMC_PRO_API_KEY: " + cfg_cmc_apikey + "\r\n" +
                     "Accept: application/json\r\n" +
                     "Connection: close\r\n\r\n");

        while (client.connected()) {
            String line = client.readStringUntil('\n');
            if (line == "\r") {
                break;
            }
        }
        // Discard the length of the returned payload
        String line = client.readStringUntil('\n');

        // Read the rest of the payload
        line = client.readString();
        
        const size_t capacity =
            JSON_ARRAY_SIZE(1) + 2*JSON_OBJECT_SIZE(1) +
            JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(5) +
            JSON_OBJECT_SIZE(7) + JSON_OBJECT_SIZE(14) + 563 + 2048;
            
        DynamicJsonDocument doc(capacity);
        DeserializationError retval = deserializeJson(doc, line);
        if (retval == DeserializationError::NoMemory) {
            continue;	// retry
        } else if (retval != DeserializationError::Ok) {
            Serial.printf("deserializeJson failed: %s\n", retval.c_str());
            continue; 	// retry
        }
        
        String temp = doc["data"]["BTC"]["quote"][cfg_cmc_currency]["price"];
        g_ratestr = temp;
        Serial.printf("1 BTC = %s %s\n",
                      g_ratestr.c_str(), cfg_cmc_currency.c_str());
        return;
    }
}
