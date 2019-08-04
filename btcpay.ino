// Copyright 2019 Bonsai Software, Inc.  All Rights Reserved.

// BTCPay REST interface

void btp_rate() {
    // Loop until we succeed
    while (true) {
        Serial.printf("btp_rate updating BTC%s\n", cfg_btp_currency.c_str());
        displayText(10, 100, "BTCPay BTC" + cfg_btp_currency + " ...");

        WiFiClientSecure client;

        while (!client.connect(cfg_btp_host.c_str(), cfg_btp_port)) {
            Serial.printf("btp_rate connect failed\n");
            setupNetwork();
        }

        if (cfg_btp_fingerprint.length() > 0 &&
            !client.verify(cfg_btp_fingerprint.c_str(), NULL)) {
            Serial.printf("btp_rate verify failed\n");
            continue;
        }

        String url = "/rates/" + cfg_btp_currency + "/BTC";
        String args = "?storeId=" + cfg_btp_storeid;
        
        Serial.printf("%s%s\n", url.c_str(), args.c_str());
        
        client.print(String("GET ") + url + args + " HTTP/1.1\r\n" +
                     "Host: " + cfg_btp_host + "\r\n" +
                     "User-Agent: ESP32\r\n" +
                     "Authorization: Basic " + cfg_btp_apikey + "\r\n" +
                     "accept: application/json\r\n" +
                     "X-accept-version: 2.0.0\r\n" +
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

        Serial.printf("%s\n", line.c_str());
        
        const size_t capacity =
            JSON_ARRAY_SIZE(1) + JSON_OBJECT_SIZE(1) +
            JSON_OBJECT_SIZE(5) + 91 + 2048;
            
        DynamicJsonDocument doc(capacity);
        DeserializationError retval = deserializeJson(doc, line);
        if (retval == DeserializationError::NoMemory) {
            continue;	// retry
        } else if (retval != DeserializationError::Ok) {
            Serial.printf("deserializeJson failed: %s\n", retval.c_str());
            continue; 	// retry
        }

        double rate = doc["data"]["rate"];
        g_rate = 1.0 / rate;
        Serial.printf("1 BTC = %f %s\n", g_rate, cfg_btp_currency.c_str());
        return;
    }
}

payreq_t btp_createinvoice() {
    WiFiClientSecure client;

    while (true) {
        Serial.printf("btp_createinvoice %f %lu\n", g_fiat, g_sats);

        while (!client.connect(cfg_btp_host.c_str(), cfg_btp_port)) {
            Serial.printf("btp_createinvoice %s %d connect failed\n",
                          cfg_btp_host.c_str(), cfg_btp_port);
            setupNetwork();
        }

        if (cfg_btp_fingerprint.length() > 0 &&
            !client.verify(cfg_btp_fingerprint.c_str(), NULL)) {
            Serial.printf("btp_createinvoice verify failed\n");
            continue;
        }
        
        String url = "/invoices";

        String postdata =
            "{\"itemDesc\":\"" + cfg_prefix + cfg_presets[g_preset].title + "\", " +
            "\"price\":\"" + String(g_fiat) + "\", " +
            "\"currency\":\"" + cfg_btp_currency + "\"" +
            "}";

        Serial.printf("%s\n", postdata.c_str());
        
        client.print(String("POST ") + url + " HTTP/1.1\r\n" +
                     "Host: " + cfg_btp_host + "\r\n" +
                     "User-Agent: ESP32\r\n" +
                     "Authorization: Basic " + cfg_btp_apikey + "\r\n" +
                     "accept: application/json\r\n" +
                     "X-accept-version: 2.0.0\r\n" +
                     "Content-Type: application/json\r\n" +
                     "Connection: close\r\n" +
                     "Content-Length: " + postdata.length() + "\r\n" +
                     "\r\n" +
                     postdata + "\n");

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
        
        Serial.printf("%s\n", line.c_str());

        const size_t capacity = 
            2*JSON_ARRAY_SIZE(0) + JSON_ARRAY_SIZE(2) +
            9*JSON_OBJECT_SIZE(1) + 6*JSON_OBJECT_SIZE(2) +
            5*JSON_OBJECT_SIZE(5) + JSON_OBJECT_SIZE(9) +
            2*JSON_OBJECT_SIZE(15) + JSON_OBJECT_SIZE(37) +
            3235 + 8192;
        
        DynamicJsonDocument doc(capacity);
        DeserializationError retval = deserializeJson(doc, line);
        if (retval == DeserializationError::NoMemory) {
            continue;	// retry
        } else if (retval != DeserializationError::Ok) {
            Serial.printf("deserializeJson failed: %s\n", retval.c_str());
            continue; 	// retry
        }

        String id = doc["data"]["id"];
        String payreq = doc["data"]["addresses"]["BTC_LightningLike"];
        
        // Retry if we don't have a payment request
        if (payreq.length() == 0) {
            Serial.printf("btp_createinvoice failed, retrying\n");
        } else {
            Serial.printf("btp_createinvoice -> %s %s\n",
                          id.c_str(), payreq.c_str());
            return { id, payreq };
        }
    }
}

bool btp_checkpayment(String PAYID) {
    WiFiClientSecure client;

    Serial.printf("btp_checkpayment %s\n", PAYID.c_str());

    while (!client.connect(cfg_btp_host.c_str(), cfg_btp_port)) {
        Serial.printf("btp_checkpayment %s %d connect failed\n",
                      cfg_btp_host.c_str(), cfg_btp_port);
        setupNetwork();
    }

    if (cfg_btp_fingerprint.length() > 0 &&
        !client.verify(cfg_btp_fingerprint.c_str(), NULL)) {
        Serial.printf("btp_checkpayment verify failed\n");
        return false;
    }
        
    String url = "/invoices/" + PAYID;
        
    client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                 "Host: " + cfg_btp_host + "\r\n" +
                 "User-Agent: ESP32\r\n" +
                 "Authorization: Basic " + cfg_btp_apikey + "\r\n" +
                 "Content-Type: application/json\r\n" +
                 "accept: application/json\r\n" +
                 "X-accept-version: 2.0.0\r\n" +
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

    // Serial.printf("%s\n", line.c_str());

    const size_t capacity =
        2*JSON_ARRAY_SIZE(0) + JSON_ARRAY_SIZE(2) +
        10*JSON_OBJECT_SIZE(1) + 5*JSON_OBJECT_SIZE(2) +
        5*JSON_OBJECT_SIZE(5) + JSON_OBJECT_SIZE(9) +
        2*JSON_OBJECT_SIZE(15) + JSON_OBJECT_SIZE(37) +
        3216 + 8192;

    DynamicJsonDocument doc(capacity);
    DeserializationError retval = deserializeJson(doc, line);
    if (retval == DeserializationError::NoMemory) {
        return false;
    } else if (retval != DeserializationError::Ok) {
        Serial.printf("deserializeJson failed: %s\n", retval.c_str());
        return false;
    }

    String status = doc["data"]["status"];
    Serial.printf("btp_checkpayment -> %s\n", status.c_str());
    return status == "complete";
}
