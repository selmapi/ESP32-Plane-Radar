#include "ui/trails.h"

#include <cstring>

namespace ui::radar {

namespace {

struct TrailSlot {
  char hex[7];
  TrailPoint points[kTrailPoints];
  uint8_t count;   // valid points, 0..kTrailPoints
  uint8_t head;    // index of next write (ring)
  bool seen_now;   // set during the current update pass
  uint32_t age;    // updates since last seen (for recycling)
};

TrailSlot s_slots[kTrailSlots];

TrailSlot* findSlot(const char* hex) {
  for (size_t i = 0; i < kTrailSlots; ++i) {
    if (s_slots[i].count > 0 && strncmp(s_slots[i].hex, hex, 7) == 0) {
      return &s_slots[i];
    }
  }
  return nullptr;
}

TrailSlot* claimSlot(const char* hex) {
  // Prefer a truly empty slot; else recycle the oldest.
  TrailSlot* oldest = &s_slots[0];
  for (size_t i = 0; i < kTrailSlots; ++i) {
    if (s_slots[i].count == 0) {
      TrailSlot* s = &s_slots[i];
      strncpy(s->hex, hex, sizeof(s->hex) - 1);
      s->hex[sizeof(s->hex) - 1] = '\0';
      s->count = 0;
      s->head = 0;
      s->age = 0;
      return s;
    }
    if (s_slots[i].age > oldest->age) {
      oldest = &s_slots[i];
    }
  }
  strncpy(oldest->hex, hex, sizeof(oldest->hex) - 1);
  oldest->hex[sizeof(oldest->hex) - 1] = '\0';
  oldest->count = 0;
  oldest->head = 0;
  oldest->age = 0;
  return oldest;
}

void appendPoint(TrailSlot* s, float lat, float lon) {
  s->points[s->head].lat = lat;
  s->points[s->head].lon = lon;
  s->head = static_cast<uint8_t>((s->head + 1) % kTrailPoints);
  if (s->count < kTrailPoints) {
    ++s->count;
  }
}

}  // namespace

void trailsUpdate(const services::adsb::Aircraft* list, size_t count) {
  for (size_t i = 0; i < kTrailSlots; ++i) {
    s_slots[i].seen_now = false;
  }
  for (size_t i = 0; i < count; ++i) {
    if (list[i].hex[0] == '\0') {
      continue;
    }
    TrailSlot* s = findSlot(list[i].hex);
    if (s == nullptr) {
      s = claimSlot(list[i].hex);
    }
    appendPoint(s, list[i].lat, list[i].lon);
    s->seen_now = true;
    s->age = 0;
  }
  for (size_t i = 0; i < kTrailSlots; ++i) {
    if (!s_slots[i].seen_now && s_slots[i].count > 0) {
      ++s_slots[i].age;
      if (s_slots[i].age > kTrailPoints) {
        s_slots[i].count = 0;  // fully aged out; free the slot
      }
    }
  }
}

size_t trailPointCount(const char* hex) {
  TrailSlot* s = findSlot(hex);
  return s ? s->count : 0;
}

bool trailPointAt(const char* hex, size_t i, TrailPoint* out) {
  TrailSlot* s = findSlot(hex);
  if (s == nullptr || i >= s->count) {
    return false;
  }
  // Oldest-first: head points at next write; oldest is head - count.
  const size_t start =
      (s->head + kTrailPoints - s->count) % kTrailPoints;
  const size_t idx = (start + i) % kTrailPoints;
  *out = s->points[idx];
  return true;
}

}  // namespace ui::radar
