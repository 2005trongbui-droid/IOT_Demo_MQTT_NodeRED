#pragma once

// Default profile for Wokwi. Wokwi uses the public broker and has no
// authentication/ACL. For a secured local Mosquitto broker, copy this file to
// mqtt_secrets.h (ignored by Git) and replace the Wi-Fi, host, username, and
// password values as described in README.md.
#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""

#define MQTT_HOST "test.mosquitto.org"
#define MQTT_PORT 1883
#define MQTT_USERNAME ""
#define MQTT_PASSWORD ""
