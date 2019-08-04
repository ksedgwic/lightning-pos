// Copyright 2019 Bonsai Software, Inc.  All Rights Reserved.

#include <base64.hpp>

// LND REST connection

payreq_t lnd_createinvoice() {
    WiFiClientSecure client;

    while (true) {
        Serial.printf("lnd_createinvoice %f %lu\n", g_fiat, g_sats);

        // client.setCACert(cfg_lnd_tlscert.c_str());
        
        while (!client.connect(cfg_lnd_host.c_str(), cfg_lnd_port)) {
            Serial.printf("lnd_createinvoice %s %d connect failed\n",
                          cfg_lnd_host.c_str(), cfg_lnd_port);
            setupNetwork();
        }

        if (cfg_lnd_fingerprint.length() > 0 &&
            !client.verify(cfg_lnd_fingerprint.c_str(), NULL)) {
            Serial.printf("lnd_createinvoice verify failed\n");
            continue;
        }
        
        String url = "/v1/invoices";
        
        String postdata =
            "{\"memo\":\"" + cfg_prefix + cfg_presets[g_preset].title + "\", " +
            "\"value\":\"" + String(g_sats) + "\"}";

        client.print(String("POST ") + url + " HTTP/1.1\r\n" +
                     "Host: " + cfg_lnd_host + "\r\n" +
                     "User-Agent: ESP32\r\n" +
                     "Grpc-Metadata-macaroon: " + cfg_lnd_inv_macaroon +"\r\n" +
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
            Serial.printf("lnd_createinvoice failed, retrying\n");
        } else {
            Serial.printf("lnd_createinvoice -> %s %s\n",
                          id.c_str(), payreq.c_str());
            return { id, payreq };
        }
    }
}

int lnd_checkpayment(String PAYID) {
    bool saw_error = false;
    WiFiClientSecure client;

    Serial.printf("lnd_checkpayment %s\n", PAYID.c_str());

    while (!client.connect(cfg_lnd_host.c_str(), cfg_lnd_port)) {
        Serial.printf("lnd_checkpayment %s %d connect failed\n",
                      cfg_lnd_host.c_str(), cfg_lnd_port);
        saw_error = true;
        setupNetwork();
    }

    if (cfg_lnd_fingerprint.length() > 0 &&
        !client.verify(cfg_lnd_fingerprint.c_str(), NULL)) {
        Serial.printf("lnd_checkpayment verify failed\n");
        return saw_error ? -1 : 0;
    }
        
    String url = "/v1/invoice/" + PAYID;
    
    client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                 "Host: " + cfg_lnd_host + "\r\n" +
                 "User-Agent: ESP32\r\n" +
                 "Grpc-Metadata-macaroon: " + cfg_lnd_inv_macaroon +"\r\n" +
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
        return saw_error ? -1 : 0;
    } else if (retval != DeserializationError::Ok) {
        Serial.printf("deserializeJson failed: %s\n", retval.c_str());
        return saw_error ? -1 : 0;
    }

    String state = doc["state"];
    Serial.printf("lnd_checkpayment -> %s\n", state.c_str());
    return state == "SETTLED" ? 1 : (saw_error ? -1 : 0);
}
