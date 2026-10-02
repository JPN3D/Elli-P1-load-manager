#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiManager.h>

#include <cstdlib>

#include "config.h"

namespace {
struct UserSettings {
  float fuseCurrentA = Config::DEFAULT_FUSE_CURRENT_A;
  float maxChargePowerKw = Config::DEFAULT_MAX_CHARGE_POWER_KW;
  char homeWizardHost[64] = {};
  char elliHost[64] = {};
  bool configured = false;
};

struct P1Reading {
  float phasePowerW[3] = {};
  uint32_t receivedAtMs = 0;
  bool valid = false;
};

P1Reading latestReading;
UserSettings userSettings;
Preferences preferences;
uint32_t lastPollMs = 0;
uint32_t lastWifiAttemptMs = 0;
uint32_t lastElliCheckMs = 0;
uint32_t elliLostSinceMs = 0;
uint32_t lastStatusReportMs = 0;
bool elliConnectionKnown = false;
bool elliConnected = false;

enum class LedState : uint8_t { Off, P1Update, ElliFault };
LedState displayedLedState = LedState::Off;

bool validNetworkHost(const char *text) {
  const size_t length = strlen(text);
  if (length == 0 || length > 63) {
    return false;
  }

  IPAddress address;
  if (address.fromString(text)) {
    return address[0] != 0 && address[0] != 127 && address[0] < 224 &&
           !(address[0] == 255 && address[1] == 255 &&
             address[2] == 255 && address[3] == 255);
  }

  size_t labelLength = 0;
  bool previousWasHyphen = false;
  for (size_t index = 0; index < length; ++index) {
    const char character = text[index];
    if (character == '.') {
      if (labelLength == 0 || previousWasHyphen) {
        return false;
      }
      labelLength = 0;
      previousWasHyphen = false;
    } else if ((character >= 'a' && character <= 'z') ||
               (character >= 'A' && character <= 'Z') ||
               (character >= '0' && character <= '9') ||
               (character == '-' && labelLength > 0 && !previousWasHyphen)) {
      ++labelLength;
      previousWasHyphen = character == '-';
      if (labelLength > 63) {
        return false;
      }
    } else {
      return false;
    }
  }
  return labelLength > 0 && !previousWasHyphen;
}

bool validSettings(float fuseCurrentA, float maxChargePowerKw,
                   const char *homeWizardHost, const char *elliHost) {
  return isfinite(fuseCurrentA) && isfinite(maxChargePowerKw) &&
         fuseCurrentA >= Config::MIN_FUSE_CURRENT_A &&
         fuseCurrentA <= Config::MAX_FUSE_CURRENT_A &&
         maxChargePowerKw >= Config::MIN_MAX_CHARGE_POWER_KW &&
         maxChargePowerKw <= Config::MAX_MAX_CHARGE_POWER_KW &&
         validNetworkHost(homeWizardHost) && validNetworkHost(elliHost);
}

bool resolveNetworkHost(const char *host, IPAddress &address) {
  if (address.fromString(host)) {
    return true;
  }

  const size_t hostLength = strlen(host);
  const bool isLocalHostname = hostLength > 6 &&
      strcasecmp(host + hostLength - 6, ".local") == 0;
  if (isLocalHostname) {
    address = MDNS.queryHost(host, Config::HOST_RESOLVE_TIMEOUT_MS);
    return address[0] != 0;
  }

  return WiFi.hostByName(host, address) == 1;
}

bool parseSetting(const char *text, float &value) {
  char *end = nullptr;
  value = strtof(text, &end);
  return end != text && *end == '\0' && isfinite(value);
}

void loadUserSettings() {
  userSettings.fuseCurrentA = preferences.getFloat("fuse_a", Config::DEFAULT_FUSE_CURRENT_A);
  userSettings.maxChargePowerKw = preferences.getFloat("max_kw", Config::DEFAULT_MAX_CHARGE_POWER_KW);
  preferences.getString("p1_host", userSettings.homeWizardHost, sizeof(userSettings.homeWizardHost));
  preferences.getString("elli_host", userSettings.elliHost, sizeof(userSettings.elliHost));
  userSettings.configured = preferences.getBool("configured", false) &&
                            validSettings(userSettings.fuseCurrentA, userSettings.maxChargePowerKw,
                                          userSettings.homeWizardHost, userSettings.elliHost);
}

bool saveUserSettings(float fuseCurrentA, float maxChargePowerKw,
                      const char *homeWizardHost, const char *elliHost) {
  preferences.putBool("configured", false);
  const bool valuesSaved = preferences.putFloat("fuse_a", fuseCurrentA) == sizeof(float) &&
                           preferences.putFloat("max_kw", maxChargePowerKw) == sizeof(float) &&
                           preferences.putString("p1_host", homeWizardHost) > 0 &&
                           preferences.putString("elli_host", elliHost) > 0;
  const bool markerSaved = valuesSaved && preferences.putBool("configured", true) == sizeof(bool);
  return markerSaved;
}

void createSetupAccessPoint(char *ssid, size_t ssidSize, char *password, size_t passwordSize) {
  const uint64_t chipId = ESP.getEfuseMac();
  snprintf(ssid, ssidSize, "Elli-Setup-%06X", static_cast<unsigned int>(chipId & 0xFFFFFF));
  snprintf(password, passwordSize, "Elli-%08lX",
           static_cast<unsigned long>(chipId & 0xFFFFFFFF));
}

bool setupNetworkAndSettings(bool forcePortalOnEntry = false) {
  char setupSsid[24];
  char setupPassword[20];
  createSetupAccessPoint(setupSsid, sizeof(setupSsid), setupPassword, sizeof(setupPassword));

  bool forcePortal = forcePortalOnEntry || !userSettings.configured;
  while (true) {
    char fuseInput[12];
    char powerInput[12];
    snprintf(fuseInput, sizeof(fuseInput), "%.1f", userSettings.fuseCurrentA);
    snprintf(powerInput, sizeof(powerInput), "%.1f", userSettings.maxChargePowerKw);

    WiFiManagerParameter p1HostParameter(
      "p1_host", "HomeWizard P1 IP/hostname (esim. meter.local)", userSettings.homeWizardHost,
      sizeof(userSettings.homeWizardHost) - 1);
    WiFiManagerParameter elliHostParameter(
      "elli_host", "Elli IP/hostname (esim. wallbox-serial.local)", userSettings.elliHost,
      sizeof(userSettings.elliHost) - 1);
    WiFiManagerParameter networkInfo(
      "<p>Valitse sama kotiverkko kuin P1:ll&auml; ja Elli-laturilla. Sy&ouml;t&auml; IP tai hostname ilman http(s)://-osaa.</p>");
    WiFiManagerParameter fuseParameter(
      "fuse_a", "Sulakevirta vaihetta kohti (3x25 A: syota 25)", fuseInput,
        sizeof(fuseInput) - 1, " type='number' min='6' max='63' step='0.1'");
    WiFiManagerParameter powerParameter(
      "max_kw", "Latauksen enimmäisteho (kW)", powerInput,
      sizeof(powerInput) - 1, " type='number' min='0' max='11' step='0.1'");

    WiFiManager wifiManager;
    wifiManager.setConfigPortalTimeout(600);
    wifiManager.setConnectTimeout(20);
    wifiManager.setHostname("elli-load-manager");
    wifiManager.addParameter(&networkInfo);
    wifiManager.addParameter(&p1HostParameter);
    wifiManager.addParameter(&elliHostParameter);
    wifiManager.addParameter(&fuseParameter);
    wifiManager.addParameter(&powerParameter);

    Serial.printf("Setup AP: %s, password: %s\n", setupSsid, setupPassword);
    Serial.println("Open http://192.168.4.1 to configure Wi-Fi and load limits.");
    const bool connected = forcePortal
                               ? wifiManager.startConfigPortal(setupSsid, setupPassword)
                               : wifiManager.autoConnect(setupSsid, setupPassword);
    if (!connected) {
      Serial.println("Setup portal timed out or Wi-Fi connection failed; reopening setup.");
      forcePortal = true;
      continue;
    }

    float fuseCurrentA = 0.0F;
    float maxChargePowerKw = 0.0F;
    char homeWizardHost[sizeof(userSettings.homeWizardHost)] = {};
    char elliHost[sizeof(userSettings.elliHost)] = {};
    strlcpy(homeWizardHost, p1HostParameter.getValue(), sizeof(homeWizardHost));
    strlcpy(elliHost, elliHostParameter.getValue(), sizeof(elliHost));
    if (!parseSetting(fuseParameter.getValue(), fuseCurrentA) ||
        !parseSetting(powerParameter.getValue(), maxChargePowerKw) ||
        !validSettings(fuseCurrentA, maxChargePowerKw, homeWizardHost, elliHost)) {
      Serial.println("Invalid setup. Enter valid limits and both device IP/hostnames.");
      forcePortal = true;
      continue;
    }

    const bool changed = !userSettings.configured ||
                         fuseCurrentA != userSettings.fuseCurrentA ||
                         maxChargePowerKw != userSettings.maxChargePowerKw ||
                         strcmp(homeWizardHost, userSettings.homeWizardHost) != 0 ||
                         strcmp(elliHost, userSettings.elliHost) != 0;
    if (changed && !saveUserSettings(fuseCurrentA, maxChargePowerKw, homeWizardHost, elliHost)) {
      Serial.println("Could not save settings to flash; reopening setup.");
      forcePortal = true;
      continue;
    }

    userSettings.fuseCurrentA = fuseCurrentA;
    userSettings.maxChargePowerKw = maxChargePowerKw;
    strlcpy(userSettings.homeWizardHost, homeWizardHost, sizeof(userSettings.homeWizardHost));
    strlcpy(userSettings.elliHost, elliHost, sizeof(userSettings.elliHost));
    userSettings.configured = true;
    WiFi.setAutoReconnect(true);
    if (!MDNS.begin("elli-load-manager")) {
      Serial.println("mDNS responder could not start; .local lookups may fail.");
    }
    lastWifiAttemptMs = millis();
    Serial.printf("Connected WLAN: %s, IP: %s\n", WiFi.SSID().c_str(),
            WiFi.localIP().toString().c_str());
    Serial.printf("Settings saved: %.1f A/phase, maximum %.1f kW.\n",
                  userSettings.fuseCurrentA, userSettings.maxChargePowerKw);
    Serial.printf("P1 host: %s, Elli host: %s\n", userSettings.homeWizardHost, userSettings.elliHost);
    return true;
  }
}

bool readNumber(JsonVariantConst value, float &result) {
  if (value.is<float>() || value.is<int>() || value.is<unsigned int>()) {
    result = value.as<float>();
    return isfinite(result);
  }
  return false;
}

bool parseP1Payload(const String &payload, P1Reading &reading) {
  JsonDocument document;
  if (deserializeJson(document, payload)) {
    return false;
  }

  JsonVariantConst data = document.as<JsonVariantConst>();
  float phasePower[3] = {};
  if (!readNumber(data["active_power_l1_w"], phasePower[0]) ||
      !readNumber(data["active_power_l2_w"], phasePower[1]) ||
      !readNumber(data["active_power_l3_w"], phasePower[2])) {
    return false;
  }

  for (uint8_t phase = 0; phase < 3; ++phase) {
    reading.phasePowerW[phase] = phasePower[phase];
  }
  reading.receivedAtMs = millis();
  reading.valid = true;
  return true;
}

bool pollHomeWizard(P1Reading &reading) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  HTTPClient http;
  IPAddress p1Address;
  if (!resolveNetworkHost(userSettings.homeWizardHost, p1Address)) {
    Serial.printf("Could not resolve HomeWizard host: %s\n", userSettings.homeWizardHost);
    return false;
  }

