#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <vector>

#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/hal.h"

namespace esphome::baha_rs485 {

enum SensorKind : uint8_t {
  SENSOR_KIND_CURRENT_TEMPERATURE = 0,
  SENSOR_KIND_TARGET_TEMPERATURE = 1,
};

class BahaRS485Component;

class BahaTemperatureSensor : public sensor::Sensor, public Parented<BahaRS485Component> {
 public:
  void set_room(uint8_t room) { this->room_ = room; }
  void set_kind(SensorKind kind) { this->kind_ = kind; }
  uint8_t get_room() const { return this->room_; }
  SensorKind get_kind() const { return this->kind_; }

 protected:
  uint8_t room_{0};
  SensorKind kind_{SENSOR_KIND_CURRENT_TEMPERATURE};
};

class BahaHeaterSwitch : public switch_::Switch, public Parented<BahaRS485Component> {
 public:
  void set_room(uint8_t room) { this->room_ = room; }
  void set_on_delta(int8_t on_delta) { this->on_delta_ = on_delta; }
  void set_off_delta(int8_t off_delta) { this->off_delta_ = off_delta; }
  uint8_t get_room() const { return this->room_; }

 protected:
  void write_state(bool state) override;

  uint8_t room_{0};
  int8_t on_delta_{1};
  int8_t off_delta_{-2};
};

class BahaLightSwitch : public switch_::Switch, public Parented<BahaRS485Component> {
 public:
  void set_channel(uint8_t channel) { this->channel_ = channel; }
  uint8_t get_channel() const { return this->channel_; }

 protected:
  void write_state(bool state) override;

  uint8_t channel_{1};
};

class BahaMasterSwitch : public switch_::Switch, public Parented<BahaRS485Component> {
 protected:
  void write_state(bool state) override;
};

class BahaRS485Component : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_stale_timeout(uint32_t stale_timeout_ms) { this->stale_timeout_ms_ = stale_timeout_ms; }
  void set_startup_listen_window(uint32_t startup_listen_window_ms) {
    this->startup_listen_window_ms_ = startup_listen_window_ms;
  }
  void set_poll_retry_interval(uint32_t poll_retry_interval_ms) {
    this->poll_retry_interval_ms_ = poll_retry_interval_ms;
  }
  void set_idle_before_tx(uint32_t idle_before_tx_ms) { this->idle_before_tx_ms_ = idle_before_tx_ms; }
  void set_inter_frame_gap(uint32_t inter_frame_gap_ms) { this->inter_frame_gap_ms_ = inter_frame_gap_ms; }
  void set_post_write_readback(uint32_t post_write_readback_ms) {
    this->post_write_readback_ms_ = post_write_readback_ms;
  }

  void register_temperature_sensor(BahaTemperatureSensor *sensor);
  void register_heater_switch(BahaHeaterSwitch *heater_switch);
  void register_light_switch(BahaLightSwitch *light_switch);
  void register_master_switch(BahaMasterSwitch *master_switch);

  void request_heater_state(uint8_t room, bool state, int8_t on_delta, int8_t off_delta);
  void request_light_state(uint8_t channel, bool state);
  void request_master_state(bool state);

 protected:
  static constexpr uint8_t ROOM_COUNT = 5;
  static constexpr uint8_t LIGHT_COUNT = 4;
  static constexpr uint8_t LIGHT_NODE = 0x10;
  static constexpr uint8_t LIGHT_FUNCTION = 0x04;
  static constexpr uint8_t MASTER_NODE = 0x1F;
  static constexpr uint8_t MASTER_FUNCTION = 0x0F;
  static constexpr uint8_t HEATER_NODE = 0x40;
  static constexpr uint8_t HEATER_FUNCTION = 0x90;
  static constexpr size_t MAX_FRAME_LEN = 64;
  static constexpr size_t MAX_TX_QUEUE = 24;
  static constexpr uint8_t MASTER_STATE_OFF = 0x09;
  static constexpr uint8_t MASTER_STATE_ON = 0x0A;
  static constexpr uint8_t MASTER_STATE_TRANSIENT = 0x08;

