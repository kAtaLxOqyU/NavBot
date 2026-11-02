// podbot_aim.h - PODBot-style spring-damper aim controller (SDK independent).
//
// Interpretation of the pb_aim_* values (tune in game):
//   each axis is a second-order system:  acc = w^2 * error - 2 * zeta * w * velocity
//   w    = pb_aim_spring_stiffness_*   (rad/s: speed and oscillation frequency)
//   zeta = pb_aim_damper_coefficient_* (< 1 overshoots like a human flick)
//   x axis = vertical (pitch), y axis = horizontal (yaw), as in podbot.cfg.
#pragma once
#include "podbot_data.h"

namespace podbot {

struct Angles { float pitch = 0.0f, yaw = 0.0f; };   // degrees

struct AimParams {
	int   type = 4;                    // pb_aim_type: 1 snap, 2 constant velocity, 3/4 spring
	float stiffX = 13.0f, stiffY = 13.0f;
	float dampX = 0.22f,  dampY = 0.22f;
	float dampScale = 1.0f;                    // pb_aim_damping_scale (CS:S edition): multiplies both damper values
	float devX = 2.0f,    devY = 1.0f;          // idle error margin (game units)
	float infXonY = 0.25f, infYonX = 0.17f;     // cross-axis disturbance
	float offsetDelay = 1.2f;                   // seconds between new idle offsets
	float noTargetSlowdown = 0.5f;              // speed ratio when aiming at nothing
	float anticipation = 2.2f;                  // pb_aim_target_anticipation_ratio
	static AimParams FromSettings(const Settings& s);
};

float WrapDeg(float a);   // to [-180, 180]

// Predicted aim point: position + velocity * anticipation * leadTime.
void Anticipate(const float pos[3], const float vel[3], float anticipation, float leadTime, float out[3]);

// Random aim-point offset (units) for a skill bracket (AIM_OFFS_X/Y/Z), zero at skill 100.
void SkillAimOffset(const SkillBracket& b, Rng& rng, float out[3]);

class SpringAim {
public:
	explicit SpringAim(const AimParams& p = AimParams(), unsigned seed = 1) : m_p(p), m_rng(seed) {}
	void Reset(const Angles& current) { m_cur = current; m_velP = m_velY = 0.0f; m_timer = 0.0f; m_devP = m_devY = 0.0f; }
	// desired: ideal view angles. maxTurn: angular speed cap in deg/s (0 = none).
	// distance: units to the aim point (turns the idle error margin into an angle).
	Angles Update(const Angles& desired, bool hasTarget, float maxTurn, float distance, float dt);
	// Follows the real view angles (recoil, snaps) while keeping the angular velocity.
	void SyncCurrent(const Angles& a) { m_cur = a; }
	const Angles& Current() const { return m_cur; }
	float VelocityPitch() const { return m_velP; }
	float VelocityYaw() const { return m_velY; }
private:
	AimParams m_p;
	Rng m_rng;
	Angles m_cur;
	float m_velP = 0.0f, m_velY = 0.0f;
	float m_timer = 0.0f, m_devP = 0.0f, m_devY = 0.0f;
};

} // namespace podbot