  const String url = String("http://") + p1Address.toString() + ":" +
                     Config::HOMEWIZARD_PORT + "/api/v1/data";
  http.setTimeout(Config::HTTP_TIMEOUT_MS);
  if (!http.begin(url)) {
    return false;
  }

  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  const String payload = http.getString();
  http.end();
  return parseP1Payload(payload, reading);
}

float calculateCurrentLimitA(const P1Reading &reading) {
  const float powerLimitCurrentA = userSettings.maxChargePowerKw * 1000.0F /
                                   (230.0F * Config::CHARGER_PHASES);
  float availableCurrentA = min(Config::CHARGER_MAX_CURRENT_A, powerLimitCurrentA);
  for (uint8_t phase = 0; phase < Config::CHARGER_PHASES; ++phase) {
    const float importedCurrentA = max(0.0F, reading.phasePowerW[phase]) / 230.0F;
    const float phaseHeadroomA = userSettings.fuseCurrentA - Config::GRID_RESERVE_A - importedCurrentA;
    availableCurrentA = min(availableCurrentA, phaseHeadroomA);
  }

  if (availableCurrentA < Config::CHARGER_MIN_CURRENT_A) {
    return 0.0F;
  }
  return floorf(min(availableCurrentA, Config::CHARGER_MAX_CURRENT_A));
}

