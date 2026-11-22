// podbot_look.inl - human-like glances while running (look-around v1).
//
// Source of the numbers: SofTy POV demo, 10.7 min of running, 58 events (HANDOFF section 8):
//   5.42 glances/min while running, peak angle median 33 deg (p10 23, p90 88),
//   duration median 475 ms (p10 210, p90 723), left/right 50/50, no shooting within 1 s.
//
// Part 1 = pure logic (no engine types, unit-tested with g++ -DPB_LOOK_PURE_ONLY).
// Part 2 = engine glue, one call per bot ~10 Hz: podbot::look::UpdateLook(now, get, log) from CPodBotManager::Frame().
#pragma once
#include <algorithm>
#include <cmath>
#include <functional>
#include <random>
#include <unordered_map>

namespace podbot { namespace look {

// ===================================================================================================================
// PART 1 - PURE LOGIC
// ===================================================================================================================
struct Glance { float angle; float duration; }; // angle: signed degrees (+ = left, - = right); duration: seconds

inline std::mt19937& Rng() { static std::mt19937 g{ std::random_device{}() }; return g; }
inline float Rand01() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(Rng()); }
inline float Normal() { return std::normal_distribution<float>(0.0f, 1.0f)(Rng()); }

// peak = 20 + 13 * exp(1.29 * N): median 33, p10 about 22.5, p90 about 88 (fitted to the demo percentiles). Hard cap 120.
inline float SampleAngle()
{
	return std::min(120.0f, 20.0f + 13.0f * std::exp(1.29f * Normal()));
}

// duration: normal mean 0.475 s, sd 0.2 s (p10 about 0.22, p90 about 0.73), clamped to 0.12 .. 0.80 s
inline float SampleDuration()
{
	return std::max(0.12f, std::min(0.80f, 0.475f + 0.2f * Normal()));
}

inline Glance SampleGlance()
{
	const float sign = (Rand01() < 0.5f) ? 1.0f : -1.0f;
	return { sign * SampleAngle(), SampleDuration() };
}

// Time until the next glance: exponential with mean 60 / ratePerMinute, never closer than minGap seconds.
inline float NextGlanceDelay(float ratePerMinute, float minGap = 1.5f)
{
	if (ratePerMinute <= 0.0f) { return 1.0e9f; }
	const float u = std::max(1.0e-4f, Rand01());
	return std::max(minGap, -std::log(u) * (60.0f / ratePerMinute));
}

inline float NormalizeYaw(float y)
{
	while (y > 180.0f) { y -= 360.0f; }
	while (y < -180.0f) { y += 360.0f; }
	return y;
}

// ===================================================================================================================
// PART 2 - ENGINE
// ===================================================================================================================
#ifndef PB_LOOK_PURE_ONLY

namespace detail
{
	struct LookState
	{
		bool init = false;
		float nextGlance = 0.0f;
		float lastTime = 0.0f;
		Vector lastPos = Vector(0.0f, 0.0f, 0.0f);
		float speed = 0.0f;
		float travelYaw = 0.0f;   // direction of travel in degrees, from position changes (no eye-angle getter needed)
	};
	inline std::unordered_map<const void*, LookState>& States() { static std::unordered_map<const void*, LookState> s; return s; }
}

// Settings (read through `get`): pb_look 1, pb_look_rate 5.4 (glances/min while running), pb_look_min_speed 100 (units/s).
// A glance is a short AimAt toward a point to the left or right of the travel direction, at head height.
inline void UpdateLook(float now, const std::function<float(const char*, float)>& get, const std::function<void(CCSSBot*, const char*)>& log)
{
	if (get("pb_look", 1.0f) < 0.5f) { return; }

	const float rate = get("pb_look_rate", 5.4f);
	const float minSpeed = get("pb_look_min_speed", 100.0f);
	const bool freezeNow = csslib::IsInFreezeTime();

	auto lookFunc = [&](CBaseBot* b)
	{
		CCSSBot* bot = static_cast<CCSSBot*>(b);
		detail::LookState& st = detail::States()[bot];

		if (!bot->IsAlive()) { st = detail::LookState(); return; }

		const Vector pos = bot->GetAbsOrigin();
		if (!st.init)
		{
			st.init = true; st.lastPos = pos; st.lastTime = now;
			st.nextGlance = now + NextGlanceDelay(rate);
			return;
		}

		const float dt = now - st.lastTime;
		if (dt > 0.05f)
		{
			const float dx = pos.x - st.lastPos.x;
			const float dy = pos.y - st.lastPos.y;
			st.speed = std::sqrt(dx * dx + dy * dy) / dt;
			if (st.speed >= minSpeed) { st.travelYaw = std::atan2(dy, dx) * (180.0f / 3.14159265f); }
			st.lastPos = pos; st.lastTime = now;
		}

		if (freezeNow) { st.nextGlance = now + NextGlanceDelay(rate); return; }
		if (now < st.nextGlance) { return; }

		// only while moving, not in a fight, and only when nothing important (combat, doors, ladders...) is steering the view
		const CKnownEntity* threat = bot->GetSensorInterface()->GetPrimaryKnownThreat(true);
		const bool busy = (threat != nullptr && threat->IsVisibleNow()) || bot->HasTakenDamageRecently(1.0f);
		IPlayerController* control = bot->GetControlInterface();

		if (st.speed < minSpeed || busy || control->GetAimAtPriority() > IPlayerController::LOOK_PATH)
		{
			st.nextGlance = now + 0.5f;   // try again soon instead of waiting a whole interval
			return;
		}

		const Glance g = SampleGlance();
		const float yaw = (st.travelYaw + g.angle) * (3.14159265f / 180.0f);
		const Vector target(pos.x + std::cos(yaw) * 500.0f, pos.y + std::sin(yaw) * 500.0f, pos.z + 64.0f);

		control->AimAt(target, IPlayerController::LOOK_SEARCH, g.duration, "podbot look-around glance");
		st.nextGlance = now + NextGlanceDelay(rate);
		log(bot, "look: glance");
	};

	extmanager->ForEachBot(lookFunc);
}

#endif // PB_LOOK_PURE_ONLY

}} // namespace podbot::look
