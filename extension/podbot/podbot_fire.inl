// podbot_fire.inl - human fire control (burst, spray, counter-strafe, recoil compensation, AWP handling).
//
// NavBot decides WHEN it may fire; this layer decides how a human would pull the trigger. Everything is driven from the outside
// through ICombat::DisableFiringWeapons, the player input presses and the aim override, so no NavBot or CS:S bot class is edited.
// Numbers are measured from the demos (HANDOFF section 8): SofTy (POV) plus 12 players in the SourceTV demos.
//
//   Burst length       rifles: median 2 shots, 12 percent of bursts 6+ shots; close range sprays longer, far range taps (1-3).
//   Trigger delay      first shot comes about 110 ms (lognormal) after the crosshair settles on the target.
//   Counter-strafe     when moving fast and about to shoot, 45 percent of the time the bot taps the opposite key and waits
//                      ~130 ms (measured median) before firing; snipers nearly always stop first.
//   Recoil compensation the view is pulled down by about half of the weapon's recoil (measured 51 percent), capped.
//                      AK-47 / M4A1 recoil curves are measured; other weapons use scaled copies.
//   AWP / Scout        scope only beyond 350 units, and after every shot a quick switch to the pistol and back (Q-Q) cancels
//                      the automatic re-zoom (owner playbook).
//
// Part 1 = pure logic (FireBrain), unit-tested with g++ -DPB_FIRE_PURE_ONLY.
// Part 2 = engine adapter, one call per server frame: podbot::fire::UpdateFire(now, get, log) from CPodBotManager::Frame().
#pragma once
#include <algorithm>
#include <cmath>
#include <functional>
#include <random>
#include <string>
#include <unordered_map>

namespace podbot { namespace fire {

// ===================================================================================================================
// PART 1 - PURE LOGIC
// ===================================================================================================================
enum class WClass { Other, Rifle, Smg, Pistol, Sniper };

struct WProfile
{
	WClass cls;
	bool autoFire;       // holding the button keeps firing
	float cycle;         // seconds between shots
	float recoilScale;   // multiplier on the recoil curve (0 = none)
	bool akCurve;        // true = AK-47 curve, false = M4A1 curve
};

// Pitch rise in degrees after shot 1..10 of a burst (HANDOFF section 8, pooled over three demos).
constexpr float kAK[10] = { 0.7f, 1.7f, 3.4f, 5.6f, 7.6f, 8.9f, 9.4f, 9.8f, 9.9f, 9.9f };
constexpr float kM4[10] = { 0.7f, 1.4f, 2.8f, 4.1f, 4.8f, 5.1f, 5.1f, 5.2f, 5.3f, 5.4f };

inline const WProfile& ProfileFor(const std::string& name)
{
	static const struct { const char* n; WProfile p; } table[] = {
		{ "weapon_ak47",    { WClass::Rifle,  true,  0.100f, 1.00f, true  } },
		{ "weapon_m4a1",    { WClass::Rifle,  true,  0.090f, 1.00f, false } },
		{ "weapon_galil",   { WClass::Rifle,  true,  0.090f, 1.00f, true  } },
		{ "weapon_famas",   { WClass::Rifle,  true,  0.090f, 1.00f, false } },
		{ "weapon_sg552",   { WClass::Rifle,  true,  0.090f, 1.00f, false } },
		{ "weapon_aug",     { WClass::Rifle,  true,  0.090f, 1.00f, false } },
		{ "weapon_m249",    { WClass::Rifle,  true,  0.080f, 1.30f, true  } },
		{ "weapon_mp5navy", { WClass::Smg,    true,  0.075f, 0.55f, false } },
		{ "weapon_p90",     { WClass::Smg,    true,  0.070f, 0.50f, false } },
		{ "weapon_ump45",   { WClass::Smg,    true,  0.105f, 0.60f, false } },
		{ "weapon_mac10",   { WClass::Smg,    true,  0.075f, 0.60f, false } },
		{ "weapon_tmp",     { WClass::Smg,    true,  0.070f, 0.45f, false } },
		{ "weapon_usp",     { WClass::Pistol, false, 0.150f, 0.80f, false } },
		{ "weapon_glock",   { WClass::Pistol, false, 0.150f, 0.80f, false } },
		{ "weapon_p228",    { WClass::Pistol, false, 0.150f, 0.80f, false } },
		{ "weapon_fiveseven", { WClass::Pistol, false, 0.150f, 0.80f, false } },
		{ "weapon_elite",   { WClass::Pistol, false, 0.120f, 0.80f, false } },
		{ "weapon_deagle",  { WClass::Pistol, false, 0.225f, 1.20f, false } },
		{ "weapon_awp",     { WClass::Sniper, false, 1.460f, 0.00f, false } },
		{ "weapon_scout",   { WClass::Sniper, false, 1.250f, 0.00f, false } },
	};
	static const WProfile other = { WClass::Other, false, 0.300f, 0.0f, false };
	for (const auto& e : table) { if (name == e.n) { return e.p; } }
	return other;
}

// Recoil in degrees after `shots` shots (fractional allowed, linear between the measured points).
inline float RecoilAt(const WProfile& w, float shots)
{
	if (shots <= 0.0f || w.recoilScale <= 0.0f) { return 0.0f; }
	const float* c = w.akCurve ? kAK : kM4;
	if (shots <= 1.0f) { return c[0] * shots * w.recoilScale; }
	if (shots >= 10.0f) { return c[9] * w.recoilScale; }
	const int i = static_cast<int>(shots - 1.0f);
	const float t = (shots - 1.0f) - static_cast<float>(i);
	return (c[i] + (c[i + 1] - c[i]) * t) * w.recoilScale;
}

inline std::mt19937& Rng() { static std::mt19937 g{ std::random_device{}() }; return g; }
inline float Rand01() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(Rng()); }
inline float RandRange(float a, float b) { return a + (b - a) * Rand01(); }
inline float Normal() { return std::normal_distribution<float>(0.0f, 1.0f)(Rng()); }
inline float Clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }
inline float LogNormalMs(float medianMs, float sigma, float loMs, float hiMs) { return Clampf(medianMs * std::exp(sigma * Normal()), loMs, hiMs) * 0.001f; }