void serviceWifi() {
  if (WiFi.status() == WL_CONNECTED || millis() - lastWifiAttemptMs < Config::WIFI_RETRY_INTERVAL_MS) {
    return;
  }
  WiFi.reconnect();
  lastWifiAttemptMs = millis();
}

void serviceSerialSetupCommand() {
  bool openPortal = false;
  while (Serial.available()) {
    const char command = static_cast<char>(Serial.read());
    openPortal = openPortal || command == 'c' || command == 'C';
  }
  if (!openPortal) {
    return;
  }

  Serial.println("Opening setup portal. Connect to the printed setup AP.");
  setupNetworkAndSettings(true);
  latestReading.valid = false;
  elliConnectionKnown = false;
}

void serviceP1() {
  if (millis() - lastPollMs < Config::P1_POLL_INTERVAL_MS) {
    return;
  }
  lastPollMs = millis();

  P1Reading newReading;
  if (pollHomeWizard(newReading)) {
    latestReading = newReading;
    Serial.printf("P1 W: L1 %.0f, L2 %.0f, L3 %.0f\n",
                  latestReading.phasePowerW[0], latestReading.phasePowerW[1],
                  latestReading.phasePowerW[2]);
  }
}

void serviceElliConnection() {
  const uint32_t now = millis();
  if (elliConnectionKnown && now - lastElliCheckMs < Config::ELLI_CHECK_INTERVAL_MS) {
    return;
  }
  lastElliCheckMs = now;

  bool reachable = false;
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client;
    IPAddress elliAddress;
    if (resolveNetworkHost(userSettings.elliHost, elliAddress)) {
      reachable = client.connect(elliAddress, Config::ELLI_MODBUS_PORT,
                                 Config::ELLI_CONNECT_TIMEOUT_MS);
    } else {
      Serial.printf("Could not resolve Elli host: %s\n", userSettings.elliHost);
    }
    client.stop();
  }

  const bool wasKnown = elliConnectionKnown;
  const bool wasConnected = elliConnected;
  elliConnectionKnown = true;
  elliConnected = reachable;

  if (!reachable && (!wasKnown || wasConnected)) {
    elliLostSinceMs = now;
    Serial.println("FAULT: Elli Modbus TCP port is unreachable.");
  } else if (reachable && (!wasKnown || !wasConnected)) {
    Serial.println("Elli Modbus TCP port is reachable.");
  }
}

