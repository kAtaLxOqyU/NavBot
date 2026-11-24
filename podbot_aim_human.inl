// podbot_aim_human.inl - human aim layer on top of the PODBot spring (aim v2).
//
// What it adds to the plain spring-damper in podbot_aim.cpp (all numbers measured from the demos, HANDOFF section 8 and the
// apEX / SmithZz / batham flick analysis, 20 players, about 14,000 flicks):
//   1. Personality     every bot gets its own speed multiplier (log-normal, sd 0.16 = the spread between the 20 measured players:
//                      ScreaM fast ... apEX slow), so no two bots turn alike.
//   2. Flick variation each flick draws its own damping and speed. Most flicks land with no overshoot, about 1 in 5 overshoots
//                      6-15 percent (measured: median overshoot 0, p90 6-10 percent). A plain spring overshoots every flick the same.
//   3. Reaction onset  when an enemy appears and the view must turn more than 15 degrees, the view holds still for about 70 ms
//                      (log-normal) before it starts moving, like a human's perception-to-hand delay.
//   4. Hold tremor     while on a target the aim drifts a little (slow Ornstein-Uhlenbeck noise, 0.15 degrees), like a hand on a mouse.
//   5. Axes            pitch and yaw have their own stiffness / damping (vertical flicks measured 25-30 percent slower).
//
// Part 1 = pure logic (no engine types; unit-tested with g++ -DPB_AIMH_PURE_ONLY).
// Part 2 = IAimOverride wrapper, created per bot by CPodBotManager instead of PodAimOverride when pb_aimh 1.
#pragma once
#include <algorithm>
#include <cmath>
#include <random>

namespace podbot { namespace aimh {

constexpr float kRad2Deg = 57.29577951f;
constexpr float kDeg2Rad = 0.01745329252f;

struct Ang { float pitch = 0.0f, yaw = 0.0f; };   // degrees

inline float Wrap(float a) { while (a > 180.0f) { a -= 360.0f; } while (a < -180.0f) { a += 360.0f; } return a; }
inline float Clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

struct Cfg
{
	// spring, same meaning as the pb_aim_* settings (x = pitch, y = yaw)
	float stiffX = 10.0f, stiffY = 14.0f;
	float dampX = 0.75f, dampY = 0.73f;
	float dampScale = 1.0f;
	float infXonY = 0.25f, infYonX = 0.17f;
	float devX = 2.0f, devY = 1.0f, offsetDelay = 1.2f;   // idle error margin (units) when aiming at nothing
	float noTargetSlowdown = 0.5f;
	float maxTurn = 0.0f;                                  // deg/s cap, 0 = none
	float levelDown = 4.0f;                                // relaxed looks (walking, idle): view never goes more than this many degrees below level
	float levelUp = 8.0f;                                  // ... or this many degrees above level
	// human layer
	float overshootChance = 0.20f;                         // share of flicks that overshoot
	float onsetMs = 70.0f;                                 // median reaction hold before a big flick toward an enemy
	float tremorDeg = 0.15f;                               // hold tremor, degrees (sd)
	float flickJitter = 0.08f;                             // per-flick speed variation (log-normal sd)
};

struct FlickInfo { bool fresh = false; float size = 0.0f, onsetMs = 0.0f, stiffMul = 1.0f, dampMul = 1.0f; };

class HumanSpring
{
public:
	explicit HumanSpring(unsigned seed, float archetypeSd = 0.16f) : m_rng(seed)
	{
		m_speed = Clampf(std::exp(archetypeSd * Normal()), 0.7f, 1.4f);
	}

	float Speed() const { return m_speed; }
	bool TakeFlick(FlickInfo& out) { if (!m_info.fresh) { return false; } out = m_info; m_info.fresh = false; return true; }

