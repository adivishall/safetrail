// safetrail_server -- the SafeTrail operations platform over HTTP.
//
//   make app                      build and start on http://127.0.0.1:8080
//   ./build/safetrail_server --help
//
// Demo mode (default) loads the Shillong OpenStreetMap zones and road network,
// registers labelled demonstration responders, and preloads the walkthrough
// scenario paused at time zero. Production mode loads the same geography but
// no demonstration records, refuses scenarios, and runs the platform clock in
// real time.
//
// Credentials never live in the repository. Each account's password comes from
// its environment variable (SAFETRAIL_ADMIN_PASSWORD, ...); failing that, from
// var/demo-credentials.txt (git-ignored, mode 0600) if an earlier run created
// it; failing that, it is generated at random and written there.
#include <sys/stat.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>

#include "safetrail/app/api.hpp"
#include "safetrail/app/scenario.hpp"
#include "safetrail/app/world.hpp"
#include "safetrail/net/http.hpp"

using namespace safetrail;

namespace {

volatile int g_stop = 0;
void on_signal(int) { g_stop = 1; }

struct Options {
  std::string host = "127.0.0.1";
  int port = 8080;
  bool demo = true;
  std::string web = "web/app";
  std::string zones = "data/zones/shillong_osm.geojson";
  std::string roads = "data/osm/roads.txt";
  std::string var = "var";
  std::string scenario = "walkthrough";
};

void usage() {
  std::printf(
      "safetrail_server [--host 127.0.0.1] [--port 8080] [--mode demo|production]\n"
      "                 [--web web/app] [--zones FILE] [--roads FILE] [--var DIR]\n"
      "                 [--scenario walkthrough|normal|entry|separation|offline|none]\n");
}

// "username password" lines; '#' comments.
void load_credentials(const std::string& path, std::vector<app::StaffAccount>& staff,
                      const std::vector<std::string>& generated, std::vector<std::string>* still_generated) {
  std::ifstream f(path);
  std::string user, pass;
  std::vector<std::pair<std::string, std::string>> saved;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    const size_t sp = line.find(' ');
    if (sp == std::string::npos) continue;
    saved.emplace_back(line.substr(0, sp), line.substr(sp + 1));
  }
  for (const auto& g : generated) {
    bool found = false;
    for (auto& s : staff)
      if (s.username == g)
        for (const auto& [u, p] : saved)
          if (u == g && p.size() >= 10) { s.password = p; found = true; }
    if (!found) still_generated->push_back(g);
  }
}

}  // namespace

int main(int argc, char** argv) {
  Options o;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&](const char* what) -> std::string {
      if (i + 1 >= argc) { std::fprintf(stderr, "%s needs a value\n", what); std::exit(2); }
      return argv[++i];
    };
    if (a == "--host") o.host = next("--host");
    else if (a == "--port") o.port = std::atoi(next("--port").c_str());
    else if (a == "--mode") {
      const std::string m = next("--mode");
      if (m != "demo" && m != "production") { usage(); return 2; }
      o.demo = m == "demo";
    } else if (a == "--web") o.web = next("--web");
    else if (a == "--zones") o.zones = next("--zones");
    else if (a == "--roads") o.roads = next("--roads");
    else if (a == "--var") o.var = next("--var");
    else if (a == "--scenario") o.scenario = next("--scenario");
    else if (a == "--help" || a == "-h") { usage(); return 0; }
    else { usage(); return 2; }
  }
  if (o.host != "127.0.0.1")
    std::printf("WARNING: listening on %s, not loopback. There is no TLS: put a TLS-terminating proxy in front\n"
                "         before exposing this to any network (docs/final/SECURITY_AND_PRIVACY.md).\n",
                o.host.c_str());

  // ── Credentials ──────────────────────────────────────────────────────────
  std::vector<std::string> generated, still;
  std::vector<app::StaffAccount> staff = app::staff_from_environment(o.demo, &generated);
  ::mkdir(o.var.c_str(), 0700);
  const std::string cred_path = o.var + "/demo-credentials.txt";
  load_credentials(cred_path, staff, generated, &still);
  if (!still.empty() || !generated.empty()) {
    std::ofstream f(cred_path, std::ios::trunc);
    f << "# SafeTrail local credentials -- generated, git-ignored, never commit.\n"
      << "# Set SAFETRAIL_<ROLE>_PASSWORD to override. Delete this file to regenerate.\n";
    for (const auto& s : staff)
      for (const auto& g : generated)
        if (s.username == g) f << s.username << " " << s.password << "\n";
    f.close();
    ::chmod(cred_path.c_str(), 0600);
  }

  // ── World ────────────────────────────────────────────────────────────────
  app::WorldOptions wo;
  wo.zones_path = o.zones;
  wo.roads_path = o.roads;
  wo.demo = o.demo;
  wo.staff = staff;
  std::unique_ptr<app::Platform> world;
  std::string err;
  const auto t0 = std::chrono::steady_clock::now();
  world = app::build_world(wo, &err);
  if (!world) { std::fprintf(stderr, "cannot build the platform: %s\n", err.c_str()); return 1; }
  const double load_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

  std::unique_ptr<app::ScenarioRunner> runner;
  if (o.demo) {
    runner = std::make_unique<app::ScenarioRunner>(&world, [&] {
      std::string e;
      auto w = app::build_world(wo, &e);
      if (w) world = std::move(w);
    });
    if (o.scenario != "none" && !runner->load(o.scenario, &err))
      std::fprintf(stderr, "scenario %s not loaded: %s\n", o.scenario.c_str(), err.c_str());
  }

  app::ApiOptions ao;
  ao.web_root = o.web;
  ao.demo = o.demo;
  app::Api api(ao, &world, runner.get());

  net::Server server;
  if (!server.listen(o.host, o.port, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);

  std::printf("SafeTrail %s mode — http://%s:%d  (world built in %.1f s: %zu zones, %zu road junctions)\n",
              o.demo ? "demo" : "production", o.host.c_str(), server.port(), load_s,
              world->zones().all_ids().size(), world->roads().node_count());
  std::printf("Accounts (passwords from the environment or %s):\n", cred_path.c_str());
  for (const auto& s : staff) {
    bool gen = false;
    for (const auto& g : generated) if (g == s.username) gen = true;
    std::printf("  %-10s %-9s %s\n", s.username.c_str(), app::to_string(s.role),
                gen ? s.password.c_str() : "(from environment)");
  }
  if (runner && !runner->scenario_id().empty())
    std::printf("Scenario \"%s\" is loaded and paused at 00:00 — press Start in Live Monitoring.\n",
                runner->scenario_id().c_str());
  std::printf("Ctrl-C to stop.\n");
  std::fflush(stdout);

  auto last = std::chrono::steady_clock::now();
  const auto started = last;
  server.run([&](const net::Request& r) { return api.handle(r); },
             [&]() -> int {
               const auto now = std::chrono::steady_clock::now();
               const double dt = std::chrono::duration<double>(now - last).count();
               last = now;
               if (runner) {
                 runner->advance(dt);
               } else {
                 // Production: the platform clock is real time since start.
                 world->tick(std::chrono::duration_cast<std::chrono::milliseconds>(now - started).count());
               }
               return 50;
             },
             &g_stop);
  std::printf("\nstopped.\n");
  return 0;
}
