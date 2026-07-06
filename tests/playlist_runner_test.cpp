// Behavioral test for the playlist stack machine (src/trance/playlist_runner.{h,cpp}):
// standard-item hold/handoff timing, subroutine push/pop ordering, pause freeze, and
// the stack-overflow guard. Headless like session_json_test: links common_lib for the
// proto types, no SFML/GL/audio.
#include <trance/playlist_runner.h>

#include <iostream>
#include <string>
#include <vector>

#pragma warning(push, 0)
#include <common/trance.pb.h>
#pragma warning(pop)

namespace
{
  int g_fail = 0;
  void check(bool ok, const std::string& what)
  {
    std::cout << (ok ? "  ok  " : "FAIL  ") << what << "\n";
    if (!ok) ++g_fail;
  }

  std::string joined(const std::vector<std::string>& names)
  {
    std::string out;
    for (const auto& name : names) {
      out += (out.empty() ? "" : ",") + name;
    }
    return out;
  }
}

int main()
{
  // A (1s, -> B) and B (1s, no next): hold until play_time elapses, hand off once,
  // then hold forever on the dead end.
  {
    trance_pb::Session session;
    session.set_first_playlist_item("A");
    auto& a = (*session.mutable_playlist())["A"];
    a.mutable_standard()->set_play_time_seconds(1);
    auto* next = a.add_next_item();
    next->set_playlist_item_name("B");
    next->set_random_weight(1);
    auto& b = (*session.mutable_playlist())["B"];
    b.mutable_standard()->set_play_time_seconds(1);

    PlaylistRunner runner{session, {}};
    std::vector<std::string> entered;
    auto on_enter = [&](const std::string& name, const trance_pb::PlaylistItem&) {
      entered.push_back(name);
    };
    runner.start(0);
    check(&runner.current() == &session.playlist().at("A"), "starts on first_playlist_item");
    runner.advance(500, on_enter);
    check(entered.empty(), "standard item holds before play_time elapses");
    runner.advance(1000, on_enter);
    check(joined(entered) == "B", "hands off to the weighted next at play_time");
    check(&runner.current() == &session.playlist().at("B"), "current() tracks the handoff");
    runner.advance(60000, on_enter);
    check(joined(entered) == "B", "an item with no next holds forever");
  }

  // Pause freeze: shifting the switch clock postpones the handoff by exactly the
  // frozen time.
  {
    trance_pb::Session session;
    session.set_first_playlist_item("A");
    auto& a = (*session.mutable_playlist())["A"];
    a.mutable_standard()->set_play_time_seconds(1);
    auto* next = a.add_next_item();
    next->set_playlist_item_name("A");
    next->set_random_weight(1);

    PlaylistRunner runner{session, {}};
    std::vector<std::string> entered;
    auto on_enter = [&](const std::string& name, const trance_pb::PlaylistItem&) {
      entered.push_back(name);
    };
    runner.start(0);
    runner.freeze(600);
    runner.advance(1100, on_enter);
    check(entered.empty(), "freeze() postpones the timeout");
    runner.advance(1600, on_enter);
    check(joined(entered) == "A", "handoff fires once the frozen time is served");
  }

  // Subroutine: S = [C, C]; each step enters C, C's timeout pops back, S resumes at
  // the next step, and an exhausted S with no next holds.
  {
    trance_pb::Session session;
    session.set_first_playlist_item("S");
    auto& sub = (*session.mutable_playlist())["S"];
    sub.mutable_subroutine()->add_playlist_item_name("C");
    sub.mutable_subroutine()->add_playlist_item_name("C");
    auto& c = (*session.mutable_playlist())["C"];
    c.mutable_standard()->set_play_time_seconds(1);

    PlaylistRunner runner{session, {}};
    std::vector<std::string> entered;
    auto on_enter = [&](const std::string& name, const trance_pb::PlaylistItem&) {
      entered.push_back(name);
    };
    runner.start(0);
    runner.advance(0, on_enter);
    check(joined(entered) == "C", "subroutine pushes its first step immediately");
    check(&runner.current() == &session.playlist().at("C"), "current() is the pushed step");
    runner.advance(1000, on_enter);
    check(joined(entered) == "C,C", "step timeout pops and pushes the next step");
    runner.advance(2000, on_enter);
    check(joined(entered) == "C,C", "exhausted subroutine pops and holds");
    check(&runner.current() == &session.playlist().at("S"), "current() is back on the subroutine");
    runner.advance(60000, on_enter);
    check(joined(entered) == "C,C", "no re-entry after exhaustion");
  }

  // Self-recursive subroutine: the MAXIMUM_STACK guard must terminate (no hang, no
  // crash) rather than pushing forever.
  {
    trance_pb::Session session;
    session.set_first_playlist_item("R");
    auto& r = (*session.mutable_playlist())["R"];
    r.mutable_subroutine()->add_playlist_item_name("R");

    PlaylistRunner runner{session, {}};
    int enters = 0;
    auto on_enter = [&](const std::string&, const trance_pb::PlaylistItem&) { ++enters; };
    runner.start(0);
    runner.advance(0, on_enter);
    check(enters > 0 && enters < 300, "recursive subroutine bounded by MAXIMUM_STACK");
  }

  if (g_fail) {
    std::cout << g_fail << " FAILURES\n";
    return 1;
  }
  std::cout << "all ok\n";
  return 0;
}
