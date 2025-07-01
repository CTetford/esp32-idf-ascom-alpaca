#pragma once
#ifndef DOME_CONTROLLER_H
#define DOME_CONTROLLER_H

#include "../hal/Motor.h"
#include "../hal/Encoder.h"
#include "../hal/LimitSwitch.h"
#include "esp_err.h"
#include <cstdint>

namespace DomeControl {

enum class DomeState {
    IDLE,
    // Homing States
    HOMING_START,
    HOMING_MOVING_TO_SWITCH,      // Initial fast move towards switch
    HOMING_FOUND_SWITCH_STOPPING, // Stop motor after first detection
    HOMING_MOVING_OFF_SWITCH,     // Slowly move backward off the switch
    HOMING_STOPPING_OFF_SWITCH,   // Stop motor once off the switch
    HOMING_REAPPROACHING_SWITCH,  // Slowly move forward onto the switch again
    HOMING_FINAL_STOP,            // Stop motor once switch is re-activated precisely
    HOMING_COMPLETE,              // Homing finished, position set

    // Slewing States
    SLEWING_TO_AZIMUTH,
    SLEWING_TO_PARK,
    PARKED,

    // Error States
    ERROR_MOTOR,
    ERROR_ENCODER,
    ERROR_LIMIT_SWITCH,
    ERROR_STALLED, // If movement detected but encoder not changing
    ERROR_HOMING_TIMEOUT
};

const char* domeStateToString(DomeState state);

class DomeController {
public:
    DomeController(HAL::Motor& motor, HAL::Encoder& encoder, HAL::LimitSwitch& home_switch);
    ~DomeController();

    /**
     * @brief Initializes the Dome Controller.
     * @param home_azimuth The defined azimuth (degrees) for the home position.
     * @param park_azimuth The defined azimuth (degrees) for the park position.
     * @param pulses_per_degree Number of encoder pulses per degree of dome rotation.
     * @param pid_kp Proportional gain for PID.
     * @param pid_ki Integral gain for PID.
     * @param pid_kd Derivative gain for PID.
     * @param homing_speed Speed (percentage) for homing.
     * @param homing_direction Direction for homing (1 for normal, -1 for reverse from motor perspective).
     * @param home_switch_active_low True if home switch is active low.
     * @return ESP_OK on success.
     */
    esp_err_t begin(float home_azimuth, float park_azimuth, float pulses_per_degree,
                    float pid_kp, float pid_ki, float pid_kd,
                    int homing_speed, int homing_direction, bool home_switch_active_low);

    /**
     * @brief Main update loop for the dome controller. Call this regularly.
     * Handles state machine, PID updates, etc.
     */
    void update();

    /**
     * @brief Initiates the homing sequence to find the dome's home position.
     */
    void findHome();

    /**
     * @brief Slews the dome to the target azimuth.
     * @param target_azimuth Target azimuth in degrees.
     */
    void slewToAzimuth(float target_azimuth);

    /**
     * @brief Slews the dome to the predefined park position.
     */
    void park();

    /**
     * @brief Sets the current dome position as the park position.
     * This would typically save the current azimuth to NVS or a variable.
     * For now, it might just update the internal target park azimuth.
     */
    void setParkPosition();

    /**
     * @brief Aborts any ongoing slew or homing operation immediately.
     */
    void abortSlew();

    /**
     * @brief Synchronizes the dome's current position to a specified azimuth.
     * This is useful if the dome's physical position is known and needs to be set in the controller.
     * @param current_azimuth The actual current azimuth of the dome in degrees.
     */
    void syncToAzimuth(float current_azimuth);


    // Getter methods
    float getCurrentAzimuth() const;
    float getTargetAzimuth() const;
    bool isAtHome() const;
    bool isParked() const;
    bool isSlewing() const; // True if homing, slewing to azimuth, or slewing to park
    bool isHomingActive() const;
    DomeState getCurrentState() const;
    const char* getCurrentStateStr() const;


private:
    HAL::Motor& _motor;
    HAL::Encoder& _encoder;
    HAL::LimitSwitch& _home_switch;

    DomeState _currentState;
    DomeState _lastState; // For logging state changes

    float _current_azimuth_degrees;
    float _target_azimuth_degrees;
    float _home_azimuth;
    float _park_azimuth; // Configurable park position

    int32_t _current_encoder_pulses; // Changed from int16_t to int32_t
    int32_t _target_encoder_pulses;
    int32_t _home_encoder_offset_pulses; // Encoder count when at home_azimuth

    float _pulses_per_degree;

    // PID Controller variables
    float _pid_kp;
    float _pid_ki;
    float _pid_kd;
    float _pid_error_sum;
    float _pid_last_error;
    uint32_t _pid_last_time_ms;

    // Homing variables
    int _homing_speed_percent;
    int _homing_direction_multiplier; // 1 or -1 from motor's perspective
    bool _home_switch_active_low;
    uint32_t _homing_start_time_ms;
    int32_t _encoder_at_switch_actuation; // Changed to int32_t
    uint32_t _state_timer_ms; // Generic timer for states that need it (e.g. stopping states)


    // Internal methods
    void _updateMotorSpeed(float speed_percent);
    void _performPID();
    void _transitionState(DomeState new_state);
    void _updateCurrentAzimuth(); // Reads encoder and updates _current_azimuth_degrees
    int32_t _azimuthToPulses(float target_azimuth) const; // Converts target azimuth to target encoder pulses
    // float _pulsesToAzimuth(int32_t pulses) const; // Not strictly needed if _updateCurrentAzimuth does the job

    // State handlers
    void _handleIdle();
    // Homing state handlers
    void _handleHomingStart();
    void _handleHomingMovingToSwitch();
    void _handleHomingFoundSwitchStopping();
    void _handleHomingMovingOffSwitch();
    void _handleHomingStoppingOffSwitch();
    void _handleHomingReapproachingSwitch();
    void _handleHomingFinalStop();
    void _handleHomingComplete();
    // Slewing and Parked state handlers
    void _handleSlewingToAzimuth(); // Also used for slewing to park
    void _handleParked();
    // Error state handler
    void _handleError();

    bool _is_homed; // Flag indicating if homing has been successfully completed at least once
    float _homing_slow_speed_percent; // Speed for fine movements during homing
};

} // namespace DomeControl

#endif // DOME_CONTROLLER_H
