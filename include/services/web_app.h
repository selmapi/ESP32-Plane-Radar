#pragma once

#include <WebServer.h>

namespace services::web_app {

/** Register the companion-app + API routes on WiFiManager's WebServer. */
void registerRoutes(WebServer& server);

}  // namespace services::web_app