// Shots in one burst. Mid range reproduces the measurement: median 2, 12 percent of bursts 6 or more.
inline int SampleBurst(float dist)
{
	static const float mid[10]  = { 20, 30, 20, 10, 8, 3, 3, 2, 2, 2 };
	static const float nearr[10] = { 2, 5, 10, 10, 15, 10, 10, 8, 10, 20 };   // close range: spray
	static const float farr[10]  = { 30, 40, 20, 10, 0, 0, 0, 0, 0, 0 };      // far range: taps and short bursts
	const float* w = (dist < 350.0f) ? nearr : (dist > 850.0f ? farr : mid);
	float sum = 0.0f; for (int i = 0; i < 10; ++i) { sum += w[i]; }
	float r = Rand01() * sum;
	for (int i = 0; i < 10; ++i) { r -= w[i]; if (r <= 0.0f) { return (i == 9) ? 10 + static_cast<int>(Rand01() * 6.0f) : i + 1; } }
	return 2;
}

struct Cfg
{
	bool burst = true;            // limit bursts (pb_fire_burst)
	float triggerMs = 110.0f;     // median delay between the crosshair settling and the first shot
	float csChance = 0.45f;       // chance to counter-strafe before shooting while moving
	float csMs = 130.0f;          // median wait after the counter-strafe tap
	float csSpeed = 90.0f;        // units/s: faster than this counts as moving
	float recoilComp = 0.5f;      // share of the recoil the view is pulled down by (measured 0.51)
	float recoilMax = 4.5f;       // cap on the pull, degrees
	bool awpQQ = true;            // quick-switch after every sniper shot
	float awpScopeMin = 350.0f;   // snipers scope only farther away than this
};

struct In
{
	float now = 0.0f, dt = 0.0f;
	bool alive = false, hasWeapon = false;
	const WProfile* w = nullptr;
	bool inCombat = false, threatVisible = false, canFire = false, onTarget = false, attackHeld = false;
	bool wantsScope = false;
	float dist = 0.0f;            // distance to the enemy
	float speed = 0.0f, velX = 0.0f, velY = 0.0f;   // horizontal speed and world velocity
	float viewYaw = 0.0f;         // degrees, where the bot is looking
};

struct Out
{
	float blockFire = 0.0f;       // seconds to stop firing (0 = nothing)
	bool releaseButtons = false;  // also release the attack buttons
	int strafe = 0;               // -1 tap left, +1 tap right
	int forward = 0;              // +1 tap forward, -1 tap back
	float tapDur = 0.0f;
	int scopeWant = -1;           // -1 leave alone, 0 unscope, 1 scope
	bool qq = false;              // sniper shot taken: do the quick switch
	float qqFirst = 0.0f, qqSecond = 0.0f;   // seconds from now: switch away, switch back
	int event = 0;                // for debug: 1 burst pause, 2 trigger delay, 3 counter-strafe
};

