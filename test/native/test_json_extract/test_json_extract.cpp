#include <ArduinoJson.h>
#include <unity.h>

#include <cstdint>
#include <cstring>

#include "services/aircraft_fields.h"

using services::adsb::Aircraft;
using services::adsb::extractExtendedFields;

static Aircraft parseOne(const char* json) {
  JsonDocument doc;
  deserializeJson(doc, json);
  JsonObject plane = doc.as<JsonObject>();
  Aircraft ac{};
  extractExtendedFields(&ac, plane);
  return ac;
}

void test_hex_strips_tilde_and_lowercases() {
  Aircraft ac = parseOne(R"({"hex":"~A1B2C3"})");
  TEST_ASSERT_EQUAL_STRING("a1b2c3", ac.hex);
}

void test_alt_ft_numeric() {
  Aircraft ac = parseOne(R"({"alt_baro":12300})");
  TEST_ASSERT_EQUAL_INT32(12300, ac.alt_ft);
}

void test_alt_ft_ground_is_sentinel() {
  Aircraft ac = parseOne(R"({"alt_baro":"ground"})");
  TEST_ASSERT_EQUAL_INT32(INT32_MIN, ac.alt_ft);
}

void test_vs_prefers_baro_rate() {
  Aircraft ac = parseOne(R"({"baro_rate":-1200,"geom_rate":900})");
  TEST_ASSERT_EQUAL_INT16(-1200, ac.vs_fpm);
}

void test_vs_falls_back_to_geom_rate() {
  Aircraft ac = parseOne(R"({"geom_rate":900})");
  TEST_ASSERT_EQUAL_INT16(900, ac.vs_fpm);
}

void test_squawk_and_emergency_7700() {
  Aircraft ac = parseOne(R"({"squawk":"7700"})");
  TEST_ASSERT_EQUAL_STRING("7700", ac.squawk);
  TEST_ASSERT_TRUE(ac.emergency);
}

void test_squawk_normal_not_emergency() {
  Aircraft ac = parseOne(R"({"squawk":"1200"})");
  TEST_ASSERT_EQUAL_STRING("1200", ac.squawk);
  TEST_ASSERT_FALSE(ac.emergency);
}

void test_emergency_field_true() {
  Aircraft ac = parseOne(R"({"emergency":"general"})");
  TEST_ASSERT_TRUE(ac.emergency);
}

void test_emergency_field_none_is_false() {
  Aircraft ac = parseOne(R"({"emergency":"none"})");
  TEST_ASSERT_FALSE(ac.emergency);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_hex_strips_tilde_and_lowercases);
  RUN_TEST(test_alt_ft_numeric);
  RUN_TEST(test_alt_ft_ground_is_sentinel);
  RUN_TEST(test_vs_prefers_baro_rate);
  RUN_TEST(test_vs_falls_back_to_geom_rate);
  RUN_TEST(test_squawk_and_emergency_7700);
  RUN_TEST(test_squawk_normal_not_emergency);
  RUN_TEST(test_emergency_field_true);
  RUN_TEST(test_emergency_field_none_is_false);
  return UNITY_END();
}
