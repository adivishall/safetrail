#include "safetrail/app/world.hpp"

#include <cstdlib>

#include "safetrail/app/crypto.hpp"

namespace safetrail::app {

namespace {

std::string random_password() {
  // 15 bytes -> 20 characters of an unambiguous alphabet (no 0/O, 1/l/I).
  static const char* alphabet = "abcdefghjkmnpqrstuvwxyzABCDEFGHJKMNPQRSTUVWXYZ23456789";
  uint8_t raw[20];
  if (!random_bytes(raw, sizeof raw)) std::abort();   // fail closed
  std::string s;
  for (uint8_t b : raw) s.push_back(alphabet[b % 54]);
  return s;
}

}  // namespace

std::vector<StaffAccount> staff_from_environment(bool demo, std::vector<std::string>* generated) {
  struct Spec { const char* user; const char* name; Role role; const char* env; bool demo_only; };
  const Spec specs[] = {
      {"admin", "Administrator", Role::Admin, "SAFETRAIL_ADMIN_PASSWORD", false},
      {"operator", "Duty Operator", Role::Operator, "SAFETRAIL_OPERATOR_PASSWORD", true},
      {"responder", "Demo Medic (Unit A)", Role::Responder, "SAFETRAIL_RESPONDER_PASSWORD", true},
      {"tourist", "Demo Tourist", Role::Tourist, "SAFETRAIL_TOURIST_PASSWORD", true},
  };
  std::vector<StaffAccount> out;
  for (const auto& s : specs) {
    if (s.demo_only && !demo) continue;
    StaffAccount a;
    a.username = s.user;
    a.display_name = s.name;
    a.role = s.role;
    const char* v = std::getenv(s.env);
    if (v && std::string(v).size() >= 10) {
      a.password = v;
    } else {
      a.password = random_password();
      if (generated) generated->push_back(a.username);
    }
    out.push_back(std::move(a));
  }
  return out;
}

std::unique_ptr<Platform> build_world(const WorldOptions& opt, std::string* err) {
  auto pf = std::make_unique<Platform>(opt.config);
  if (!opt.zones_path.empty() && !pf->load_zones_file(opt.zones_path, err)) return nullptr;
  std::string rerr;
  if (opt.roads_path.empty() || !pf->load_roads_file(opt.roads_path, &rerr)) pf->use_grid_roads(16, 16);

  const Principal sys = Principal::sys();
  for (const auto& s : opt.staff) {
    Outcome o = pf->create_user(sys, s.username, s.display_name, s.role, s.password,
                                s.role == Role::Admin ? Source::Api : Source::Fixture);
    if (!o.ok()) { if (err) *err = "staff account " + s.username + ": " + o.error; return nullptr; }
  }

  if (opt.demo) {
    // Demonstration responders. Their names say "Demo", their source is
    // "fixture", and their positions are fixed points chosen to sit on the
    // road network around the demo area -- not anyone's real location or
    // availability. Production mode registers none.
    struct R { const char* name; const char* type; double lat, lon; };
    const R rs[] = {
        {"Demo Medical Unit A", "medical", 25.5785, 91.8790},
        {"Demo Rescue Team B", "rescue", 25.5945, 91.8605},
        {"Demo Police Patrol C", "police", 25.5700, 91.8840},
        {"Demo Medical Unit D", "medical", 25.5660, 91.8990},
        {"Demo Rescue Team E", "rescue", 25.6020, 91.8800},
        {"Demo Police Patrol F", "police", 25.5830, 91.8470},
    };
    const User* acct = pf->user_by_name("responder");
    for (size_t i = 0; i < sizeof rs / sizeof rs[0]; ++i)
      pf->create_responder(sys, rs[i].name, rs[i].type, {rs[i].lat, rs[i].lon}, Source::Fixture,
                           i == 0 && acct ? acct->id : kNoId);
  }
  return pf;
}

}  // namespace safetrail::app