class FireBrain
{
public:
	void Reset() { *this = FireBrain(); }

	// Recoil pull to add to the desired pitch (degrees, positive = view down).
	float Pull(float now, const Cfg& c) const
	{
		if (m_w == nullptr || m_recoilShots <= 0.0f) { return 0.0f; }
		const float since = now - m_lastShot - m_w->cycle;
		const float k = since <= 0.0f ? 1.0f : std::max(0.0f, 1.0f - since / 0.45f);   // recoil settles in about 0.45 s
		return std::min(c.recoilMax, RecoilAt(*m_w, m_recoilShots) * k * c.recoilComp);
	}

	void Frame(const In& in, const Cfg& c, Out& out)
	{
		out = Out();
		if (!in.alive || !in.hasWeapon || in.w == nullptr) { Reset(); return; }
		m_w = in.w;
		const WProfile& w = *in.w;

		// ---- trigger delay: the crosshair just settled on a target and the last shot was a while ago.
		// Decided first: NavBot may already press fire on this very frame, so the press is cancelled and delayed.
		bool suppress = false;
		if (in.threatVisible && !m_onTargetPrev && in.onTarget && in.now - m_lastShot > 1.0f && in.now >= m_blockUntil)
		{
			const float d = LogNormalMs(c.triggerMs, 0.4f, 40.0f, 350.0f);
			out.blockFire = std::max(out.blockFire, d); m_blockUntil = in.now + d; out.event = 2;
			if (in.attackHeld) { out.releaseButtons = true; suppress = true; }
		}
		m_onTargetPrev = in.threatVisible && in.onTarget;

		// ---- shot detection from the attack button (no game event needed)
		const bool pressed = in.attackHeld && !suppress;
		if (pressed && !m_prevPressed) { m_accum = 0.0f; Shot(in, c, out); m_csChecked = true; }
		else if (pressed && w.autoFire)
		{
			m_accum += in.dt;
			while (m_accum >= w.cycle) { m_accum -= w.cycle; Shot(in, c, out); }
		}
		m_prevPressed = pressed;

		// ---- burst limit: stop the trigger after the sampled number of shots
		if (c.burst && w.autoFire && m_burstTarget > 0 && m_burstShots >= m_burstTarget && in.now >= m_blockUntil && in.now > m_lastShot)   // next frame: releasing on the shot's own frame would cancel it
		{
			const float pause = RandRange(0.20f, 0.45f);
			out.blockFire = std::max(out.blockFire, pause); out.releaseButtons = true; out.event = 1;
			m_blockUntil = in.now + pause; m_burstShots = 0; m_burstTarget = 0;
		}

		// ---- no target: forget the engagement after a moment
		if (!in.threatVisible)
		{
			m_csChecked = false;
			if (in.now - m_lastShot > 1.0f) { m_burstShots = 0; m_burstTarget = 0; m_recoilShots = 0.0f; }
			return;
		}

		// ---- counter-strafe: about to shoot (no shot taken yet in this opportunity) while moving fast
		if (!pressed && !m_csChecked && in.inCombat && in.canFire && in.speed > c.csSpeed && in.now >= m_csCool)
		{
			m_csChecked = true; m_csCool = in.now + 0.8f;
			const float chance = (w.cls == WClass::Sniper) ? 0.90f : c.csChance;
			if (Rand01() < chance)
			{
				const float delay = LogNormalMs(c.csMs, 0.35f, 60.0f, 300.0f);
				const float yaw = in.viewYaw * 0.01745329252f;
				const float fwd = in.velX * std::cos(yaw) + in.velY * std::sin(yaw);       // along the view
				const float right = in.velX * std::sin(yaw) - in.velY * std::cos(yaw);     // to the right of the view
				if (std::fabs(right) >= std::fabs(fwd)) { out.strafe = right > 0.0f ? -1 : 1; } else { out.forward = fwd > 0.0f ? -1 : 1; }
				out.tapDur = RandRange(0.06f, 0.10f);
				out.blockFire = std::max(out.blockFire, delay); m_blockUntil = std::max(m_blockUntil, in.now + delay);
				out.event = 3;
			}
		}

		// ---- scope policy for snipers
		if (w.cls == WClass::Sniper)
		{
			const bool want = in.dist > c.awpScopeMin;
			if (want != in.wantsScope) { out.scopeWant = want ? 1 : 0; }
		}
	}

private:
	void Shot(const In& in, const Cfg& c, Out& out)
	{
		if (in.now - m_lastShot > 0.6f) { m_recoilShots = 0.0f; m_burstShots = 0; m_burstTarget = 0; }
		if (m_burstTarget == 0) { m_burstTarget = SampleBurst(in.dist); }
		m_burstShots += 1; m_recoilShots += 1.0f; m_lastShot = in.now;

		if (in.w->cls == WClass::Sniper && c.awpQQ)
		{
			out.qq = true; out.qqFirst = RandRange(0.10f, 0.30f); out.qqSecond = out.qqFirst + RandRange(0.15f, 0.35f);
		}
	}

