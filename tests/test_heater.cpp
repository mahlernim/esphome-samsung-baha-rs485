#include "baha_rs485.h"

#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace esphome::baha_rs485;

void require(bool condition, const std::string &message) {
  if (!condition) throw std::runtime_error(message);
}

std::vector<uint8_t> hex(const char *text) {
  std::vector<uint8_t> bytes;
  std::istringstream input(text);
  unsigned value;
  while (input >> std::hex >> value) bytes.push_back(static_cast<uint8_t>(value));
  return bytes;
}

class TestBus : public BahaRS485Component {
 public:
  void feed(const std::vector<uint8_t> &bytes) {
    rx_buffer_.insert(rx_buffer_.end(), bytes.begin(), bytes.end());
    parse_rx_buffer_();
  }
  const std::deque<TxFrame> &queued() const { return tx_queue_; }
  void clear_queue() { tx_queue_.clear(); }
  float decode(uint8_t value) const { return decode_temperature_(value); }
};

struct Fixture {
  TestBus bus;
  std::array<BahaTemperatureSensor, 5> current;
  std::array<BahaTemperatureSensor, 5> target;
  std::array<BahaHeaterSwitch, 5> switches;

  Fixture() {
    for (uint8_t room = 0; room < 5; ++room) {
      current[room].set_room(room);
      current[room].set_kind(SENSOR_KIND_CURRENT_TEMPERATURE);
      target[room].set_room(room);
      target[room].set_kind(SENSOR_KIND_TARGET_TEMPERATURE);
      switches[room].set_room(room);
      bus.register_temperature_sensor(&current[room]);
      bus.register_temperature_sensor(&target[room]);
      bus.register_heater_switch(&switches[room]);
    }
  }
};

// Recorded July 2026 frames; see baha-wallpad-packets/reference/raw-session-notes.md.
const auto NORMAL_CURRENT = hex("02 40 90 81 06 02 A1 9E A0 A0 9F F7 00");
const auto NORMAL_TARGET = hex("02 40 90 85 06 02 92 95 93 85 91 D3 00");
const auto AWAY_CURRENT = hex("02 40 90 81 06 02 A1 9E E0 A0 9F B7 00");
const auto AWAY_TARGET = hex("02 40 90 85 06 02 92 95 CA 85 91 8A 00");

void expect_temperatures(const std::array<BahaTemperatureSensor, 5> &sensors,
                         const std::array<float, 5> &expected) {
  for (size_t i = 0; i < sensors.size(); ++i) {
    require(sensors[i].has_state && sensors[i].state == expected[i],
            "zone " + std::to_string(i + 1) + ": expected " + std::to_string(expected[i]) +
            ", got " + std::to_string(sensors[i].state));
  }
}

void expect_write(TestBus &bus, const char *expected) {
  require(bus.queued().size() == 3, "expected a write and two readbacks");
  require(bus.queued()[0].data == hex(expected), "wrong setpoint write or checksum");
  require(bus.queued()[1].data == hex("02 40 90 01 00 D3 00"), "missing current readback");
  require(bus.queued()[2].data == hex("02 40 90 05 00 D7 00"), "missing target readback");
}

void normal_capture() {
  Fixture f;
  f.bus.feed(NORMAL_CURRENT);
  f.bus.feed(NORMAL_TARGET);
  expect_temperatures(f.current, {33, 30, 32, 32, 31});
  expect_temperatures(f.target, {18, 21, 19, 5, 17});
}

void mixed_away_capture() {
  Fixture f;
  f.bus.feed(AWAY_CURRENT);
  f.bus.feed(AWAY_TARGET);
  expect_temperatures(f.current, {33, 30, 32, 32, 31});
  expect_temperatures(f.target, {18, 21, 10, 5, 17});
  for (const auto &sw : f.switches) require(sw.has_state && !sw.state, "unexpected heat demand");
}

