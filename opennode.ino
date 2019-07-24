// Copyright 2019 Bonsai Software, Inc.  All Rights Reserved.

// OpenNode config
const char* g_host = "api.opennode.co";
const int g_httpsPort = 443;
String g_hints = "false";

void opn_rate() {
    // Loop until we succeed
    while (true) {
        Serial.printf("opn_rate updating %s\n", cfg_opn_currency.c_str());
        displayText(10, 100, "Updating " + cfg_opn_currency + " ...");

        WiFiClientSecure client;

        if (!client.connect(g_host, g_httpsPort)) {
            Serial.printf("opn_rate connect failed\n");
            loopUntilConnected();
        }

        String url = "/v1/rates";

        client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                     "Host: " + g_host + "\r\n" +
                     "User-Agent: ESP32\r\n" +
                     "Connection: close\r\n\r\n");

        while (client.connected()) {
            String line = client.readStringUntil('\n');
            if (line == "\r") {
                break;
            }
        }
        String line = client.readStringUntil('\n');

        const size_t capacity =
            169*JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(168) + 3800;
            
        DynamicJsonDocument doc(capacity);
        DeserializationError retval = deserializeJson(doc, line);
        if (retval == DeserializationError::NoMemory) {
            continue;	// retry
        } else if (retval != DeserializationError::Ok) {
            Serial.printf("deserializeJson failed: %s\n", retval.c_str());
            continue; 	// retry
        }
        
        String temp =
            doc["data"][cfg_opn_currency][cfg_opn_currency.substring(3)];
        g_ratestr = temp;
        Serial.printf("1 BTC = %s %s\n",
                      g_ratestr.c_str(), cfg_opn_currency.substring(3).c_str());
        return;
    }
}
payreq_t opn_fetchpayment(){
    WiFiClientSecure client;

    while (true) {
        Serial.printf("fetchpayment %lu\n", g_sats);
    
        if (!client.connect(g_host, g_httpsPort)) {
            Serial.printf("fetchpayment connect failed\n");
            loopUntilConnected();
        }

        String SATSAMOUNT = String(g_sats);
        String topost =
            "{  \"amount\": \"" + SATSAMOUNT + "\", \"description\": \"" +
            cfg_prefix + cfg_presets[g_preset].title +
            "\", \"route_hints\": \"" + g_hints + "\"}";
        String url = "/v1/charges";

        client.print(String("POST ") + url + " HTTP/1.1\r\n" +
                     "Host: " + g_host + "\r\n" +
                     "User-Agent: ESP32\r\n" +
                     "Authorization: " + cfg_opn_apikey + "\r\n" +
                     "Content-Type: application/json\r\n" +
                     "Connection: close\r\n" +
                     "Content-Length: " + topost.length() + "\r\n" +
                     "\r\n" +
                     topost + "\n");

        while (client.connected()) {
            String line = client.readStringUntil('\n');
            if (line == "\r") {
                break;
            }
        }
        String line = client.readStringUntil('\n');

        const size_t capacity =
            169*JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(168) + 3800;
        
        DynamicJsonDocument doc(capacity);
        DeserializationError retval = deserializeJson(doc, line);
        if (retval == DeserializationError::NoMemory) {
            continue;	// retry
        } else if (retval != DeserializationError::Ok) {
            Serial.printf("deserializeJson failed: %s\n", retval.c_str());
            continue; 	// retry
        }
        
        String id = doc["data"]["id"];
        String payreq = doc["data"]["lightning_invoice"]["payreq"];

        // Retry if we don't have a payment request
        if (payreq.length() == 0) {
            Serial.printf("fetchpayment failed, retrying\n");
        } else {
            Serial.printf("fetchpayment -> %d %s\n", id, payreq.c_str());
            return { id, payreq };
        }
    }
}

// Check the status of the payment, return true if it has been paid.
bool opn_checkpayment(String PAYID){

    WiFiClientSecure client;

    Serial.printf("checkpayment %s\n", PAYID.c_str());

    if (!client.connect(g_host, g_httpsPort)) {
        Serial.printf("checkpayment connect failed\n");
        loopUntilConnected();
    }

    String url = "/v1/charge/" + PAYID;

    client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                 "Host: " + g_host + "\r\n" +
                 "Authorization: " + cfg_opn_apikey + "\r\n" +
                 "User-Agent: ESP32\r\n" +
                 "Connection: close\r\n\r\n");

    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") {
            break;
        }
    }
    String line = client.readStringUntil('\n');

    const size_t capacity =
        JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(4) +
        JSON_OBJECT_SIZE(14) + 650;
        
    DynamicJsonDocument doc(capacity);
    DeserializationError retval = deserializeJson(doc, line);
    if (retval == DeserializationError::NoMemory) {
        return false;
    } else if (retval != DeserializationError::Ok) {
        Serial.printf("deserializeJson failed: %s\n", retval.c_str());
        return false;
    }
        
    String stat = doc["data"]["status"];
    Serial.printf("checkpayment -> %s\n", stat.c_str());
    return stat == "paid";
}