	const WProfile* m_w = nullptr;
	bool m_prevPressed = false, m_onTargetPrev = false, m_csChecked = false;
	float m_accum = 0.0f, m_lastShot = -100.0f, m_blockUntil = 0.0f, m_csCool = 0.0f;
	int m_burstShots = 0, m_burstTarget = 0;
	float m_recoilShots = 0.0f;
};

// ===================================================================================================================
// PART 2 - ENGINE ADAPTER
// ===================================================================================================================
#ifndef PB_FIRE_PURE_ONLY

namespace detail
{
	struct BotState
	{
		FireBrain brain;
		bool init = false;
		Vector lastPos = Vector(0.0f, 0.0f, 0.0f);
		float lastTime = 0.0f, vx = 0.0f, vy = 0.0f, speed = 0.0f;
		int qq = 0;                  // 0 none, 1 waiting to switch away, 2 waiting to switch back
		float qqAt = 0.0f, qqSecondAt = 0.0f;
		std::string qqHeld, qqPistol;
		float holdAimUntil = 0.0f;   // keep aiming at the enemy until then (trigger held back)
	};
	inline std::unordered_map<const void*, BotState>& States() { static std::unordered_map<const void*, BotState> s; return s; }
	inline Cfg& CurCfg() { static Cfg c; return c; }
}

// Recoil pull (degrees, positive = down) the aim override adds to the desired pitch. 0 if unknown.
inline float RecoilPull(const void* bot, float now)
{
	auto& m = detail::States();
	const auto it = m.find(bot);
	return it == m.end() ? 0.0f : it->second.brain.Pull(now, detail::CurCfg());
}

