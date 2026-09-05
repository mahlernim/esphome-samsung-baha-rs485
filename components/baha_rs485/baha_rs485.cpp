#include "baha_rs485.h"

#include <algorithm>
#include <cinttypes>

#include "esphome/core/log.h"

namespace esphome::baha_rs485 {

static const char *const TAG = "baha_rs485";

void BahaHeaterSwitch::write_state(bool state) {
  if (this->parent_ == nullptr) {
    return;
  }
  this->parent_->request_heater_state(this->room_, state, this->on_delta_, this->off_delta_);
}

void BahaLightSwitch::write_state(bool state) {
  if (this->parent_ == nullptr) {
    return;
  }
  this->parent_->request_light_state(this->channel_, state);
}

void BahaMasterSwitch::write_state(bool state) {
  if (this->parent_ == nullptr) {
    return;
  }
  this->parent_->request_master_state(state);
}

void BahaRS485Component::setup() {
  this->boot_ms_ = millis();
  this->last_rx_ms_ = 0;
  this->last_tx_ms_ = 0;
  this->next_light_poll_ms_ = this->boot_ms_ + this->startup_listen_window_ms_;
  this->next_master_poll_ms_ = this->boot_ms_ + this->startup_listen_window_ms_;
  this->next_heater_current_poll_ms_ = this->boot_ms_ + this->startup_listen_window_ms_;
  this->next_heater_target_poll_ms_ = this->boot_ms_ + this->startup_listen_window_ms_;
  this->rx_buffer_.reserve(128);
}

void BahaRS485Component::loop() {
  this->read_uart_();

  const uint32_t now = millis();
  this->maybe_poll_groups_(now);
  this->process_tx_queue_(now);
}

void BahaRS485Component::dump_config() {
  ESP_LOGCONFIG(TAG, "BAHA RS485");
  ESP_LOGCONFIG(TAG, "  Stale timeout: %" PRIu32 " ms", this->stale_timeout_ms_);
  ESP_LOGCONFIG(TAG, "  Startup listen window: %" PRIu32 " ms", this->startup_listen_window_ms_);
  ESP_LOGCONFIG(TAG, "  Poll retry interval: %" PRIu32 " ms", this->poll_retry_interval_ms_);
  ESP_LOGCONFIG(TAG, "  Idle before TX: %" PRIu32 " ms", this->idle_before_tx_ms_);
  ESP_LOGCONFIG(TAG, "  Inter-frame gap: %" PRIu32 " ms", this->inter_frame_gap_ms_);
  ESP_LOGCONFIG(TAG, "  Post-write readback: %" PRIu32 " ms", this->post_write_readback_ms_);
  this->check_uart_settings(9600, 1, uart::UART_CONFIG_PARITY_NONE, 8);
}

void BahaRS485Component::register_temperature_sensor(BahaTemperatureSensor *sensor) {
  if (sensor == nullptr || sensor->get_room() >= ROOM_COUNT) {
    return;
  }

  if (sensor->get_kind() == SENSOR_KIND_CURRENT_TEMPERATURE) {
    this->current_sensors_[sensor->get_room()] = sensor;
    if (!std::isnan(this->current_temperatures_[sensor->get_room()])) {
      sensor->publish_state(this->current_temperatures_[sensor->get_room()]);
    }
    return;
  }

  this->target_sensors_[sensor->get_room()] = sensor;
  if (!std::isnan(this->target_temperatures_[sensor->get_room()])) {
    sensor->publish_state(this->target_temperatures_[sensor->get_room()]);
  }
}

void BahaRS485Component::register_heater_switch(BahaHeaterSwitch *heater_switch) {
  if (heater_switch == nullptr || heater_switch->get_room() >= ROOM_COUNT) {
    return;
  }

  this->heater_switches_[heater_switch->get_room()] = heater_switch;
  this->update_heater_switch_state_(heater_switch->get_room());
}

void BahaRS485Component::register_light_switch(BahaLightSwitch *light_switch) {
  if (light_switch == nullptr || light_switch->get_channel() < 1 || light_switch->get_channel() > LIGHT_COUNT) {
    return;
  }

  this->light_switches_[light_switch->get_channel() - 1] = light_switch;
  this->update_light_switch_states_();
}

void BahaRS485Component::register_master_switch(BahaMasterSwitch *master_switch) {
  this->master_switch_ = master_switch;
  if (this->master_state_known_) {
    this->master_switch_->publish_state(this->master_state_);
  }
}

void BahaRS485Component::request_heater_state(uint8_t room, bool state, int8_t on_delta, int8_t off_delta) {
  if (room >= ROOM_COUNT) {
    return;
  }

  if (!this->is_heater_ready_(room)) {
    ESP_LOGW(TAG, "Heater room %u has no current temperature yet; scheduling readback first", room);
    const uint32_t now = millis();
    this->queue_heater_current_poll_(now);
    this->queue_heater_target_poll_(now + this->inter_frame_gap_ms_);
    return;
  }

  const int current = static_cast<int>(std::lround(this->current_temperatures_[room]));
  const int delta = state ? on_delta : off_delta;
  const int target = this->clamp_temperature_(current + delta);

  // A normal-mode write must still leave Away when the numeric target matches.
  if (!this->target_away_[room] && !std::isnan(this->target_temperatures_[room]) &&
      static_cast<int>(std::lround(this->target_temperatures_[room])) == target) {
    return;
  }

  std::vector<uint8_t> payload{0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  payload[room + 1] = static_cast<uint8_t>(0x80 + target);

  const uint32_t now = millis();
  this->queue_frame_(this->build_frame_(HEATER_NODE, HEATER_FUNCTION, 0x02, payload), now);
  this->queue_heater_current_poll_(now + this->post_write_readback_ms_);
  this->queue_heater_target_poll_(now + this->post_write_readback_ms_ + this->inter_frame_gap_ms_);
}

void BahaRS485Component::request_light_state(uint8_t channel, bool state) {
  if (channel < 1 || channel > LIGHT_COUNT) {
    return;
  }

  const uint8_t channel_mask = static_cast<uint8_t>(1U << (channel - 1));
  if (this->light_mask_known_ && (((this->light_mask_ & channel_mask) != 0U) == state)) {
    return;
  }

  const uint32_t now = millis();
  this->queue_frame_(this->build_frame_(LIGHT_NODE, LIGHT_FUNCTION, 0x02,
                                        {channel_mask, static_cast<uint8_t>(state ? channel_mask : 0x00)}),
                     now);
  this->queue_light_poll_(now + this->post_write_readback_ms_);
}

void BahaRS485Component::request_master_state(bool state) {
  if (this->master_state_known_ && this->master_state_ == state) {
    return;
  }

  const uint8_t value = state ? MASTER_STATE_ON : MASTER_STATE_OFF;
  const uint32_t now = millis();
  this->queue_frame_(this->build_frame_(MASTER_NODE, MASTER_FUNCTION, 0x02, {value}), now);
  this->queue_master_poll_(now + this->post_write_readback_ms_);
  this->queue_light_poll_(now + this->post_write_readback_ms_ + this->inter_frame_gap_ms_);
}

void BahaRS485Component::read_uart_() {
  bool got_data = false;
  uint8_t byte = 0;
  while (this->available()) {
    if (!this->read_byte(&byte)) {
      break;
    }
    got_data = true;
    this->rx_buffer_.push_back(byte);
  }

  if (!got_data) {
    return;
  }

  this->last_rx_ms_ = millis();
  if (this->rx_buffer_.size() > 256) {
    this->rx_buffer_.erase(this->rx_buffer_.begin(), this->rx_buffer_.end() - 128);
  }
  this->parse_rx_buffer_();
}

void BahaRS485Component::parse_rx_buffer_() {
  while (!this->rx_buffer_.empty()) {
    if (this->rx_buffer_[0] != 0x02) {
      this->rx_buffer_.erase(this->rx_buffer_.begin());
      continue;
    }

    if (this->rx_buffer_.size() < 5) {
      return;
    }

    const size_t frame_len = static_cast<size_t>(this->rx_buffer_[4]) + 7U;
    if (frame_len > MAX_FRAME_LEN) {
      this->rx_buffer_.erase(this->rx_buffer_.begin());
      continue;
    }
    if (this->rx_buffer_.size() < frame_len) {
      return;
    }
    if (this->rx_buffer_[frame_len - 1] != 0x00) {
      this->rx_buffer_.erase(this->rx_buffer_.begin());
      continue;
    }

    uint8_t checksum = 0x00;
    for (size_t i = 0; i < frame_len - 2; i++) {
      checksum ^= this->rx_buffer_[i];
    }
    if (checksum != this->rx_buffer_[frame_len - 2]) {
      this->rx_buffer_.erase(this->rx_buffer_.begin());
      continue;
    }

    std::vector<uint8_t> frame(this->rx_buffer_.begin(), this->rx_buffer_.begin() + static_cast<long>(frame_len));
    this->rx_buffer_.erase(this->rx_buffer_.begin(), this->rx_buffer_.begin() + static_cast<long>(frame_len));
    this->handle_frame_(frame);
  }
}

void BahaRS485Component::handle_frame_(const std::vector<uint8_t> &frame) {
  if (frame.size() < 7) {
    return;
  }

  const uint8_t node = frame[1];
  const uint8_t function = frame[2];
  const uint8_t opcode = frame[3];
  const std::vector<uint8_t> payload(frame.begin() + 5, frame.end() - 2);
  const uint32_t now = millis();

  if (node == LIGHT_NODE && function == LIGHT_FUNCTION && opcode == 0x81) {
    this->last_light_seen_ms_ = now;
    this->next_light_poll_ms_ = now + this->stale_timeout_ms_;
    this->handle_light_status_(payload);
    return;
  }

  if (node == MASTER_NODE && function == MASTER_FUNCTION && opcode == 0x81) {
    this->last_master_seen_ms_ = now;
    this->next_master_poll_ms_ = now + this->stale_timeout_ms_;
    this->handle_master_status_(payload);
    return;
  }

  if (node == HEATER_NODE && function == HEATER_FUNCTION && opcode == 0x81) {
    this->last_heater_current_seen_ms_ = now;
    this->next_heater_current_poll_ms_ = now + this->stale_timeout_ms_;
    this->handle_heater_current_(payload);
    return;
  }

  if (node == HEATER_NODE && function == HEATER_FUNCTION && opcode == 0x85) {
    this->last_heater_target_seen_ms_ = now;
    this->next_heater_target_poll_ms_ = now + this->stale_timeout_ms_;
    this->handle_heater_target_(payload);
  }
}

void BahaRS485Component::handle_light_status_(const std::vector<uint8_t> &payload) {
  if (payload.size() < 2) {
    return;
  }

  const uint8_t capability_mask = payload[0];
  this->light_mask_ = payload[1] & capability_mask;
  this->light_mask_known_ = true;
  this->update_light_switch_states_();
}

void BahaRS485Component::handle_master_status_(const std::vector<uint8_t> &payload) {
  if (payload.empty()) {
    return;
  }

  if (payload[0] == MASTER_STATE_TRANSIENT) {
    return;
  }
  if (payload[0] == MASTER_STATE_ON) {
    this->update_master_switch_state_(true);
    return;
  }
  if (payload[0] == MASTER_STATE_OFF) {
    this->update_master_switch_state_(false);
  }
}

void BahaRS485Component::handle_heater_current_(const std::vector<uint8_t> &payload) {
  if (payload.size() < 6) {
    return;
  }

  for (uint8_t room = 0; room < ROOM_COUNT; room++) {
    const float value = this->decode_temperature_(payload[room + 1]);
    this->current_temperatures_[room] = value;
    this->publish_temperature_(room, SENSOR_KIND_CURRENT_TEMPERATURE, value);
    this->update_heater_switch_state_(room);
  }
}

void BahaRS485Component::handle_heater_target_(const std::vector<uint8_t> &payload) {
  if (payload.size() < 6) {
    return;
  }

  for (uint8_t room = 0; room < ROOM_COUNT; room++) {
    this->target_away_[room] = (payload[room + 1] & 0xC0) == 0xC0;
    const float value = this->decode_temperature_(payload[room + 1]);
    this->target_temperatures_[room] = value;
    this->publish_temperature_(room, SENSOR_KIND_TARGET_TEMPERATURE, value);
    this->update_heater_switch_state_(room);
  }
}

void BahaRS485Component::maybe_poll_groups_(uint32_t now) {
  this->maybe_poll_group_(now, this->last_light_seen_ms_, this->next_light_poll_ms_, &BahaRS485Component::queue_light_poll_);
  this->maybe_poll_group_(now, this->last_master_seen_ms_, this->next_master_poll_ms_,
                          &BahaRS485Component::queue_master_poll_);
  this->maybe_poll_group_(now, this->last_heater_current_seen_ms_, this->next_heater_current_poll_ms_,
                          &BahaRS485Component::queue_heater_current_poll_);
  this->maybe_poll_group_(now, this->last_heater_target_seen_ms_, this->next_heater_target_poll_ms_,
                          &BahaRS485Component::queue_heater_target_poll_);
}

void BahaRS485Component::maybe_poll_group_(uint32_t now, uint32_t &last_seen_ms, uint32_t &next_poll_ms,
                                           void (BahaRS485Component::*queue_fn)(uint32_t)) {
  if (!this->time_reached_(now, next_poll_ms)) {
    return;
  }

  if (last_seen_ms != 0 && now - last_seen_ms < this->stale_timeout_ms_) {
    next_poll_ms = last_seen_ms + this->stale_timeout_ms_;
    return;
  }

  (this->*queue_fn)(now);
  next_poll_ms = now + this->poll_retry_interval_ms_;
}

void BahaRS485Component::queue_light_poll_(uint32_t due_ms) {
  this->queue_frame_(this->build_frame_(LIGHT_NODE, LIGHT_FUNCTION, 0x01, {}), due_ms);
}

void BahaRS485Component::queue_master_poll_(uint32_t due_ms) {
  this->queue_frame_(this->build_frame_(MASTER_NODE, MASTER_FUNCTION, 0x01, {}), due_ms);
}

void BahaRS485Component::queue_heater_current_poll_(uint32_t due_ms) {
  this->queue_frame_(this->build_frame_(HEATER_NODE, HEATER_FUNCTION, 0x01, {}), due_ms);
}

void BahaRS485Component::queue_heater_target_poll_(uint32_t due_ms) {
  this->queue_frame_(this->build_frame_(HEATER_NODE, HEATER_FUNCTION, 0x05, {}), due_ms);
}

void BahaRS485Component::queue_frame_(std::vector<uint8_t> &&frame, uint32_t due_ms) {
  if (this->tx_queue_.size() >= MAX_TX_QUEUE) {
    ESP_LOGW(TAG, "TX queue full; dropping frame");
    return;
  }

  auto it = this->tx_queue_.begin();
  for (; it != this->tx_queue_.end(); ++it) {
    if (static_cast<int32_t>(due_ms - it->due_ms) < 0) {
      break;
    }
  }
  this->tx_queue_.insert(it, TxFrame{std::move(frame), due_ms});
}

void BahaRS485Component::process_tx_queue_(uint32_t now) {
  if (this->tx_queue_.empty()) {
    return;
  }
  if (!this->time_reached_(now, this->tx_queue_.front().due_ms)) {
    return;
  }
  if (this->last_tx_ms_ != 0 && now - this->last_tx_ms_ < this->inter_frame_gap_ms_) {
    return;
  }
  if (this->last_rx_ms_ != 0 && now - this->last_rx_ms_ < this->idle_before_tx_ms_) {
    return;
  }

  this->write_array(this->tx_queue_.front().data);
  this->flush();
  this->last_tx_ms_ = now;
  this->tx_queue_.pop_front();
}

std::vector<uint8_t> BahaRS485Component::build_frame_(uint8_t node, uint8_t function, uint8_t opcode,
                                                      const std::vector<uint8_t> &payload) const {
  std::vector<uint8_t> frame;
  frame.reserve(payload.size() + 7U);
  frame.push_back(0x02);
  frame.push_back(node);
  frame.push_back(function);
  frame.push_back(opcode);
  frame.push_back(static_cast<uint8_t>(payload.size()));
  frame.insert(frame.end(), payload.begin(), payload.end());

  uint8_t checksum = 0x00;
  for (uint8_t byte : frame) {
    checksum ^= byte;
  }

  frame.push_back(checksum);
  frame.push_back(0x00);
  return frame;
}

void BahaRS485Component::publish_temperature_(uint8_t room, SensorKind kind, float value) {
  if (room >= ROOM_COUNT || std::isnan(value)) {
    return;
  }

  if (kind == SENSOR_KIND_CURRENT_TEMPERATURE) {
    if (this->current_sensors_[room] != nullptr) {
      this->current_sensors_[room]->publish_state(value);
    }
    return;
  }

  if (this->target_sensors_[room] != nullptr) {
    this->target_sensors_[room]->publish_state(value);
  }
}

void BahaRS485Component::update_heater_switch_state_(uint8_t room) {
  if (room >= ROOM_COUNT || this->heater_switches_[room] == nullptr) {
    return;
  }
  if (std::isnan(this->current_temperatures_[room]) || std::isnan(this->target_temperatures_[room])) {
    return;
  }

  this->heater_switches_[room]->publish_state(this->target_temperatures_[room] > this->current_temperatures_[room]);
}

void BahaRS485Component::update_light_switch_states_() {
  if (!this->light_mask_known_) {
    return;
  }

  for (uint8_t index = 0; index < LIGHT_COUNT; index++) {
    if (this->light_switches_[index] == nullptr) {
      continue;
    }
    const bool state = (this->light_mask_ & static_cast<uint8_t>(1U << index)) != 0U;
    this->light_switches_[index]->publish_state(state);
  }
}

void BahaRS485Component::update_master_switch_state_(bool state) {
  this->master_state_known_ = true;
  this->master_state_ = state;
  if (this->master_switch_ != nullptr) {
    this->master_switch_->publish_state(state);
  }
}

float BahaRS485Component::decode_temperature_(uint8_t value) const {
  if (value >= 0x80) {
    // Bit 0x40 is Away mode in both current (81) and target (85) tables.
    return static_cast<float>(value & 0x3F);
  }
  return static_cast<float>(value);
}

int BahaRS485Component::clamp_temperature_(int value) const {
  return std::min(35, std::max(5, value));
}

bool BahaRS485Component::is_heater_ready_(uint8_t room) const {
  return room < ROOM_COUNT && !std::isnan(this->current_temperatures_[room]);
}

bool BahaRS485Component::time_reached_(uint32_t now, uint32_t due_ms) {
  return due_ms != 0 && static_cast<int32_t>(now - due_ms) >= 0;
}

void BahaRS485Component::schedule_earlier_(uint32_t &due_ms, uint32_t candidate_ms) {
  if (due_ms == 0 || static_cast<int32_t>(candidate_ms - due_ms) < 0) {
    due_ms = candidate_ms;
  }
}

}  // namespace esphome::baha_rs485