void all_away_capture() {
  Fixture f;
  f.bus.feed(hex("02 40 90 81 06 03 D7 D6 D5 D6 D5 81 00"));
  f.bus.feed(hex("02 40 90 85 06 03 CA CA CA CA CA 98 00"));
  expect_temperatures(f.current, {23, 22, 21, 22, 21});
  expect_temperatures(f.target, {10, 10, 10, 10, 10});
}

void fragmented_capture() {
  Fixture f;
  f.bus.feed({AWAY_CURRENT.begin(), AWAY_CURRENT.begin() + 7});
  require(!f.current[2].has_state, "published incomplete frame");
  f.bus.feed({AWAY_CURRENT.begin() + 7, AWAY_CURRENT.end()});
  require(f.current[2].has_state && f.current[2].state == 32, "fragmented Away frame failed");
}

void away_write(bool on) {
  Fixture f;
  f.bus.feed(AWAY_CURRENT);
  f.bus.feed(AWAY_TARGET);
  f.bus.request_heater_state(2, on, 1, -2);
  expect_write(f.bus, on ? "02 40 90 02 06 00 00 00 A1 00 00 77 00"
                         : "02 40 90 02 06 00 00 00 9E 00 00 48 00");
}

// Synthetic boundary frames: a normal 10 C request must still exit Away at 10 C.
void equal_temperature_mode_change(bool on) {
  Fixture f;
  f.bus.feed(hex(on ? "02 40 90 81 06 03 C9 CA CA CA CA 9F 00"
                   : "02 40 90 81 06 03 CC CA CA CA CA 9A 00"));
  f.bus.feed(hex("02 40 90 85 06 03 CA CA CA CA CA 98 00"));
  f.bus.request_heater_state(0, on, 1, -2);
  expect_write(f.bus, "02 40 90 02 06 00 8A 00 00 00 00 5C 00");

  // A normal-mode readback clears the Away flag; duplicates can then be skipped.
  f.bus.clear_queue();
  f.bus.feed(hex("02 40 90 85 06 02 8A CA CA CA CA D9 00"));
  f.bus.request_heater_state(0, on, 1, -2);
  require(f.bus.queued().empty(), "unchanged normal setpoint should be deduplicated");
}

void normal_write() {
  Fixture f;
  f.bus.feed(NORMAL_CURRENT);
  f.bus.feed(NORMAL_TARGET);
  f.bus.request_heater_state(2, true, 1, -2);
  expect_write(f.bus, "02 40 90 02 06 00 00 00 A1 00 00 77 00");
}

void switch_demand() {
  Fixture f;
  f.bus.feed(hex("02 40 90 81 06 03 C9 CA CA CA CA 9F 00"));
  f.bus.feed(hex("02 40 90 85 06 03 CA CA CA CA CA 98 00"));
  require(f.switches[0].has_state && f.switches[0].state, "target above current is demand");
  require(f.switches[1].has_state && !f.switches[1].state, "equal temperatures are not demand");
}

void raw_byte_compatibility() {
  TestBus bus;
  for (unsigned value = 0; value < 0x80; ++value)
    require(bus.decode(static_cast<uint8_t>(value)) == value, "raw-byte fallback changed");
}

int main() {
  const std::vector<std::pair<const char *, std::function<void()>>> tests{
      {"normal capture", normal_capture}, {"mixed Away capture", mixed_away_capture},
      {"all Away capture", all_away_capture}, {"fragmented capture", fragmented_capture},
      {"Away On writes 33 C", [] { away_write(true); }},
      {"Away Off writes 30 C", [] { away_write(false); }},
      {"equal target On exits Away", [] { equal_temperature_mode_change(true); }},
      {"equal target Off exits Away", [] { equal_temperature_mode_change(false); }},
      {"normal write unchanged", normal_write}, {"switch demand", switch_demand},
      {"raw-byte compatibility", raw_byte_compatibility}};
  size_t failures = 0;
  for (const auto &test : tests) {
    try { test.second(); std::cout << "PASS " << test.first << '\n'; }
    catch (const std::exception &error) {
      ++failures;
      std::cerr << "FAIL " << test.first << ": " << error.what() << '\n';
    }
  }
  std::cout << tests.size() << " tests, " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
