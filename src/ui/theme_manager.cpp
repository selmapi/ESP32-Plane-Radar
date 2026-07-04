#include "ui/theme_manager.h"

#include <Preferences.h>

namespace ui::radar {

namespace {

constexpr char kPrefsNamespace[] = "planeradar";
constexpr char kPrefsThemeKey[] = "themeIdx";
constexpr uint8_t kDefaultThemeIndex = 0;  // Midnight

Preferences s_prefs;
uint8_t s_theme_index = kDefaultThemeIndex;

void saveThemeIndex() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putUChar(kPrefsThemeKey, s_theme_index);
  s_prefs.end();
}

}  // namespace

void themeInit() {
  if (!s_prefs.begin(kPrefsNamespace, true)) {
    return;
  }
  const uint8_t saved = s_prefs.getUChar(kPrefsThemeKey, kDefaultThemeIndex);
  s_theme_index = (saved < kThemeCount) ? saved : kDefaultThemeIndex;
  s_prefs.end();
}

void themeNext() {
  s_theme_index = static_cast<uint8_t>((s_theme_index + 1) % kThemeCount);
  saveThemeIndex();
}

void themeSet(uint8_t index) {
  s_theme_index = (index < kThemeCount) ? index : kDefaultThemeIndex;
  saveThemeIndex();
}

uint8_t themeIndex() { return s_theme_index; }

const Theme& themeCurrent() { return kThemes[s_theme_index]; }

}  // namespace ui::radar
