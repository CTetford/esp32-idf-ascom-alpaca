#include "DomeController.h"
#include "../config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cmath>

static const char *TAG = "DomeController";

// Helper to convert DomeState enum to string for logging
const char* DomeControl::domeStateToString(DomeControl::DomeState state) {
    switch (state) {
        case DomeState::IDLE: return "IDLE";
        case DomeState::HOMING_START: return "HOMING_START";
        case DomeState::HOMING_MOVING_TO_SWITCH: return "HOMING_MOVING_TO_SWITCH";
        case DomeState::HOMING_FOUND_SWITCH_STOPPING: return "HOMING_FOUND_SWITCH_STOPPING";
        case DomeState::HOMING_MOVING_OFF_SWITCH: return "HOMING_MOVING_OFF_SWITCH";
        case DomeState::HOMING_STOPPING_OFF_SWITCH: return "HOMING_STOPPING_OFF_SWITCH";
        case DomeState::HOMING_REAPPROACHING_SWITCH: return "HOMING_REAPPROACHING_SWITCH";
        case DomeState::HOMING_FINAL_STOP: return "HOMING_FINAL_STOP";
        case DomeState::HOMING_COMPLETE: return "HOMING_COMPLETE";
        case DomeState::SLEWING_TO_AZIMUTH: return "SLEWING_TO_AZIMUTH";
        case DomeState::SLEWING_TO_PARK: return "SLEWING_TO_PARK";
        case DomeState::PARKED: return "PARKED";
        case DomeState::ERROR_MOTOR: return "ERROR_MOTOR";
        case DomeState::ERROR_ENCODER: return "ERROR_ENCODER";
        case DomeState::ERROR_LIMIT_SWITCH: return "ERROR_LIMIT_SWITCH";
        case DomeState::ERROR_STALLED: return "ERROR_STALLED";
        case DomeState::ERROR_HOMING_TIMEOUT: return "ERROR_HOMING_TIMEOUT";
        default: return "UNKNOWN_STATE";
    }
}


