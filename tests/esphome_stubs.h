#pragma once

// Minimal host-side interfaces. The parser, decoder and transmit queue under
// test are compiled directly from components/baha_rs485/baha_rs485.cpp.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace esphome {
inline uint32_t millis() { return 1000; }

class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
};

template<typename T> class Parented {
 public:
  void set_parent(T *parent) { parent_ = parent; }
 protected:
  T *parent_{nullptr};
};

namespace sensor {
class Sensor {
 public:
  float state{0};
  bool has_state{false};
  void publish_state(float value) { state = value; has_state = true; }
};
}

namespace switch_ {
class Switch {
 public:
  virtual ~Switch() = default;
  bool state{false};
  bool has_state{false};
  void publish_state(bool value) { state = value; has_state = true; }
 protected:
  virtual void write_state(bool state) = 0;
};
}

namespace uart {
enum UARTParityOptions { UART_CONFIG_PARITY_NONE };
class UARTDevice {
 public:
  int available() { return 0; }
  bool read_byte(uint8_t *) { return false; }
  void write_array(const std::vector<uint8_t> &) {}
  void flush() {}
  void check_uart_settings(int, int, UARTParityOptions, int) {}
};
}
}

#define ESP_LOGCONFIG(...) ((void) 0)
#define ESP_LOGW(...) ((void) 0)
