#include <unity.h>

#include <cstdint>
#include <cstring>

#include "ui/region_map_blob.h"

using ui::radar::decodeMapBlobHeader;
using ui::radar::decodeMapBlobSpan;
using ui::radar::decodeMapBlobTown;
using ui::radar::decodeMapBlobVert;
using ui::radar::kMapBlobHeaderBytes;
using ui::radar::kMapBlobSpanBytes;
using ui::radar::kMapBlobTownBytes;
using ui::radar::kMapBlobVertBytes;
using ui::radar::mapBlobExpectedTotalBytes;
using ui::radar::MapBlobHeader;
using ui::radar::MapSpan;
using ui::radar::MapTown;
using ui::radar::MapVert;

namespace {

// Public Denver demo center, same as the baked map / other native tests
// (never a real-world personal coordinate -- see CLAUDE.md landmine #8).
constexpr float kCenterLat = 39.7392f;
constexpr float kCenterLon = -104.9903f;

void writeU16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

void writeI16(uint8_t* p, int16_t v) { writeU16(p, static_cast<uint16_t>(v)); }

void writeF32(uint8_t* p, float v) {
  uint32_t bits = 0;
  std::memcpy(&bits, &v, sizeof(bits));
  p[0] = static_cast<uint8_t>(bits & 0xFF);
  p[1] = static_cast<uint8_t>((bits >> 8) & 0xFF);
  p[2] = static_cast<uint8_t>((bits >> 16) & 0xFF);
  p[3] = static_cast<uint8_t>((bits >> 24) & 0xFF);
}

/** Builds a valid 24-byte header fixture with the given counts. */
void buildHeader(uint8_t* out, uint16_t vertCount, uint16_t spanCount,
                  uint16_t townCount) {
  out[0] = 'P';
  out[1] = 'R';
  out[2] = 'M';
  out[3] = 'B';
  out[4] = 1;    // version
  out[5] = 0;    // reserved
  out[6] = 0;
  out[7] = 0;
  writeF32(out + 8, kCenterLat);
  writeF32(out + 12, kCenterLon);
  writeU16(out + 16, vertCount);
  writeU16(out + 18, spanCount);
  writeU16(out + 20, townCount);
  writeU16(out + 22, 0);  // reserved
}

}  // namespace

void test_valid_header_decodes() {
  uint8_t buf[kMapBlobHeaderBytes];
  buildHeader(buf, 2, 1, 1);

  MapBlobHeader header{};
  TEST_ASSERT_TRUE(decodeMapBlobHeader(buf, header));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, kCenterLat, header.centerLat);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, kCenterLon, header.centerLon);
  TEST_ASSERT_EQUAL_UINT16(2, header.vertCount);
  TEST_ASSERT_EQUAL_UINT16(1, header.spanCount);
  TEST_ASSERT_EQUAL_UINT16(1, header.townCount);
}

void test_wrong_magic_rejected() {
  uint8_t buf[kMapBlobHeaderBytes];
  buildHeader(buf, 0, 0, 0);
  buf[0] = 'X';  // corrupt magic

  MapBlobHeader header{};
  TEST_ASSERT_FALSE(decodeMapBlobHeader(buf, header));
}

void test_wrong_version_rejected() {
  uint8_t buf[kMapBlobHeaderBytes];
  buildHeader(buf, 0, 0, 0);
  buf[4] = 2;  // unsupported version

  MapBlobHeader header{};
  TEST_ASSERT_FALSE(decodeMapBlobHeader(buf, header));
}

void test_expected_total_bytes_matches_fixture() {
  // Hand-built full blob: 2 verts, 1 span, 1 town.
  constexpr uint16_t kVertCount = 2;
  constexpr uint16_t kSpanCount = 1;
  constexpr uint16_t kTownCount = 1;
  constexpr size_t kTotal = kMapBlobHeaderBytes + kVertCount * kMapBlobVertBytes +
                            kSpanCount * kMapBlobSpanBytes +
                            kTownCount * kMapBlobTownBytes;
  uint8_t buf[kTotal];
  buildHeader(buf, kVertCount, kSpanCount, kTownCount);

  MapBlobHeader header{};
  TEST_ASSERT_TRUE(decodeMapBlobHeader(buf, header));
  TEST_ASSERT_EQUAL_UINT32(kTotal, mapBlobExpectedTotalBytes(header));

  // A header claiming more records than actually present must NOT match the
  // fixture's real byte length -- this is the truncation-detection case.
  MapBlobHeader inflated = header;
  inflated.vertCount = kVertCount + 10;
  TEST_ASSERT_NOT_EQUAL(kTotal, mapBlobExpectedTotalBytes(inflated));
}

void test_decode_vert_byte_exact() {
  uint8_t buf[kMapBlobVertBytes];
  writeI16(buf, 100);
  writeI16(buf + 2, -200);

  const MapVert v = decodeMapBlobVert(buf);
  TEST_ASSERT_EQUAL_INT16(100, v.dlat);
  TEST_ASSERT_EQUAL_INT16(-200, v.dlon);
}

void test_decode_span_byte_exact() {
  uint8_t buf[kMapBlobSpanBytes];
  writeU16(buf, 10);
  writeU16(buf + 2, 20);
  buf[4] = 2;  // MapLayer::kBoundary

  const MapSpan s = decodeMapBlobSpan(buf);
  TEST_ASSERT_EQUAL_UINT16(10, s.start);
  TEST_ASSERT_EQUAL_UINT16(20, s.len);
  TEST_ASSERT_EQUAL_UINT8(2, s.layer);
}

void test_decode_town_byte_exact() {
  uint8_t buf[kMapBlobTownBytes];
  writeI16(buf, 5);
  writeI16(buf + 2, -5);
  // 5-byte label field: "AB" + zero padding, matching the Worker's
  // zero-padded fixed-width label encoding.
  buf[4] = 'A';
  buf[5] = 'B';
  buf[6] = '\0';
  buf[7] = '\0';
  buf[8] = '\0';

  const MapTown t = decodeMapBlobTown(buf);
  TEST_ASSERT_EQUAL_INT16(5, t.dlat);
  TEST_ASSERT_EQUAL_INT16(-5, t.dlon);
  TEST_ASSERT_EQUAL_STRING("AB", t.label);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_valid_header_decodes);
  RUN_TEST(test_wrong_magic_rejected);
  RUN_TEST(test_wrong_version_rejected);
  RUN_TEST(test_expected_total_bytes_matches_fixture);
  RUN_TEST(test_decode_vert_byte_exact);
  RUN_TEST(test_decode_span_byte_exact);
  RUN_TEST(test_decode_town_byte_exact);
  return UNITY_END();
}
