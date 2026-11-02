#pragma once
// Building a platform ready to serve: zones, roads, staff accounts, and -- in
// demo mode only -- clearly labelled demonstration responders.
//
// Passwords never live in the repository. The server reads them from the
// environment (SAFETRAIL_ADMIN_PASSWORD and friends); any that are unset are
// generated at random on first start and written to var/demo-credentials.txt
// with mode 0600, which .gitignore excludes. See docs/final/SECURITY_AND_PRIVACY.md.
#include <memory>
#include <string>
#include <vector>

#include "safetrail/app/platform.hpp"

namespace safetrail::app {

struct StaffAccount {
  std::string username, display_name, password;
  Role role = Role::Operator;
};

struct WorldOptions {
  std::string zones_path = "data/zones/shillong_osm.geojson";
  std::string roads_path = "data/osm/roads.txt";
  bool demo = true;                    // false = production: no fixtures, no scenarios
  std::vector<StaffAccount> staff;
  PlatformConfig config{};
};

// nullptr on failure, with *err set.
std::unique_ptr<Platform> build_world(const WorldOptions& opt, std::string* err);

// The accounts the server needs, passwords from the environment or generated.
// `generated` lists the ones that were generated, so the caller can record them.
std::vector<StaffAccount> staff_from_environment(bool demo, std::vector<std::string>* generated);

}  // namespace safetrail::app
