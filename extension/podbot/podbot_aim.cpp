#include "podbot_aim.h"

#include <algorithm>
#include <cmath>

namespace podbot {

namespace {
constexpr float kRadToDeg = 57.29577951f;
float Clamp(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }
}

AimParams AimParams::FromSettings(const Settings& s)
{
	AimParams p;
	p.type = s.GetInt("pb_aim_type", p.type);
	p.stiffX = s.GetFloat("pb_aim_spring_stiffness_x", p.stiffX);
	p.stiffY = s.GetFloat("pb_aim_spring_stiffness_y", p.stiffY);
	p.dampX = s.GetFloat("pb_aim_damper_coefficient_x", p.dampX);
	p.dampY = s.GetFloat("pb_aim_damper_coefficient_y", p.dampY);
	p.dampScale = s.GetFloat("pb_aim_damping_scale", p.dampScale);
	p.devX = s.GetFloat("pb_aim_deviation_x", p.devX);
	p.devY = s.GetFloat("pb_aim_deviation_y", p.devY);
	p.infXonY = s.GetFloat("pb_aim_influence_x_on_y", p.infXonY);
	p.infYonX = s.GetFloat("pb_aim_influence_y_on_x", p.infYonX);
	p.offsetDelay = s.GetFloat("pb_aim_offset_delay", p.offsetDelay);
	p.noTargetSlowdown = s.GetFloat("pb_aim_notarget_slowdown_ratio", p.noTargetSlowdown);
	p.anticipation = s.GetFloat("pb_aim_target_anticipation_ratio", p.anticipation);
	return p;
}

float WrapDeg(float a)
{
	a = std::fmod(a + 180.0f, 360.0f);
	if (a < 0.0f) a += 360.0f;
	return a - 180.0f;
}

void Anticipate(const float pos[3], const float vel[3], float anticipation, float leadTime, float out[3])
{
	for (int i = 0; i < 3; ++i) out[i] = pos[i] + vel[i] * anticipation * leadTime;
}

void SkillAimOffset(const SkillBracket& b, Rng& rng, float out[3])
{
	out[0] = rng.Float(-b.offX, b.offX);
	out[1] = rng.Float(-b.offY, b.offY);
	out[2] = rng.Float(-b.offZ, b.offZ);
}

Angles SpringAim::Update(const Angles& desired, bool hasTarget, float maxTurn, float distance, float dt)
{
	if (!(dt > 0.0f)) return m_cur;
	dt = std::min(dt, 0.1f);

	if (m_p.type <= 1) {                      // "inhuman turns": snap to the target
		m_cur = desired; m_cur.pitch = Clamp(m_cur.pitch, -89.0f, 89.0f); m_cur.yaw = WrapDeg(m_cur.yaw);
		m_velP = m_velY = 0.0f;
		return m_cur;
	}

	if (m_p.type == 2) {                      // constant angular velocity
		float step = (maxTurn > 0.0f ? maxTurn : 720.0f) * dt;
		float eP = desired.pitch - m_cur.pitch, eY = WrapDeg(desired.yaw - m_cur.yaw);
		m_cur.pitch += Clamp(eP, -step, step);
		m_cur.yaw = WrapDeg(m_cur.yaw + Clamp(eY, -step, step));
		m_velP = m_velY = 0.0f;
		return m_cur;
	}

	// spring-damper (types 3 and 4 share one model for now)
	m_timer -= dt;
	if (m_timer <= 0.0f) {                    // pick a new idle error offset (units)
		m_devP = m_rng.Float(-m_p.devX, m_p.devX);
		m_devY = m_rng.Float(-m_p.devY, m_p.devY);
		m_timer = m_rng.Float(0.2f * m_p.offsetDelay, m_p.offsetDelay);
	}
	Angles target = desired;
	if (!hasTarget && distance > 1.0f) {      // the error margin only applies when aiming at nothing
		target.pitch += std::atan2(m_devP, distance) * kRadToDeg;
		target.yaw += std::atan2(m_devY, distance) * kRadToDeg;
	}

	const float scale = hasTarget ? 1.0f : m_p.noTargetSlowdown;
	const float dX = m_p.dampX * m_p.dampScale, dY = m_p.dampY * m_p.dampScale;
	const float wP = m_p.stiffX * scale, wY = m_p.stiffY * scale;
	const int steps = std::max(1, static_cast<int>(std::ceil(dt * 120.0f)));
	const float h = dt / static_cast<float>(steps);

	for (int i = 0; i < steps; ++i) {
		float eP = target.pitch - m_cur.pitch;
		float eY = WrapDeg(target.yaw - m_cur.yaw);
		float aP = wP * wP * eP - 2.0f * dX * wP * m_velP;
		float aY = wY * wY * eY - 2.0f * dY * wY * m_velY;
		float aP2 = aP + m_p.infYonX * aY;     // yaw motion disturbs pitch
		float aY2 = aY + m_p.infXonY * aP;     // pitch motion disturbs yaw
		m_velP += aP2 * h;
		m_velY += aY2 * h;
		if (maxTurn > 0.0f) { m_velP = Clamp(m_velP, -maxTurn, maxTurn); m_velY = Clamp(m_velY, -maxTurn, maxTurn); }
		m_cur.pitch += m_velP * h;
		m_cur.yaw += m_velY * h;
	}
	m_cur.pitch = Clamp(m_cur.pitch, -89.0f, 89.0f);
	m_cur.yaw = WrapDeg(m_cur.yaw);
	return m_cur;
}

} // namespace podbot