  struct TxFrame {
    std::vector<uint8_t> data;
    uint32_t due_ms;
  };

  void read_uart_();
  void parse_rx_buffer_();
  void handle_frame_(const std::vector<uint8_t> &frame);
  void handle_light_status_(const std::vector<uint8_t> &payload);
  void handle_master_status_(const std::vector<uint8_t> &payload);
  void handle_heater_current_(const std::vector<uint8_t> &payload);
  void handle_heater_target_(const std::vector<uint8_t> &payload);

  void maybe_poll_groups_(uint32_t now);
  void maybe_poll_group_(uint32_t now, uint32_t &last_seen_ms, uint32_t &next_poll_ms,
                         void (BahaRS485Component::*queue_fn)(uint32_t));

  void queue_light_poll_(uint32_t due_ms);
  void queue_master_poll_(uint32_t due_ms);
  void queue_heater_current_poll_(uint32_t due_ms);
  void queue_heater_target_poll_(uint32_t due_ms);
  void queue_frame_(std::vector<uint8_t> &&frame, uint32_t due_ms);
  void process_tx_queue_(uint32_t now);

  std::vector<uint8_t> build_frame_(uint8_t node, uint8_t function, uint8_t opcode,
                                    const std::vector<uint8_t> &payload) const;

  void publish_temperature_(uint8_t room, SensorKind kind, float value);
  void update_heater_switch_state_(uint8_t room);
  void update_light_switch_states_();
  void update_master_switch_state_(bool state);

  float decode_temperature_(uint8_t value) const;
  int clamp_temperature_(int value) const;
  bool is_heater_ready_(uint8_t room) const;

  static bool time_reached_(uint32_t now, uint32_t due_ms);
  static void schedule_earlier_(uint32_t &due_ms, uint32_t candidate_ms);

  uint32_t stale_timeout_ms_{30000};
  uint32_t startup_listen_window_ms_{10000};
  uint32_t poll_retry_interval_ms_{5000};
  uint32_t idle_before_tx_ms_{80};
  uint32_t inter_frame_gap_ms_{120};
  uint32_t post_write_readback_ms_{400};

  uint32_t boot_ms_{0};
  uint32_t last_rx_ms_{0};
  uint32_t last_tx_ms_{0};

  uint32_t last_light_seen_ms_{0};
  uint32_t last_master_seen_ms_{0};
  uint32_t last_heater_current_seen_ms_{0};
  uint32_t last_heater_target_seen_ms_{0};

  uint32_t next_light_poll_ms_{0};
  uint32_t next_master_poll_ms_{0};
  uint32_t next_heater_current_poll_ms_{0};
  uint32_t next_heater_target_poll_ms_{0};

  std::vector<uint8_t> rx_buffer_;
  std::deque<TxFrame> tx_queue_;

  std::array<BahaTemperatureSensor *, ROOM_COUNT> current_sensors_{};
  std::array<BahaTemperatureSensor *, ROOM_COUNT> target_sensors_{};
  std::array<BahaHeaterSwitch *, ROOM_COUNT> heater_switches_{};
  std::array<BahaLightSwitch *, LIGHT_COUNT> light_switches_{};
  BahaMasterSwitch *master_switch_{nullptr};

  std::array<float, ROOM_COUNT> current_temperatures_{{NAN, NAN, NAN, NAN, NAN}};
  std::array<float, ROOM_COUNT> target_temperatures_{{NAN, NAN, NAN, NAN, NAN}};
  // Keep each table's mode with its cached value; 81 and 85 arrive separately.
  std::array<bool, ROOM_COUNT> current_away_{};
  std::array<bool, ROOM_COUNT> target_away_{};

  uint8_t light_mask_{0};
  bool light_mask_known_{false};
  bool master_state_known_{false};
  bool master_state_{false};
};

}  // namespace esphome::baha_rs485
