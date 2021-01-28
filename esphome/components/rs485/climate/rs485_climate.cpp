#include "rs485_climate.h"
#include "esphome/core/log.h"

namespace esphome {
namespace rs485 {

static const char *TAG = "rs485.climate";

void RS485Climate::dump_config() {
    ESP_LOGCONFIG(TAG, "RS485 Climate '%s':", device_name_->c_str());
    dump_rs485_device_config(TAG);
}

climate::ClimateTraits RS485Climate::traits() {
  auto traits = climate::ClimateTraits();
  traits.set_supports_current_temperature(true);
  traits.set_supports_auto_mode(this->supports_auto_);
  traits.set_supports_cool_mode(this->supports_cool_);
  traits.set_supports_heat_mode(this->supports_heat_);
  traits.set_supports_two_point_target_temperature(false);
  traits.set_supports_away(this->supports_away_);
  traits.set_visual_min_temperature(5);
  traits.set_visual_max_temperature(40);
  traits.set_visual_temperature_step(1);
  return traits;
}

void RS485Climate::setup() {
  this->target_temperature = NAN;
  if (this->sensor_) {
    this->sensor_->add_on_state_callback([this](float state) {
      this->current_temperature = state;
      // current temperature changed, publish state
      this->publish_state();
    });
    this->current_temperature = this->sensor_->state;
  } else
    this->current_temperature = NAN;
}

void RS485Climate::publish(const uint8_t *data, const num_t len) {
  bool changed = false;

  // turn off
  if(this->state_off_.has_value() && compare(&data[0], len, &state_off_.value())) {
    if(this->mode != climate::CLIMATE_MODE_OFF) {
      this->mode = climate::CLIMATE_MODE_OFF;
      changed = true;
    }
  }
  // heat mode
  else if(this->state_heat_.has_value() && compare(&data[0], len, &state_heat_.value())) {
    if(this->mode != climate::CLIMATE_MODE_HEAT) {
      this->mode = climate::CLIMATE_MODE_HEAT;
      changed = true;
    }
  }
  // cool mode
  else if(this->state_cool_.has_value() && compare(&data[0], len, &state_cool_.value())) {
    if(this->mode != climate::CLIMATE_MODE_COOL) {
      this->mode = climate::CLIMATE_MODE_COOL;
      changed = true;
    }
  }
  // auto mode
  else if(this->state_auto_.has_value() && compare(&data[0], len, &state_auto_.value())) {
    if(this->mode != climate::CLIMATE_MODE_AUTO) {
      this->mode = climate::CLIMATE_MODE_AUTO;
      changed = true;
    }
  }
  // away
  if(this->state_away_.has_value()) {
    if(this->away != compare(&data[0], len, &state_away_.value())) {
      this->away = !this->away;
      changed = true;
    }
  }

  // Current temperature
  if(this->sensor_ == nullptr) {
    if (this->state_current_func_.has_value()) {
      optional<float> val = (*this->state_current_func_)(data, len);
      if(val.has_value() && this->current_temperature != val.value()) {
        this->current_temperature = val.value();
        changed = true;
      }
    }
    else if(this->state_current_.has_value() && len >= (this->state_current_.value().offset + this->state_current_.value().length)) {
      float val = hex_to_float(&data[this->state_current_.value().offset], this->state_current_.value().length, this->state_current_.value().precision);
      if(this->current_temperature != val) {
        this->current_temperature = val;
        changed = true;
      }
    }
  }

  // Target temperature
  if (this->state_target_func_.has_value()) {
    optional<float> val = (*this->state_target_func_)(data, len);
    if(val.has_value() && this->target_temperature != val.value()) {
      this->target_temperature = val.value();
      changed = true;
    }
  }
  else if(this->state_target_.has_value() && len >= (this->state_target_.value().offset + this->state_target_.value().length)) {
    float val = hex_to_float(&data[this->state_target_.value().offset], this->state_target_.value().length, this->state_target_.value().precision);
    if(this->target_temperature != val) {
      this->target_temperature = val;
      changed = true;
    }
  }

  if(changed) this->publish_state();
}


void RS485Climate::control(const climate::ClimateCall &call) {
  // Set mode
  if (call.get_mode().has_value() && this->mode != *call.get_mode()) {
    this->mode = *call.get_mode();
    if(this->mode == climate::CLIMATE_MODE_OFF) write_with_header(this->get_command_off());
    else if(this->mode == climate::CLIMATE_MODE_HEAT && this->command_heat_.has_value()) write_with_header(&this->command_heat_.value());
    else if(this->mode == climate::CLIMATE_MODE_COOL && this->command_cool_.has_value()) write_with_header(&this->command_cool_.value());
    else if(this->mode == climate::CLIMATE_MODE_AUTO) {
      if(this->command_auto_.has_value()) write_with_header(&this->command_auto_.value());
      else if(this->command_heat_.has_value() && this->command_cool_.has_value()) ESP_LOGW(TAG, "'%s' Auto mode not support.", this->device_name_->c_str());
      else if(this->command_heat_.has_value()) {
        write_with_header(&this->command_heat_.value());
        this->mode = climate::CLIMATE_MODE_HEAT;
      }
      else if(this->command_cool_.has_value()) {
        write_with_header(&this->command_cool_.value());
        this->mode = climate::CLIMATE_MODE_COOL;
      }
    } 
  }

  // Set target temperature
  if (call.get_target_temperature().has_value() && this->target_temperature != *call.get_target_temperature()) {
    this->target_temperature = *call.get_target_temperature();
    this->command_temperature_ = (this->command_temperature_func_)(this->target_temperature);
    write_with_header(&this->command_temperature_);
  }
  
  // Set away
  if(this->command_away_.has_value() && call.get_away().has_value() && this->away != *call.get_away()) {
    this->away = *call.get_away();
    if(this->away) write_with_header(&this->command_away_.value());
    else if(this->command_home_.has_value()) write_with_header(&this->command_home_.value());
    else if(this->mode == climate::CLIMATE_MODE_OFF) write_with_header(this->get_command_off());
    else if(this->mode == climate::CLIMATE_MODE_HEAT && this->command_heat_.has_value()) write_with_header(&this->command_heat_.value());
    else if(this->mode == climate::CLIMATE_MODE_COOL && this->command_cool_.has_value()) write_with_header(&this->command_cool_.value());
    else if(this->mode == climate::CLIMATE_MODE_AUTO && this->command_auto_.has_value()) write_with_header(&this->command_auto_.value());
  }

  this->publish_state();
}


}  // namespace rs485
}  // namespace esphome
