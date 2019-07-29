// Copyright 2019 Bonsai Software, Inc.  All Rights Reserved.

// BTCPay REST interface

void btp_rate() {
    // Loop until we succeed
    while (true) {
        Serial.printf("btp_rate updating BTC%s\n", cfg_btp_currency.c_str());
        displayText(10, 100, "Updating BTC" + cfg_btp_currency + " ...");

        WiFiClientSecure client;

        while (!client.connect(btp_host.c_str(), btp_port)) {
            Serial.printf("btp_rate connect failed\n");
            setupNetwork();
        }

        if (cfg_btp_fingerprint.length() > 0 &&
            !client.verify(cfg_btp_fingerprint.c_str(), NULL)) {
            Serial.printf("btp_rate verify failed\n");
            continue;
        }
        
        String args = "?convert=" + cfg_btp_currency + "&symbol=BTC";

        client.print(String("GET ") + btp_url + args + " HTTP/1.1\r\n" +
                     "Host: " + btp_host + "\r\n" +
                     "User-Agent: ESP32\r\n" +
                     "X-BTP_PRO_API_KEY: " + cfg_btp_apikey + "\r\n" +
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
        
        String temp = doc["data"]["BTC"]["quote"][cfg_btp_currency]["price"];
        g_ratestr = temp;
        Serial.printf("1 BTC = %s %s\n",
                      g_ratestr.c_str(), cfg_btp_currency.c_str());
        return;
    }
}

payreq_t btp_createinvoice() {
    WiFiClientSecure client;

    while (true) {
        Serial.printf("btp_createinvoice %lu\n", g_sats);

        // client.setCACert(cfg_btp_tlscert.c_str());
        
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
        
        String url = "/v1/invoices";
        
        String postdata =
            "{\"memo\":\"" + cfg_prefix + cfg_presets[g_preset].title + "\", " +
            "\"value\":\"" + String(g_sats) + "\"}";

        client.print(String("POST ") + url + " HTTP/1.1\r\n" +
                     "Host: " + cfg_btp_host + "\r\n" +
                     "User-Agent: ESP32\r\n" +
                     "Grpc-Metadata-macaroon: " + cfg_btp_inv_macaroon +"\r\n" +
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
        String line = client.readStringUntil('\n');
        
        const size_t capacity = JSON_OBJECT_SIZE(3) + 293 + 1000;
        
        DynamicJsonDocument doc(capacity);
        DeserializationError retval = deserializeJson(doc, line);
        if (retval == DeserializationError::NoMemory) {
            continue;	// retry
        } else if (retval != DeserializationError::Ok) {
            Serial.printf("deserializeJson failed: %s\n", retval.c_str());
            continue; 	// retry
        }
        
        String r_hash = doc["r_hash"];
        String payreq = doc["payment_request"];

        unsigned char id_bin[32];
        unsigned int id_bin_len =
            decode_base64((unsigned char *)r_hash.c_str(), id_bin);

        String id;
        for (int ndx = 0; ndx < id_bin_len; ++ndx) {
            char buffer[3];
            sprintf(buffer, "%02x", id_bin[ndx]);
            id += buffer;
        }
        
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
        
    String url = "/v1/invoice/" + PAYID;
    
    client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                 "Host: " + cfg_btp_host + "\r\n" +
                 "User-Agent: ESP32\r\n" +
                 "Grpc-Metadata-macaroon: " + cfg_btp_inv_macaroon +"\r\n" +
                 "Connection: close\r\n\r\n");

    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") {
            break;
        }
    }
    String line = client.readString();

    const size_t capacity = JSON_OBJECT_SIZE(16) + 554 + 2048;
            
    DynamicJsonDocument doc(capacity);
    DeserializationError retval = deserializeJson(doc, line);
    if (retval == DeserializationError::NoMemory) {
        return false;
    } else if (retval != DeserializationError::Ok) {
        Serial.printf("deserializeJson failed: %s\n", retval.c_str());
        return false;
    }

    String state = doc["state"];
    Serial.printf("btp_checkpayment -> %s\n", state.c_str());
    return state == "SETTLED";
}
