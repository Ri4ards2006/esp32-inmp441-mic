/**
 * @file transport.cpp
 *
 * Why UDP: one frame = one self-describing datagram (seq + sample_index), so loss is
 * detectable, nothing is ever retransmitted and a slow/missing receiver cannot create
 * back-pressure into the audio path. TCP/WebSocket would add head-of-line blocking
 * and buffering latency for no benefit on a LAN monitoring link.
 */
#include "transport.h"

#include <Arduino.h>

#if __has_include("secrets.h")
#include "secrets.h"
#define MIC_TRANSPORT_ENABLED 1
#include <WiFi.h>
#include <WiFiUdp.h>
#else
#define MIC_TRANSPORT_ENABLED 0
#endif

namespace transport {

#if MIC_TRANSPORT_ENABLED

namespace {
WiFiUDP g_udp;
IPAddress g_host;
}  // namespace

bool begin() {
    if (!g_host.fromString(MIC_UDP_HOST)) {
        Serial.println("[net ] invalid MIC_UDP_HOST, transport disabled");
        return false;
    }
    WiFi.mode(WIFI_STA);
    // Modem sleep adds up to one DTIM interval of latency/jitter per packet;
    // a continuous 31 pkt/s stream should keep the radio awake.
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(MIC_WIFI_SSID, MIC_WIFI_PASS);  // returns immediately
    g_udp.begin(0);                            // ephemeral local port
    Serial.printf("[net ] connecting to SSID '%s', target %s:%u\r\n", MIC_WIFI_SSID,
                  MIC_UDP_HOST, static_cast<unsigned>(MIC_UDP_PORT));
    return true;
}

bool ready() { return WiFi.status() == WL_CONNECTED; }

bool send(const uint8_t* data, size_t len) {
    if (!ready()) return false;
    if (!g_udp.beginPacket(g_host, MIC_UDP_PORT)) return false;
    g_udp.write(data, len);
    return g_udp.endPacket() == 1;
}

#else

bool begin() {
    Serial.println("[net ] no include/secrets.h -> network transport disabled");
    return false;
}
bool ready() { return false; }
bool send(const uint8_t*, size_t) { return false; }

#endif

}  // namespace transport