namespace DomeControl {

// Define constants for homing, could be moved to config.h
const uint32_t HOMING_TIMEOUT_MS = 180000; // 3 minutes for overall homing
const uint32_t MOTOR_STOP_TIME_MS = 200;   // Time for motor to physically stop

DomeController::DomeController(HAL::Motor& motor, HAL::Encoder& encoder, HAL::LimitSwitch& home_switch) :
    _motor(motor),
    _encoder(encoder),
    _home_switch(home_switch),
    _currentState(DomeState::IDLE),
    _lastState(DomeState::IDLE),
    _current_azimuth_degrees(0.0f),
    _target_azimuth_degrees(0.0f),
    _home_azimuth(DOME_HOME_AZIMUTH),
    _park_azimuth(DOME_PARK_AZIMUTH),
    _current_encoder_pulses(0),
    _target_encoder_pulses(0),
    _home_encoder_offset_pulses(0),
    _pulses_per_degree( (ENCODER_TOTAL_PULSES_FOR_360_DOME_DEGREES) / 360.0f),
    _pid_kp(PID_KP), _pid_ki(PID_KI), _pid_kd(PID_KD),
    _pid_error_sum(0.0f),
    _pid_last_error(0.0f),
    _pid_last_time_ms(0),
    _homing_speed_percent(HOMING_SPEED),
    _homing_slow_speed_percent(HOMING_SPEED > 20 ? HOMING_SPEED / 2.0f : 10.0f), // Slower speed for precision
    _homing_direction_multiplier(HOMING_DIRECTION),
    _home_switch_active_low(HOME_LIMIT_SWITCH_ACTIVE_LOW),
    _homing_start_time_ms(0),
    _encoder_at_switch_actuation(0),
    _state_timer_ms(0),
    _is_homed(false)
{
    ESP_LOGI(TAG, "DomeController instance created.");
}

DomeController::~DomeController() {
    ESP_LOGI(TAG, "DomeController instance destroyed.");
    _motor.stop();
    _motor.disable();
}

esp_err_t DomeController::begin(float home_azimuth, float park_azimuth, float pulses_per_degree,
                                float pid_kp, float pid_ki, float pid_kd,
                                int homing_speed, int homing_direction, bool home_switch_active_low) {
    ESP_LOGI(TAG, "Initializing DomeController...");
    _home_azimuth = home_azimuth;
    _park_azimuth = park_azimuth;
    _pulses_per_degree = pulses_per_degree;
    _pid_kp = pid_kp;
    _pid_ki = pid_ki;
    _pid_kd = pid_kd;
    _homing_speed_percent = homing_speed;
    _homing_slow_speed_percent = homing_speed > 20 ? homing_speed / 2.0f : (homing_speed > 10 ? 10.0f : homing_speed);
    if (_homing_slow_speed_percent < 5) _homing_slow_speed_percent = 5; // Minimum slow speed
    _homing_direction_multiplier = homing_direction;
    _home_switch_active_low = home_switch_active_low;

    _is_homed = false;
    _encoder.resetPosition(); // Always reset encoder and homed state on begin
    _current_encoder_pulses = 0;
    _home_encoder_offset_pulses = 0;
    _current_azimuth_degrees = 0.0f;
    _target_azimuth_degrees = 0.0f;

    _motor.enable();
    _transitionState(DomeState::IDLE);

    ESP_LOGI(TAG, "DomeController initialized. Home Az: %.2f, Park Az: %.2f, PPD: %.2f",
             _home_azimuth, _park_azimuth, _pulses_per_degree);
    ESP_LOGI(TAG, "PID Kp:%.2f, Ki:%.2f, Kd:%.2f. HomingSpeed (Fast: %d%%, Slow: %.0f%%), HomingDir:%d",
             _pid_kp, _pid_ki, _pid_kd, _homing_speed_percent, _homing_slow_speed_percent, _homing_direction_multiplier);
    return ESP_OK;
}

void DomeController::_transitionState(DomeState new_state) {
    if (_currentState != new_state) {
        ESP_LOGI(TAG, "State transition: %s -> %s", domeStateToString(_currentState), domeStateToString(new_state));
        _currentState = new_state;
        _pid_last_time_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        _state_timer_ms = _pid_last_time_ms; // Reset state timer on each transition

        // Reset PID terms when exiting movement states or entering error/idle states
        if (new_state == DomeState::IDLE || new_state == DomeState::PARKED ||
            new_state == DomeState::HOMING_COMPLETE || new_state == DomeState::ERROR_MOTOR ||
            new_state == DomeState::ERROR_ENCODER || new_state == DomeState::ERROR_LIMIT_SWITCH ||
            new_state == DomeState::ERROR_STALLED || new_state == DomeState::ERROR_HOMING_TIMEOUT) {
            _pid_error_sum = 0.0f;
            _pid_last_error = 0.0f;
            if (new_state != DomeState::SLEWING_TO_AZIMUTH && new_state != DomeState::SLEWING_TO_PARK) {
                 // Don't stop motor if transitioning to another slewing state (not typical but safe)
                _motor.stop();
            }
        }
    }
}

void DomeController::update() {
    if (_encoder.getPosition(&_current_encoder_pulses) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read encoder!");
        _transitionState(DomeState::ERROR_ENCODER);
    }
    _updateCurrentAzimuth();

    switch (_currentState) {
        case DomeState::IDLE: _handleIdle(); break;
        case DomeState::HOMING_START: _handleHomingStart(); break;
        case DomeState::HOMING_MOVING_TO_SWITCH: _handleHomingMovingToSwitch(); break;
        case DomeState::HOMING_FOUND_SWITCH_STOPPING: _handleHomingFoundSwitchStopping(); break;
        case DomeState::HOMING_MOVING_OFF_SWITCH: _handleHomingMovingOffSwitch(); break;
        case DomeState::HOMING_STOPPING_OFF_SWITCH: _handleHomingStoppingOffSwitch(); break;
        case DomeState::HOMING_REAPPROACHING_SWITCH: _handleHomingReapproachingSwitch(); break;
        case DomeState::HOMING_FINAL_STOP: _handleHomingFinalStop(); break;
        case DomeState::HOMING_COMPLETE: _handleHomingComplete(); break;
        case DomeState::SLEWING_TO_AZIMUTH:
        case DomeState::SLEWING_TO_PARK: _handleSlewingToAzimuth(); break;
        case DomeState::PARKED: _handleParked(); break;
        case DomeState::ERROR_MOTOR:
        case DomeState::ERROR_ENCODER:
        case DomeState::ERROR_LIMIT_SWITCH:
        case DomeState::ERROR_STALLED:
        case DomeState::ERROR_HOMING_TIMEOUT: _handleError(); break;
        default:
            ESP_LOGW(TAG, "Unhandled state: %d", (int)_currentState);
            _transitionState(DomeState::IDLE);
            break;
    }
}

void DomeController::_updateCurrentAzimuth() {
    if (!_is_homed) {
        _current_azimuth_degrees = -1.0f; // Indicate unknown
        return;
    }
    int32_t relative_pulses = _current_encoder_pulses - _home_encoder_offset_pulses;
    _current_azimuth_degrees = _home_azimuth + ((float)relative_pulses / _pulses_per_degree);
    _current_azimuth_degrees = fmodf(_current_azimuth_degrees, 360.0f);
    if (_current_azimuth_degrees < 0) {
        _current_azimuth_degrees += 360.0f;
    }
}

int32_t DomeController::_azimuthToPulses(float target_azimuth) const {
    if (!_is_homed) {
        ESP_LOGW(TAG, "Cannot convert azimuth to pulses: Not homed.");
        return _current_encoder_pulses;
    }
    float current_effective_az = _current_azimuth_degrees;
    if (current_effective_az < 0) current_effective_az = _home_azimuth; // If not homed, assume current is home for delta calc

    float delta_azimuth = target_azimuth - current_effective_az;

    // Normalize delta_azimuth to find shortest path (-180 to +180)
    if (delta_azimuth > 180.0f) {
        delta_azimuth -= 360.0f;
    } else if (delta_azimuth < -180.0f) {
        delta_azimuth += 360.0f;
    }
    // Target pulses = current pulses + pulses for delta_azimuth
    return _current_encoder_pulses + (int32_t)(delta_azimuth * _pulses_per_degree);
}

void DomeController::findHome() {
    ESP_LOGI(TAG, "findHome() called.");
    if (_currentState == DomeState::IDLE || _currentState == DomeState::PARKED ||
        _currentState == DomeState::HOMING_COMPLETE || _currentState == DomeState::ERROR_ENCODER ||
        _currentState == DomeState::ERROR_LIMIT_SWITCH || _currentState == DomeState::ERROR_HOMING_TIMEOUT) {
        _is_homed = false;
        _transitionState(DomeState::HOMING_START);
    } else {
        ESP_LOGW(TAG, "Cannot start homing from state %s", domeStateToString(_currentState));
    }
}

void DomeController::slewToAzimuth(float target_azimuth) {
    ESP_LOGI(TAG, "slewToAzimuth(%.2f) called.", target_azimuth);
    if (!_is_homed) {
        ESP_LOGE(TAG, "Cannot slew to azimuth: Dome not homed yet.");
        return;
    }
    if (_currentState == DomeState::IDLE || _currentState == DomeState::PARKED || _currentState == DomeState::HOMING_COMPLETE) {
        _target_azimuth_degrees = fmodf(target_azimuth, 360.0f);
        if (_target_azimuth_degrees < 0) _target_azimuth_degrees += 360.0f;
        _target_encoder_pulses = _azimuthToPulses(_target_azimuth_degrees);
        ESP_LOGI(TAG, "Targeting Az: %.2f, Target Enc: %d (Current Enc: %d)",
                 _target_azimuth_degrees, (int)_target_encoder_pulses, (int)_current_encoder_pulses);
        float error_pulses = fabsf((float)(_target_encoder_pulses - _current_encoder_pulses));
        float tolerance_pulses = 0.1f * _pulses_per_degree;
        if (error_pulses < tolerance_pulses) {
            ESP_LOGI(TAG, "Already at target azimuth %.2f", _target_azimuth_degrees);
            _transitionState(DomeState::IDLE);
        } else {
            _transitionState(DomeState::SLEWING_TO_AZIMUTH);
        }
    } else {
        ESP_LOGW(TAG, "Cannot slew to azimuth from state %s", domeStateToString(_currentState));
    }
}

void DomeController::park() {
    ESP_LOGI(TAG, "park() called.");
     if (!_is_homed) {
        ESP_LOGE(TAG, "Cannot park: Dome not homed yet.");
        return;
    }
    if (_currentState == DomeState::IDLE || _currentState == DomeState::HOMING_COMPLETE || _currentState == DomeState::SLEWING_TO_AZIMUTH) {
        _target_azimuth_degrees = _park_azimuth;
        _target_encoder_pulses = _azimuthToPulses(_park_azimuth);
        ESP_LOGI(TAG, "Targeting Park Az: %.2f, Target Enc: %d", _target_azimuth_degrees, (int)_target_encoder_pulses);
        _transitionState(DomeState::SLEWING_TO_PARK);
    } else {
        ESP_LOGW(TAG, "Cannot park from state %s", domeStateToString(_currentState));
    }
}

void DomeController::setParkPosition() {
    ESP_LOGI(TAG, "setParkPosition() called.");
    if (!_is_homed) {
        ESP_LOGW(TAG, "Park position not set as dome is not homed.");
        return;
    }
    _park_azimuth = _current_azimuth_degrees;
    ESP_LOGI(TAG, "New park position set to current azimuth: %.2f degrees", _park_azimuth);
}

void DomeController::abortSlew() {
    ESP_LOGI(TAG, "abortSlew() called.");
    if (isSlewing() || isHomingActive()) {
        _motor.stop();
        _target_azimuth_degrees = _current_azimuth_degrees;
        _target_encoder_pulses = _current_encoder_pulses;
        _transitionState(DomeState::IDLE);
        ESP_LOGI(TAG, "Slew/Homing aborted. Motor stopped.");
    } else {
        ESP_LOGW(TAG, "No active slew/homing to abort from state %s", domeStateToString(_currentState));
    }
}

void DomeController::syncToAzimuth(float new_current_azimuth) {
    ESP_LOGI(TAG, "syncToAzimuth(%.2f) called.", new_current_azimuth);
    _current_azimuth_degrees = fmodf(new_current_azimuth, 360.0f);
    if (_current_azimuth_degrees < 0) _current_azimuth_degrees += 360.0f;

    if (_encoder.getPosition(&_current_encoder_pulses) != ESP_OK) { // Update current pulses
        ESP_LOGE(TAG, "Failed to read encoder for sync. Sync aborted.");
        _transitionState(DomeState::ERROR_ENCODER);
        return;
    }

    // New home offset: current encoder - pulses equivalent to (synced_azimuth - defined_home_azimuth)
    float delta_from_defined_home = _current_azimuth_degrees - _home_azimuth;
    // Normalize delta_from_defined_home to -180 to +180 range
    if (delta_from_defined_home > 180.0f) delta_from_defined_home -= 360.0f;
    else if (delta_from_defined_home < -180.0f) delta_from_defined_home += 360.0f;

    _home_encoder_offset_pulses = _current_encoder_pulses - (int32_t)(delta_from_defined_home * _pulses_per_degree);

    _is_homed = true;
    _target_azimuth_degrees = _current_azimuth_degrees;
    _target_encoder_pulses = _current_encoder_pulses;

    ESP_LOGI(TAG, "Dome synchronized. Current Az: %.2f, Current Enc: %d, New Home Offset Enc: %d",
             _current_azimuth_degrees, (int)_current_encoder_pulses, (int)_home_encoder_offset_pulses);

    if (_currentState != DomeState::IDLE) {
         _transitionState(DomeState::IDLE);
    }
}

float DomeController::getCurrentAzimuth() const { return _current_azimuth_degrees; } // Returns -1.0f if not homed
float DomeController::getTargetAzimuth() const { return _target_azimuth_degrees; }
bool DomeController::isAtHome() const { /* ... same as before ... */
    if (!_is_homed) return false;
    float diff = fabsf(_current_azimuth_degrees - _home_azimuth);
    if (diff > 180.0f) diff = 360.0f - diff;
    return diff < 0.5f;
}
bool DomeController::isParked() const { /* ... same as before ... */
    if (!_is_homed || (_currentState != DomeState::PARKED && _currentState != DomeState::IDLE)) return false;
    float diff = fabsf(_current_azimuth_degrees - _park_azimuth);
    if (diff > 180.0f) diff = 360.0f - diff;
    return diff < 0.5f;
}
bool DomeController::isSlewing() const { /* ... updated ... */
    return _currentState == DomeState::SLEWING_TO_AZIMUTH ||
           _currentState == DomeState::SLEWING_TO_PARK;
}
bool DomeController::isHomingActive() const { /* ... updated ... */
    return (_currentState >= DomeState::HOMING_START && _currentState < DomeState::HOMING_COMPLETE);
}
DomeState DomeController::getCurrentState() const { return _currentState; }
const char* DomeController::getCurrentStateStr() const { return domeStateToString(_currentState); }

void DomeController::_updateMotorSpeed(float speed_percent) {
    if (_motor.setSpeed(speed_percent) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set motor speed!");
        _transitionState(DomeState::ERROR_MOTOR);
    }
}

void DomeController::_performPID() {
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    float dt_seconds = (now_ms - _pid_last_time_ms) / 1000.0f;

    if (dt_seconds <= 0.001f) return;
    if (dt_seconds > 1.0f) {
        ESP_LOGW(TAG, "PID dt too large (%.2fs), limiting to 1.0s. Resetting integral.", dt_seconds);
        dt_seconds = 1.0f;
        _pid_error_sum = 0; // Reset integral on large gap
    }
    _pid_last_time_ms = now_ms;

    float error = (float)(_target_encoder_pulses - _current_encoder_pulses);
    float p_term = _pid_kp * error;
    _pid_error_sum += error * dt_seconds;

    // Basic anti-windup: clamp integral sum
    float max_integral_contribution = fabsf(PID_OUTPUT_MAX / 2.0f); // Allow I-term to contribute up to 50% of max output
    if (_pid_ki != 0) { // Avoid division by zero if Ki is zero
        float max_error_sum_val = max_integral_contribution / fabsf(_pid_ki);
        if (_pid_error_sum > max_error_sum_val) _pid_error_sum = max_error_sum_val;
        else if (_pid_error_sum < -max_error_sum_val) _pid_error_sum = -max_error_sum_val;
    }
    float i_term = _pid_ki * _pid_error_sum;

    float error_delta = (error - _pid_last_error) / dt_seconds;
    _pid_last_error = error;
    float d_term = _pid_kd * error_delta;

    float output = p_term + i_term + d_term;

    if (output > PID_OUTPUT_MAX) output = PID_OUTPUT_MAX;
    else if (output < PID_OUTPUT_MIN) output = PID_OUTPUT_MIN;

    ESP_LOGV(TAG, "PID: Err:%6.1f, P:%.2f, I:%.2f(Sum:%.1f), D:%.2f -> Out:%.2f | dt:%.3fs",
            error, p_term, i_term, _pid_error_sum, d_term, output, dt_seconds);
    _updateMotorSpeed(output);
}

// --- State Handlers ---
void DomeController::_handleIdle() { _motor.stop(); } // Ensure motor is stopped
void DomeController::_handleParked() { _motor.stop(); } // Ensure motor is stopped

void DomeController::_handleHomingStart() {
    ESP_LOGI(TAG, "Homing sequence started.");
    _encoder.resetPosition(); // Reset encoder and our software overflow count
    _current_encoder_pulses = 0;
    _home_encoder_offset_pulses = 0; // Will be set accurately at end of homing
    _is_homed = false;
    _homing_start_time_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    _transitionState(DomeState::HOMING_MOVING_TO_SWITCH);
}

void DomeController::_handleHomingMovingToSwitch() {
    _updateMotorSpeed(_homing_speed_percent * _homing_direction_multiplier);
    if (_home_switch.isActive()) {
        ESP_LOGI(TAG, "Home switch activated (initial pass).");
        _motor.stop();
        if (_encoder.getPosition(&_encoder_at_switch_actuation) != ESP_OK) { // Store rough position
             _transitionState(DomeState::ERROR_ENCODER); return;
        }
        _transitionState(DomeState::HOMING_FOUND_SWITCH_STOPPING);
    } else if ((uint32_t)(esp_timer_get_time() / 1000ULL) - _homing_start_time_ms > HOMING_TIMEOUT_MS) {
        ESP_LOGE(TAG, "Homing timeout: Limit switch not found.");
        _motor.stop();
        _transitionState(DomeState::ERROR_HOMING_TIMEOUT);
    }
}

void DomeController::_handleHomingFoundSwitchStopping() {
    _motor.stop(); // Ensure stopped
    if ((uint32_t)(esp_timer_get_time() / 1000ULL) - _state_timer_ms > MOTOR_STOP_TIME_MS) {
        _transitionState(DomeState::HOMING_MOVING_OFF_SWITCH);
    }
}

void DomeController::_handleHomingMovingOffSwitch() {
    _updateMotorSpeed(_homing_slow_speed_percent * -_homing_direction_multiplier); // Opposite direction, slowly
    if (!_home_switch.isActive()) {
        ESP_LOGI(TAG, "Moved off home switch.");
        _motor.stop();
        _transitionState(DomeState::HOMING_STOPPING_OFF_SWITCH);
    } else if ((uint32_t)(esp_timer_get_time() / 1000ULL) - _homing_start_time_ms > HOMING_TIMEOUT_MS) {
        ESP_LOGE(TAG, "Homing timeout: Could not move off switch.");
        _motor.stop();
        _transitionState(DomeState::ERROR_HOMING_TIMEOUT);
    }
}

void DomeController::_handleHomingStoppingOffSwitch() {
    _motor.stop();
    if ((uint32_t)(esp_timer_get_time() / 1000ULL) - _state_timer_ms > MOTOR_STOP_TIME_MS) {
        _transitionState(DomeState::HOMING_REAPPROACHING_SWITCH);
    }
}

void DomeController::_handleHomingReapproachingSwitch() {
    _updateMotorSpeed(_homing_slow_speed_percent * _homing_direction_multiplier); // Original direction, slowly
    if (_home_switch.isActive()) {
        ESP_LOGI(TAG, "Home switch re-activated (final pass).");
        _motor.stop();
        if (_encoder.getPosition(&_encoder_at_switch_actuation) != ESP_OK) { // Get precise position
             _transitionState(DomeState::ERROR_ENCODER); return;
        }
        ESP_LOGI(TAG, "Precise encoder at switch: %d", (int)_encoder_at_switch_actuation);
        _transitionState(DomeState::HOMING_FINAL_STOP);
    } else if ((uint32_t)(esp_timer_get_time() / 1000ULL) - _homing_start_time_ms > HOMING_TIMEOUT_MS) {
        ESP_LOGE(TAG, "Homing timeout: Could not re-approach switch.");
        _motor.stop();
        _transitionState(DomeState::ERROR_HOMING_TIMEOUT);
    }
}

void DomeController::_handleHomingFinalStop() {
    _motor.stop();
    if ((uint32_t)(esp_timer_get_time() / 1000ULL) - _state_timer_ms > MOTOR_STOP_TIME_MS) {
        _home_encoder_offset_pulses = _encoder_at_switch_actuation;
        _current_encoder_pulses = _encoder_at_switch_actuation; // Current position is now the switch position
        _is_homed = true;
        _updateCurrentAzimuth(); // Calculate current azimuth based on new home offset (should be _home_azimuth)
        _target_azimuth_degrees = _current_azimuth_degrees;
        _target_encoder_pulses = _current_encoder_pulses;
        _transitionState(DomeState::HOMING_COMPLETE);
    }
}

void DomeController::_handleHomingComplete() {
    ESP_LOGI(TAG, "Homing sequence complete. Dome is at home azimuth: %.2f, Encoder Offset: %d",
             _current_azimuth_degrees, (int)_home_encoder_offset_pulses);
    _motor.stop();
    _transitionState(DomeState::IDLE);
}

void DomeController::_handleSlewingToAzimuth() {
    float error_pulses = (float)(_target_encoder_pulses - _current_encoder_pulses);
    float tolerance_pulses = 0.2f * _pulses_per_degree; // 0.2 degree tolerance

    if (fabsf(error_pulses) < tolerance_pulses) {
        ESP_LOGI(TAG, "Target azimuth %.2f reached. Error: %.2f pulses (Tol: %.2f).",
                 _target_azimuth_degrees, error_pulses, tolerance_pulses);
        _motor.stop();
        _current_azimuth_degrees = _target_azimuth_degrees;
        _current_encoder_pulses = _target_encoder_pulses;
        if (_currentState == DomeState::SLEWING_TO_PARK) {
            _transitionState(DomeState::PARKED);
        } else {
            _transitionState(DomeState::IDLE);
        }
    } else {
        _performPID();
        // TODO: Add timeout for slew / stall detection (motor on, encoder not changing)
    }
}

void DomeController::_handleError() {
    _motor.stop();
    ESP_LOGE(TAG, "DomeController in error state: %s. Motor stopped.", domeStateToString(_currentState));
}

} // namespace DomeControl
