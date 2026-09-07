// ============================================================================
// tax_assistant_server — application entrypoint (Phase 1 scaffold)
//
// Step A goal: a server binary that boots, serves the frontend, and accepts
// WebSocket connections on /ws. No domain logic lives here yet — the extractor,
// state matrix, rule engine, and GSTR generator are wired in later steps (C..I).
// ============================================================================

#include <chrono>
#include <cstdlib>
#include <string>
#include <unordered_map>

#include "crow/crow.h"
#include "crow/json.h"
#include "crow/middlewares/cors.h"

namespace
{
    // ------------------------------------------------------------------
    // Environment helpers
    // ------------------------------------------------------------------
    const char* getEnv(const char* key, const char* fallback)
    {
        if (const char* v = std::getenv(key); v != nullptr && v[0] != '\0')
        {
            return v;
        }
        return fallback;
    }

    int getEnvInt(const char* key, int fallback)
    {
        if (const char* v = std::getenv(key); v != nullptr && v[0] != '\0')
        {
            return std::atoi(v);
        }
        return fallback;
    }

    // Session handshake payload per architecture.md §10:
    //   { "type": "SESSION_INIT", "session_id": "<uuid>" }
    crow::json::wvalue sessionInitFrame(const std::string& sessionId)
    {
        crow::json::wvalue frame;
        frame["type"]       = "SESSION_INIT";
        frame["session_id"] = sessionId;
        return frame;
    }
} // namespace

int main()
{
    // ------------------------------------------------------------------
    // Runtime configuration (from environment / .env)
    // ------------------------------------------------------------------
    const std::string host = getEnv("HOST", "0.0.0.0");
    const int port         = getEnvInt("PORT", 8080);
    const std::string staticDir = getEnv("FRONTEND_DIR", "frontend");

    crow::App<crow::CORSHandler> app;

    // NOTE on Crow v1.2.0 route gotchas (both verified empirically):
    //  1. A bare "/<path>" catch-all shadows the WebSocket upgrade request
    //     and /ws never negotiates a session.
    //  2. "/static/<path>" is a RESERVED namespace (Crow's built-in static
    //     endpoint, CROW_STATIC_ENDPOINT). Registering it crashes at startup
    //     with an abort. Assets are therefore served under /assets/<path>.
    CROW_ROUTE(app, "/")([staticDir](const crow::request&) {
        crow::response res;
        res.set_static_file_info(staticDir + "/index.html");
        return res;
    });

    CROW_ROUTE(app, "/assets/<path>")([staticDir](const std::string& path) {
        crow::response res;
        res.set_static_file_info(staticDir + "/" + path);
        return res;
    });

    CROW_WEBSOCKET_ROUTE(app, "/ws")
        .onopen([&](crow::websocket::connection& conn) {
            // Issue a fresh session id per connection. In later phases the
            // session lifecycle is owned by the persistence layer (Step G).
            static std::uint64_t counter = 0;
            const std::string sessionId  = "sess-" + std::to_string(++counter) + "-" +
                                           std::to_string(
                                               std::chrono::high_resolution_clock::now()
                                                   .time_since_epoch()
                                                   .count());
            conn.send_text(sessionInitFrame(sessionId).dump());
        })
        .onmessage([](crow::websocket::connection& /*conn*/, const std::string& /*data*/, bool /*is_binary*/) {
            // Payload dispatch arrives in Step I (orchestration). For the scalar
            // scaffold we acknowledge receipt so the client sees a live round-trip.
        });

    // CORS applies a permissive policy by default (origin/methods/headers = "*").
    app.loglevel(crow::LogLevel::Info).port(port).bindaddr(host).run();

    return 0;
}