	// current: the bot's real eye angles (without the weapon kick). desired: ideal angles. Returns the new view angles.
	// exact : the look needs the exact angle (anything above an idle or path look); false = relaxed look: slower, with an idle error margin.
	// enemy : the bot is looking at an enemy (reaction onset and hold tremor apply).
	Ang Update(const Ang& current, const Ang& desired, bool exact, bool enemy, float distance, float dt, float now, const Cfg& c)
	{
		if (!(dt > 0.0f)) { return current; }
		dt = std::min(dt, 0.1f);
		m_cur = current;

		// idle error margin, only when aiming at nothing in particular (same as the plain spring)
		m_devTimer -= dt;
		if (m_devTimer <= 0.0f)
		{
			m_devP = RandRange(-c.devX, c.devX);
			m_devY = RandRange(-c.devY, c.devY);
			m_devTimer = RandRange(0.2f * c.offsetDelay, c.offsetDelay);
		}

		Ang target = desired;
		// Relaxed looks (idle and path) keep the view near level. NavBot's path look aims at the ground ahead, which made the
		// bots stare at the floor while walking. Every other look needs the exact angle and is left alone. (Pitch: positive = down.)
		if (!exact) { target.pitch = Clampf(target.pitch, -c.levelUp, c.levelDown); }
		if (!exact && distance > 1.0f)
		{
			target.pitch += std::atan2(m_devP, distance) * kRad2Deg;
			target.yaw += std::atan2(m_devY, distance) * kRad2Deg;
		}

		// hold tremor: slow correlated noise while aiming at a target (tau 0.4 s), fades out otherwise
		{
			const float k = std::exp(-dt / 0.4f);
			const float s = c.tremorDeg * std::sqrt(std::max(0.0f, 1.0f - k * k));
			if (enemy && c.tremorDeg > 0.0f) { m_trP = m_trP * k + s * 0.7f * Normal(); m_trY = m_trY * k + s * Normal(); }
			else { m_trP *= k; m_trY *= k; }
			target.pitch += m_trP;
			target.yaw += m_trY;
		}

		const float eP0 = target.pitch - m_cur.pitch;
		const float eY0 = Wrap(target.yaw - m_cur.yaw);
		const float err = std::sqrt((eY0 * std::cos(m_cur.pitch * kDeg2Rad)) * (eY0 * std::cos(m_cur.pitch * kDeg2Rad)) + eP0 * eP0);
		const float vel = std::sqrt(m_velP * m_velP + m_velY * m_velY);

		if (m_state == State::Settled)
		{
			if (err > 5.0f) { BeginFlick(err, enemy, now, c); }
		}
		else if (m_state == State::Moving)
		{
			if (err < 1.2f && vel < 12.0f) { m_state = State::Settled; }
		}

		if (m_state == State::Onset)
		{
			// perception to hand delay: the view stays where it is
			m_onsetLeft -= dt;
			m_velP = m_velY = 0.0f;
			if (m_onsetLeft <= 0.0f) { m_state = State::Moving; }
			return m_cur;
		}

		const bool flicking = (m_state == State::Moving);
		const float sMul = flicking ? m_stiffMul : 1.0f;
		const float dMul = flicking ? m_dampMul : 1.0f;
		const float scale = exact ? 1.0f : c.noTargetSlowdown;
		const float wP = c.stiffX * m_speed * sMul * scale;
		const float wY = c.stiffY * m_speed * sMul * scale;
		const float dP = c.dampX * c.dampScale * dMul;
		const float dY = c.dampY * c.dampScale * dMul;
		const float cap = (c.maxTurn > 0.0f) ? c.maxTurn * m_speed : 0.0f;

		const int steps = std::max(1, static_cast<int>(std::ceil(dt * 120.0f)));
		const float h = dt / static_cast<float>(steps);

		for (int i = 0; i < steps; ++i)
		{
			const float eP = target.pitch - m_cur.pitch;
			const float eY = Wrap(target.yaw - m_cur.yaw);
			const float aP = wP * wP * eP - 2.0f * dP * wP * m_velP;
			const float aY = wY * wY * eY - 2.0f * dY * wY * m_velY;
			m_velP += (aP + c.infYonX * aY) * h;      // yaw motion disturbs pitch
			m_velY += (aY + c.infXonY * aP) * h;      // pitch motion disturbs yaw
			if (cap > 0.0f) { m_velP = Clampf(m_velP, -cap, cap); m_velY = Clampf(m_velY, -cap, cap); }
			m_cur.pitch += m_velP * h;
			m_cur.yaw += m_velY * h;
		}

		m_cur.pitch = Clampf(m_cur.pitch, -89.0f, 89.0f);
		m_cur.yaw = Wrap(m_cur.yaw);
		return m_cur;
	}

private:
	enum class State { Settled, Onset, Moving };

	float Rand01() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(m_rng); }
	float RandRange(float a, float b) { return a + (b - a) * Rand01(); }
	float Normal() { return std::normal_distribution<float>(0.0f, 1.0f)(m_rng); }