void displayLed(LedState state) {
  if (state == displayedLedState) {
    return;
  }

  const uint8_t level = state == LedState::Off
                            ? (Config::STATUS_LED_ACTIVE_LEVEL == HIGH ? LOW : HIGH)
                            : Config::STATUS_LED_ACTIVE_LEVEL;
  digitalWrite(Config::STATUS_LED_PIN, level);
  displayedLedState = state;
}

void serviceLed() {
  const uint32_t now = millis();
  if (elliConnectionKnown && !elliConnected) {
    const uint32_t faultElapsedMs = now - elliLostSinceMs;
    const bool blinkOn = faultElapsedMs % Config::ELLI_FAULT_BLINK_PERIOD_MS <
                         Config::ELLI_FAULT_BLINK_ON_MS;
    displayLed(blinkOn ? LedState::ElliFault : LedState::Off);
    return;
  }

  const bool p1PulseOn = latestReading.valid &&
                         now - latestReading.receivedAtMs < Config::P1_LED_PULSE_MS;
  displayLed(p1PulseOn ? LedState::P1Update : LedState::Off);
}

void reportControlState() {
  if (millis() - lastStatusReportMs < Config::STATUS_REPORT_INTERVAL_MS) {
    return;
  }
  lastStatusReportMs = millis();

  const bool p1Fresh = latestReading.valid &&
                       millis() - latestReading.receivedAtMs <= Config::P1_STALE_AFTER_MS;
  if (!p1Fresh) {
    Serial.println("FAULT: P1 data unavailable or stale; no current increase permitted.");
    return;
  }

  const float targetA = calculateCurrentLimitA(latestReading);
  Serial.printf("P1 fresh; calculated charger limit %.0f A. Modbus output %s.\n",
                targetA, Config::ELLI_MODBUS_OUTPUT_ENABLED ? "ENABLED" : "DISABLED");
}
}  // namespace

void setup() {
  Serial.begin(115200);
  pinMode(Config::STATUS_LED_PIN, OUTPUT);
  displayLed(LedState::Off);
  delay(250);
  Serial.println("Elli Charger 2 dynamic load-management prototype");
  Serial.printf("Modbus register candidates: 261=%u, 262=%u, 257=%u\n",
                Config::ELLI_MAX_CURRENT_COMMAND_REGISTER,
                Config::ELLI_FAILSAFE_CURRENT_REGISTER,
                Config::ELLI_WATCHDOG_TIMEOUT_REGISTER);
  Serial.println("Modbus writes disabled: confirm Elli Unit ID, address offset, and watchdog reset behavior.");
  if (!preferences.begin("elli-load", false)) {
    Serial.println("Could not open settings storage in flash; restarting.");
    delay(1000);
    ESP.restart();
  }
  loadUserSettings();
  setupNetworkAndSettings();
}

void loop() {
  serviceSerialSetupCommand();
  serviceWifi();
  serviceP1();
  serviceElliConnection();
  reportControlState();
  serviceLed();
  delay(20);
}