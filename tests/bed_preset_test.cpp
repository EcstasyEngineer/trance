#include <common/session.h>
#include <common/trance.pb.h>
#include <trance/net/command_protocol.h>

#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& message)
{
  if (!ok) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

// Independent fixture protects the authored values and order, not just a
// successful round trip through the same table used to apply the presets.
struct Expected {
  const char* name;
  int count;
  float values[4][4]; // carrier, pulse, relative level, binaural
};
const Expected expected[] = {
    {"bimbo-drone", 2, {{312, 5, 0, 3}, {60, 3.25f, -6, 3}}},
    {"reactor", 4, {{300, 7, 0, 4}, {225, 4.6f, -4, 3.5f}, {165, 3.3f, -8, 3}, {90, 2.55f, -14, 2.5f}}},
    {"descent", 3, {{200, 5, 0, 4}, {120, 3.25f, -3, 3}, {55, 2.55f, -6, 2}}},
    {"dreamy", 4, {{200, 6, -2, 4}, {250, 4.5f, -4, 3.5f}, {300, 3.4f, -2, 3}, {375, 2.6f, -8, 2.5f}}},
    {"celestial", 4, {{200, 6, 0, 4}, {250, 4.5f, -4, 3.5f}, {281.25f, 3.4f, -9, 3}, {400, 2.6f, -9, 2.5f}}},
    {"warm", 4, {{200, 6, -2, 4}, {250, 4.5f, -4, 3.5f}, {300, 3.4f, -2, 3}, {350, 2.6f, -7, 2.5f}}},
    {"floating", 4, {{200, 6, -2, 4}, {250, 4.5f, -4, 3.5f}, {312.5f, 3.4f, -4, 3}, {390.625f, 2.6f, -9, 2.5f}}},
    {"sacred", 4, {{180, 6, 0, 4}, {270, 4.5f, -4, 3.5f}, {360, 3.4f, -6, 3}, {450, 2.6f, -10, 2.5f}}},
};
}

int main()
{
  check(bed_preset_count() == 8, "eight presets");
  for (int i = 0; i < bed_preset_count(); ++i) {
    if (i >= 8) break;
    const auto& want = expected[i];
    const auto& preset = bed_preset_at(i);
    check(std::string(preset.name) == want.name, "stable preset name/order");
    check(find_bed_preset(want.name) == &preset, "lookup returns corresponding preset");
    for (const float master : {0.f, -33.f}) {
      trance_pb::Entrainment bed;
      bed.set_master_db(master);
      for (int n = 0; n < 6; ++n) bed.add_layer()->set_center_hz(999.f);
      apply_bed_preset(bed, preset);
      check(bed.layer_size() == want.count, "replacement clears prior layers");
      check(bed.master_db() == master, "preserves master and default sentinel");
      for (int n = 0; n < bed.layer_size() && n < want.count; ++n) {
        const auto& layer = bed.layer(n);
        const auto* values = want.values[n];
        check(layer.center_hz() == values[0] && layer.pulse_hz() == values[1] &&
                  layer.amplitude_db() == values[2] && layer.binaural_hz() == values[3],
              std::string(want.name) + " exact numeric layer " + std::to_string(n));
      }
      check(match_bed_preset(bed) == &preset, "matches applied preset");
      const std::string before = bed.SerializeAsString();
      check(find_bed_preset("not-a-preset") == nullptr, "unknown lookup rejected");
      check(bed.SerializeAsString() == before, "unknown lookup does not mutate bed");
      for (int field = 0; field < 4; ++field) {
        trance_pb::Entrainment edited = bed;
        auto* layer = edited.mutable_layer(0);
        if (field == 0) layer->set_center_hz(layer->center_hz() + 1.f);
        if (field == 1) layer->set_pulse_hz(layer->pulse_hz() + 1.f);
        if (field == 2) layer->set_amplitude_db(layer->amplitude_db() - 1.f);
        if (field == 3) layer->set_binaural_hz(layer->binaural_hz() + 1.f);
        check(match_bed_preset(edited) == nullptr, "custom edit removes preset match");
      }
    }
    const auto cmd = command_protocol::parse_command(std::string("bed preset ") + want.name);
    check(cmd.ok && cmd.verb == command_protocol::Verb::kBedPreset && cmd.value == want.name,
          "preset command parses authored name");
  }
  trance_pb::Entrainment empty;
  check(match_bed_preset(empty) == nullptr, "empty bed is not a preset");
  const auto list = command_protocol::parse_command("bed presets");
  check(list.ok && list.verb == command_protocol::Verb::kBedPresets, "list command parses");
  check(!command_protocol::parse_command("bed preset").ok, "missing name rejected");
  check(!command_protocol::parse_command("bed preset sacred extra").ok, "extra argument rejected");
  std::cout << "bed preset checks: " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
