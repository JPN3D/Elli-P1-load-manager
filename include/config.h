#pragma once

#include <Arduino.h>

namespace Config {
constexpr uint16_t HOMEWIZARD_PORT = 80;
constexpr uint16_t ELLI_MODBUS_PORT = 502;
constexpr uint32_t HOST_RESOLVE_TIMEOUT_MS = 1500;
constexpr uint8_t STATUS_LED_PIN = 8;
constexpr uint8_t STATUS_LED_ACTIVE_LEVEL = LOW;

constexpr float DEFAULT_FUSE_CURRENT_A = 25.0F;
constexpr float MIN_FUSE_CURRENT_A = 6.0F;
constexpr float MAX_FUSE_CURRENT_A = 63.0F;
constexpr float GRID_RESERVE_A = 2.0F;
constexpr uint8_t CHARGER_PHASES = 3;
constexpr float CHARGER_MAX_CURRENT_A = 16.0F;
constexpr float CHARGER_MIN_CURRENT_A = 6.0F;
constexpr float DEFAULT_MAX_CHARGE_POWER_KW = 11.0F;
constexpr float MIN_MAX_CHARGE_POWER_KW = 0.0F;
constexpr float MAX_MAX_CHARGE_POWER_KW = 11.0F;
constexpr uint32_t P1_POLL_INTERVAL_MS = 2000;
constexpr uint32_t P1_STALE_AFTER_MS = 10000;
constexpr uint32_t P1_LED_PULSE_MS = 500;
constexpr uint32_t ELLI_CHECK_INTERVAL_MS = 5000;
constexpr uint32_t ELLI_CONNECT_TIMEOUT_MS = 1000;
constexpr uint32_t ELLI_FAULT_BLINK_PERIOD_MS = 5000;
constexpr uint32_t ELLI_FAULT_BLINK_ON_MS = 500;
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 10000;
constexpr uint32_t STATUS_REPORT_INTERVAL_MS = 1000;
constexpr uint16_t HTTP_TIMEOUT_MS = 1500;

static_assert(CHARGER_PHASES == 1 || CHARGER_PHASES == 3, "CHARGER_PHASES must be 1 or 3");

constexpr uint16_t ELLI_MAX_CURRENT_COMMAND_REGISTER = 261;
constexpr uint16_t ELLI_FAILSAFE_CURRENT_REGISTER = 262;
constexpr uint16_t ELLI_WATCHDOG_TIMEOUT_REGISTER = 257;
constexpr uint16_t ELLI_CURRENT_REGISTER_MIN = 60;
constexpr uint16_t ELLI_CURRENT_REGISTER_MAX = 160;
constexpr uint16_t ELLI_CURRENT_REGISTER_PER_AMP = 10;
constexpr uint16_t ELLI_FAILSAFE_CURRENT_VALUE = 60;
constexpr uint16_t ELLI_WATCHDOG_TIMEOUT_MS = 15000;
constexpr uint16_t ELLI_WATCHDOG_TIMEOUT_MAX_MS = 63335;
constexpr bool ELLI_MODBUS_OUTPUT_ENABLED = false;
}