// Settings (read through `get`): pb_fire 1, pb_fire_burst 1, pb_fire_trigger_ms 110, pb_fire_cstrafe 0.45, pb_fire_cstrafe_ms 130,
// pb_fire_cstrafe_speed 90, pb_fire_recoil 0.5, pb_fire_recoil_max 4.5, pb_fire_awp_qq 1, pb_fire_awp_scope_min 350, pb_fire_debug 0.
inline void UpdateFire(float now, const std::function<float(const char*, float)>& get, const std::function<void(CCSSBot*, const char*)>& log)
{
	// New map / new server: the game clock restarts at 0, so the stored state of the old map must go.
	static float lastNow = 0.0f;
	if (now + 0.5f < lastNow) { detail::States().clear(); }
	const float dt = std::max(0.0f, now - lastNow);
	lastNow = now;

	if (get("pb_fire", 1.0f) < 0.5f) { return; }

	Cfg& c = detail::CurCfg();
	c.burst = get("pb_fire_burst", 1.0f) > 0.5f;
	c.triggerMs = std::max(0.0f, get("pb_fire_trigger_ms", 110.0f));
	c.csChance = Clampf(get("pb_fire_cstrafe", 0.45f), 0.0f, 1.0f);
	c.csMs = std::max(0.0f, get("pb_fire_cstrafe_ms", 130.0f));
	c.csSpeed = std::max(0.0f, get("pb_fire_cstrafe_speed", 90.0f));
	c.recoilComp = Clampf(get("pb_fire_recoil", 0.5f), 0.0f, 1.0f);
	c.recoilMax = std::max(0.0f, get("pb_fire_recoil_max", 4.5f));
	c.awpQQ = get("pb_fire_awp_qq", 1.0f) > 0.5f;
	c.awpScopeMin = std::max(0.0f, get("pb_fire_awp_scope_min", 350.0f));
	const bool dbg = get("pb_fire_debug", 0.0f) > 0.5f;

	auto botFunc = [&](CBaseBot* b)
	{
		CCSSBot* bot = static_cast<CCSSBot*>(b);
		detail::BotState& st = detail::States()[bot];

		if (!bot->IsAlive()) { st = detail::BotState(); return; }

		// horizontal velocity from position changes (no velocity getter needed)
		const Vector pos = bot->GetAbsOrigin();
		if (!st.init) { st.init = true; st.lastPos = pos; st.lastTime = now; return; }
		const float mdt = now - st.lastTime;
		if (mdt > 0.04f)
		{
			st.vx = (pos.x - st.lastPos.x) / mdt; st.vy = (pos.y - st.lastPos.y) / mdt;
			st.speed = std::sqrt(st.vx * st.vx + st.vy * st.vy);
			st.lastPos = pos; st.lastTime = now;
		}

		IInventory* inventory = bot->GetInventoryInterface();
		ICombat* combat = bot->GetCombatInterface();
		IPlayerController* control = bot->GetControlInterface();
		const CBotWeapon* active = inventory->GetActiveBotWeapon();
		const ICombat::CombatData& cd = combat->GetCachedCombatData();

		In in;
		in.now = now; in.dt = dt; in.alive = true; in.hasWeapon = (active != nullptr);
		if (active != nullptr) { in.w = &ProfileFor(active->GetClassname()); }
		in.inCombat = cd.in_combat; in.threatVisible = cd.is_visible; in.canFire = cd.can_fire;
		in.onTarget = control->IsAimOnTarget(); in.attackHeld = control->IsPressingAttackButton();
		in.wantsScope = combat->WantstoBeScoped();
		in.dist = cd.enemy_range; in.speed = st.speed; in.velX = st.vx; in.velY = st.vy;
		const Vector& look = control->GetLookTargetPosition();
		in.viewYaw = std::atan2(look.y - pos.y, look.x - pos.x) * 57.29577951f;

		Out out;
		st.brain.Frame(in, c, out);

		if (out.blockFire > 0.0f) { combat->DisableFiringWeapons(out.blockFire, out.releaseButtons); st.holdAimUntil = now + out.blockFire + 0.10f; }

		// While the trigger is held back (burst gap, trigger delay, counter-strafe) a human keeps the crosshair on the enemy.
		// Without this NavBot stops aiming and its look falls back to the path look: the view dropped to the floor after every burst.
		if (now < st.holdAimUntil && in.threatVisible)
		{
			const CKnownEntity* threat = bot->GetSensorInterface()->GetPrimaryKnownThreat(true);
			if (threat != nullptr && threat->GetEntity() != nullptr)
			{
				control->AimAt(threat->GetEntity(), IPlayerController::LOOK_COMBAT, 0.25f, "podbot fire: hold the enemy");
			}
		}
		if (out.strafe < 0) { control->PressMoveLeftButton(out.tapDur); } else if (out.strafe > 0) { control->PressMoveRightButton(out.tapDur); }
		if (out.forward > 0) { control->PressForwardButton(out.tapDur); } else if (out.forward < 0) { control->PressBackwardsButton(out.tapDur); }
		if (out.scopeWant >= 0) { combat->SetWantsToScopeState(out.scopeWant == 1); }

		// AWP / Scout: quick switch to the pistol and back after the shot (cancels the automatic re-zoom)
		if (out.qq && st.qq == 0 && active != nullptr)
		{
			std::string held;
			const char* pistol = human::detail::PickFlickWeapon(bot, held);
			if (pistol != nullptr) { st.qq = 1; st.qqHeld = held; st.qqPistol = pistol; st.qqAt = now + out.qqFirst; st.qqSecondAt = now + out.qqSecond; }
		}
		if (st.qq == 1 && now >= st.qqAt) { bot->DelayedFakeClientCommand((std::string("use ") + st.qqPistol).c_str()); st.qq = 2; }
		else if (st.qq == 2 && now >= st.qqSecondAt) { bot->DelayedFakeClientCommand((std::string("use ") + st.qqHeld).c_str()); st.qq = 0; }

#ifdef META_CONPRINTF
		if (dbg && out.event != 0)
		{
			static const char* const names[] = { "", "burst ended, pause", "trigger delay", "counter-strafe" };
			META_CONPRINTF("[podbot fire] %s: %s %.0f ms (speed %.0f, dist %.0f)\n", bot->GetClientName(), names[out.event], out.blockFire * 1000.0f, st.speed, in.dist);
		}
#endif
	};
	extmanager->ForEachBot(botFunc);
}

#endif // PB_FIRE_PURE_ONLY

}} // namespace podbot::fire