	void BeginFlick(float size, bool enemy, float now, const Cfg& c)
	{
		const bool over = Rand01() < c.overshootChance;
		m_dampMul = over ? RandRange(0.68f, 0.90f) : RandRange(1.15f, 1.40f);
		m_stiffMul = std::exp(c.flickJitter * Normal()) * (over ? kOverStiff : kCleanStiff);

		m_onsetLeft = 0.0f;
		if (enemy && c.onsetMs > 0.0f && size > 15.0f && now - m_lastOnset > 0.5f)
		{
			m_onsetLeft = Clampf(c.onsetMs * 0.001f * std::exp(0.35f * Normal()), 0.02f, 0.4f);
			m_lastOnset = now;
		}

		m_state = (m_onsetLeft > 0.0f) ? State::Onset : State::Moving;
		m_info.fresh = true; m_info.size = size; m_info.onsetMs = m_onsetLeft * 1000.0f; m_info.stiffMul = m_stiffMul; m_info.dampMul = m_dampMul;
	}

	// speed compensation so clean (more damped) flicks keep the measured timing
	static constexpr float kCleanStiff = 1.15f;
	static constexpr float kOverStiff = 1.02f;

	std::mt19937 m_rng;
	Ang m_cur;
	State m_state = State::Settled;
	FlickInfo m_info;
	float m_speed = 1.0f;
	float m_velP = 0.0f, m_velY = 0.0f;
	float m_devP = 0.0f, m_devY = 0.0f, m_devTimer = 0.0f;
	float m_trP = 0.0f, m_trY = 0.0f;
	float m_stiffMul = 1.0f, m_dampMul = 1.0f;
	float m_onsetLeft = 0.0f, m_lastOnset = -10.0f;
};

// ===================================================================================================================
// PART 2 - ENGINE (IAimOverride wrapper)
// ===================================================================================================================
#ifndef PB_AIMH_PURE_ONLY

// Settings (all live-tunable, re-read once a second):
//   pb_aimh 1               use this aim instead of the plain spring (read when a bot is created)
//   pb_aimh_archetype 0.16  spread of bot personalities (0 = every bot identical)
//   pb_aimh_overshoot_chance 0.2, pb_aimh_onset_ms 70, pb_aimh_tremor 0.15, pb_aimh_jitter 0.08
//   pb_aimh_level_down 4, pb_aimh_level_up 8   how far a relaxed (walking) look may leave level, degrees
//   pb_aimh_debug 0         1 = print every flick of 20 degrees or more to the server console
// plus the existing pb_aim_* spring settings (pb_aim_type 1 and 2 still use NavBot's own turn).
class HumanAimOverride final : public IAimOverride
{
public:
	HumanAimOverride(const AimParams& params, const SkillBracket& bracket, unsigned seed) :
		m_bracket(bracket),
		m_rng(seed * 7u + 1u),
		m_spring(seed * 13u + 5u, human::detail::Get("pb_aimh_archetype", 0.16f))
	{
		m_cfg.stiffX = params.stiffX; m_cfg.stiffY = params.stiffY;
		m_cfg.dampX = params.dampX; m_cfg.dampY = params.dampY; m_cfg.dampScale = params.dampScale;
		m_cfg.infXonY = params.infXonY; m_cfg.infYonX = params.infYonX;
		m_cfg.devX = params.devX; m_cfg.devY = params.devY; m_cfg.offsetDelay = params.offsetDelay;
		m_cfg.noTargetSlowdown = params.noTargetSlowdown;
		// botskill.cfg gives degrees per GoldSrc frame; about 60 frames per second
		m_cfg.maxTurn = m_rng.Float(bracket.minTurn, bracket.maxTurn) * 60.0f;
		m_type = params.type;
		m_offsetDelay = params.offsetDelay;
		RefreshCfg(0.0f);
	}

	Vector AdjustTarget(CBaseBot* bot, const Vector& target, CBaseEntity* entity) override
	{
		if (entity == nullptr)
		{
			return target;
		}

		if (gpGlobals->curtime >= m_nextOffsetTime)
		{
			float o[3];
			SkillAimOffset(m_bracket, m_rng, o);
			m_offset = Vector(o[0], o[1], o[2]);
			m_nextOffsetTime = gpGlobals->curtime + std::max(0.2f, m_offsetDelay * 0.5f);
		}

		return target + m_offset;
	}

	bool Turn(CBaseBot* bot, const QAngle& current, const QAngle& desired, bool hasTarget, int priority, float distance, float dt, QAngle& out) override
	{
		const float now = gpGlobals->curtime;
		RefreshCfg(now);

		if (m_type <= 2) { return false; }   // snap / constant velocity: NavBot's own turn handles it

		// Work on the bot's real eye angles: 'current' also contains the weapon's recoil kick, which the engine adds by itself.
		const QAngle eye = bot->GetEyeAngles();

		// Same rule as PodAimOverride: only idle and path looks are relaxed, every other look needs the exact angle.
		const bool exact = priority > static_cast<int>(IPlayerController::LOOK_PATH);
		const bool enemy = priority == static_cast<int>(IPlayerController::LOOK_COMBAT);

		// recoil compensation (podbot_fire.inl estimates the recoil from the shots): the view is pulled down by part of it
		const float pull = enemy ? fire::RecoilPull(bot, now) : 0.0f;

		const Ang r = m_spring.Update(Ang{ eye.x, eye.y }, Ang{ desired.x + pull, desired.y }, exact, enemy, distance, dt, now, m_cfg);
		out.x = r.pitch;
		out.y = r.yaw;
		out.z = 0.0f;

#ifdef META_CONPRINTF
		FlickInfo f;
		if (m_debug && m_spring.TakeFlick(f) && f.size >= 20.0f)
		{
			META_CONPRINTF("[podbot aim] %s: flick %.0f deg, onset %.0f ms, speed x%.2f, damper x%.2f, personality x%.2f\n",
				bot->GetClientName(), f.size, f.onsetMs, f.stiffMul, f.dampMul, m_spring.Speed());
		}
#endif
		return true;
	}

private:
	void RefreshCfg(float now)
	{
		if (now < m_nextRefresh) { return; }
		m_nextRefresh = now + 1.0f;

		auto G = [](const char* name, float def) { return human::detail::Get(name, def); };
		m_type = static_cast<int>(G("pb_aim_type", static_cast<float>(m_type)));
		m_cfg.stiffX = G("pb_aim_spring_stiffness_x", m_cfg.stiffX);
		m_cfg.stiffY = G("pb_aim_spring_stiffness_y", m_cfg.stiffY);
		m_cfg.dampX = G("pb_aim_damper_coefficient_x", m_cfg.dampX);
		m_cfg.dampY = G("pb_aim_damper_coefficient_y", m_cfg.dampY);
		m_cfg.dampScale = G("pb_aim_damping_scale", m_cfg.dampScale);
		m_cfg.infXonY = G("pb_aim_influence_x_on_y", m_cfg.infXonY);
		m_cfg.infYonX = G("pb_aim_influence_y_on_x", m_cfg.infYonX);
		m_cfg.devX = G("pb_aim_deviation_x", m_cfg.devX);
		m_cfg.devY = G("pb_aim_deviation_y", m_cfg.devY);
		m_cfg.offsetDelay = G("pb_aim_offset_delay", m_cfg.offsetDelay);
		m_cfg.noTargetSlowdown = G("pb_aim_notarget_slowdown_ratio", m_cfg.noTargetSlowdown);
		m_offsetDelay = m_cfg.offsetDelay;
		m_cfg.levelDown = Clampf(G("pb_aimh_level_down", m_cfg.levelDown), 0.0f, 89.0f);
		m_cfg.levelUp = Clampf(G("pb_aimh_level_up", m_cfg.levelUp), 0.0f, 89.0f);
		m_cfg.overshootChance = Clampf(G("pb_aimh_overshoot_chance", m_cfg.overshootChance), 0.0f, 1.0f);
		m_cfg.onsetMs = std::max(0.0f, G("pb_aimh_onset_ms", m_cfg.onsetMs));
		m_cfg.tremorDeg = std::max(0.0f, G("pb_aimh_tremor", m_cfg.tremorDeg));
		m_cfg.flickJitter = std::max(0.0f, G("pb_aimh_jitter", m_cfg.flickJitter));
		m_debug = G("pb_aimh_debug", 0.0f) > 0.5f;
	}

	SkillBracket m_bracket;
	Rng m_rng;
	HumanSpring m_spring;
	Cfg m_cfg;
	Vector m_offset = Vector(0.0f, 0.0f, 0.0f);
	float m_nextOffsetTime = 0.0f;
	float m_nextRefresh = 0.0f;
	float m_offsetDelay = 1.2f;
	int m_type = 4;
	bool m_debug = false;
};

#endif // PB_AIMH_PURE_ONLY

}} // namespace podbot::aimh
