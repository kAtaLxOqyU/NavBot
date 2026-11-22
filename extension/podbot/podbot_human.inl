// podbot_human.inl - CS:S "human behaviour" module for the PODBot layer (v2: eco connected).
//
// Part 1 (pure logic, no engine types): economy rules, round tracker, fidget windows. Unit-tested outside the game (test_human.cpp).
// Part 2 (engine): fidget timer + eco connection (round events, money collection, buy override).
//
// Included from podbot_manager.cpp (fidget call) and css_mod.cpp (eco hooks). install_human1.py adds both.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace podbot { namespace human {

// ===================================================================================================================
// PART 1 - PURE LOGIC
// ===================================================================================================================
struct EcoCfg
{
	int startMoney = 800;      // mp_startmoney (800 or 1000)
	int fullBuyT = 4100;       // AK-47 2500 + vest/helm 1000 + 600 utility
	int fullBuyCT = 4750;      // M4A1 3100 + vest/helm 1000 + kit 200 + 450 utility
	int lossBase = 1400;       // 1st loss
	int lossStep = 500;        // each further consecutive loss
	int lossCap = 3400;
	int priceDeagle = 650;
	int priceKevlar = 650;
	int priceFlash = 200;
	int priceHE = 300;
	double readyRatio = 0.60;  // share of the team that must afford a full buy
};

// 1st loss 1400, 2nd 1900, 3rd 2400, 4th 2900, 5th and later 3400. A win resets the count to 0.
inline int LossBonus(const EcoCfg& c, int consecutiveLosses)
{
	if (consecutiveLosses <= 1) { return c.lossBase; }
	return std::min(c.lossBase + (consecutiveLosses - 1) * c.lossStep, c.lossCap);
}

// mp_startmoney is read from the game: everyone spawns with it in round 1, so the average cash then is 800 or 1000.
inline int DetectStartMoney(double avgCashRound1) { return avgCashRound1 >= 900.0 ? 1000 : 800; }

inline int FullBuy(const EcoCfg& c, bool isT) { return isT ? c.fullBuyT : c.fullBuyCT; }

enum class EcoMode { Pistol, Full, Eco, Force };

inline const char* ModeName(EcoMode m)
{
	switch (m) { case EcoMode::Pistol: return "pistol"; case EcoMode::Full: return "full buy"; case EcoMode::Eco: return "eco";
		case EcoMode::Force: return "force"; }
	return "?";
}

struct TeamDecision { EcoMode mode; std::string reason; bool eco() const { return mode == EcoMode::Eco; } };

inline TeamDecision TeamShouldEco(const EcoCfg& c, bool isT, const std::vector<int>& cash, int roundNumber, bool plantedInPistol)
{
	if (cash.empty()) { return { EcoMode::Full, "no players" }; }
	if (roundNumber == 1) { return { EcoMode::Pistol, "pistol round" }; }

	double sum = 0.0;
	for (int v : cash) { sum += v; }
	const double avg = sum / static_cast<double>(cash.size());
	const int full = FullBuy(c, isT);

	if (roundNumber == 2)
	{
		if (isT && (plantedInPistol || (c.startMoney == 1000 && avg >= 1800.0)))
		{
			return { EcoMode::Force, "force: deagle + kevlar off the pistol plant / start money" };
		}
		if (!isT && c.startMoney == 1000 && avg >= 1900.0)
		{
			return { EcoMode::Force, "force: deagle + kevlar" };
		}
	}

	int ready = 0;
	for (int v : cash) { if (v >= full) { ++ready; } }
	const double ratio = static_cast<double>(ready) / static_cast<double>(cash.size());

	if (ratio < c.readyRatio) { return { EcoMode::Eco, "synchronized eco: too few can full-buy" }; }
	return { EcoMode::Full, "full buy" };
}

struct PlayerBuy { std::vector<std::string> items; int projectedBank; bool fullSave; };

// currentCash: money now. consecutiveLosses: losses in a row BEFORE this round ends (the upcoming loss is +1).
inline PlayerBuy PlayerEcoBuy(const EcoCfg& c, bool isT, int currentCash, int consecutiveLosses, bool starAimer, bool canDrop)
{
	PlayerBuy out { {}, 0, false };
	const int projected = currentCash + LossBonus(c, consecutiveLosses + 1);
	int surplus = projected - FullBuy(c, isT);
	int spent = 0;
	out.projectedBank = projected;

	if (surplus < 200) { out.fullSave = true; return out; }

	if (surplus >= c.priceDeagle && (canDrop || starAimer))
	{
		out.items.push_back((canDrop && !starAimer) ? "deagle (drop to star aimer)" : "deagle");
		surplus -= c.priceDeagle; spent += c.priceDeagle;
	}
	if (surplus >= c.priceFlash)
	{
		out.items.push_back("flashbang"); surplus -= c.priceFlash; spent += c.priceFlash;
	}
	else if (surplus >= c.priceHE && !isT)
	{
		out.items.push_back("hegrenade"); surplus -= c.priceHE; spent += c.priceHE;
	}
	if (c.startMoney == 1000 && surplus >= c.priceFlash && out.items.size() < 2)
	{
		out.items.push_back("flashbang"); surplus -= c.priceFlash; spent += c.priceFlash;
	}
	out.projectedBank = projected - spent;
	return out;
}

// Force round: Deagle + Kevlar ($1300), then a flashbang if there is money left.
inline std::vector<std::string> ForceBuyItems(const EcoCfg& c, int cash)
{
	std::vector<std::string> items;
	if (cash < c.priceDeagle) { return items; }   // cannot afford the force buy: save instead
	items.push_back("deagle"); cash -= c.priceDeagle;
	if (cash >= c.priceKevlar) { items.push_back("vest"); cash -= c.priceKevlar; }
	if (cash >= c.priceFlash) { items.push_back("flashbang"); cash -= c.priceFlash; }
	return items;
}

inline int ItemPrice(const EcoCfg& c, const std::string& item)
{
	if (item.rfind("deagle", 0) == 0) { return c.priceDeagle; }
	if (item == "vest") { return c.priceKevlar; }
	if (item == "flashbang") { return c.priceFlash; }
	if (item == "hegrenade") { return c.priceHE; }
	return 100000;
}

inline const char* BuyAlias(const std::string& item) { return item.rfind("deagle", 0) == 0 ? "deagle" : item.c_str(); }

// Tracks what the eco logic needs from the match: round number inside the half, consecutive losses per team, pistol plant.
// Team index: 2 = T, 3 = CT.
struct RoundTracker
{
	int halfRounds = 15;
	int roundsPlayed = 0;
	std::array<int, 4> losses { { 0, 0, 0, 0 } };
	bool plantThisRound = false;
	bool pistolPlant = false;

	int RoundInHalf() const { return (halfRounds > 0 ? roundsPlayed % halfRounds : roundsPlayed) + 1; }
	void OnPlant() { plantThisRound = true; }

	void Reset()
	{
		roundsPlayed = 0; losses.fill(0); plantThisRound = false; pistolPlant = false;
	}

	// reason 16 = game commencing / restart. winner 2 = T, 3 = CT, anything else = no result.
	void OnRoundEnd(int winner, int reason, int restartReason = 16)
	{
		if (reason == restartReason) { Reset(); return; }
		if (winner != 2 && winner != 3) { plantThisRound = false; return; }

		const int r = RoundInHalf();
		if (r == 1) { pistolPlant = plantThisRound; }
		else if (r == 2) { pistolPlant = false; }

		for (int t = 2; t <= 3; ++t) { losses[t] = (t == winner) ? 0 : losses[t] + 1; }
		++roundsPlayed;
		plantThisRound = false;

		if (halfRounds > 0 && roundsPlayed % halfRounds == 0) { losses.fill(0); pistolPlant = false; } // half time: sides swap, money resets
	}
};

// ---- fidget windows (seconds after freeze time ends) ----
struct Route { const char* name; bool t; float contact; float weight; };

inline const std::vector<Route>& Dust2Routes()
{
	// contact times from the playbook; weights follow its front/middle/back split (T 40/35/25, CT 40/20/40)
	static const std::vector<Route> r = {
		{ "long_doors",      true,  5.5f, 40.0f }, { "mid_doors",    true,  4.0f, 15.0f },
		{ "catwalk",         true,  8.0f, 20.0f }, { "upper_b",      true,  7.5f, 25.0f },
		{ "long_corner",     false, 6.0f, 40.0f }, { "short_a",      false, 8.5f, 10.0f },
		{ "mid_doors",       false, 4.0f, 10.0f }, { "b_tunnels",    false, 7.5f, 40.0f },
	};
	return r;
}

inline float StopTime(float contact, float drawTime, float buffer) { return std::max(0.0f, contact - (drawTime + buffer)); }

// ===================================================================================================================
// PART 2 - ENGINE
// ===================================================================================================================
#ifndef PB_HUMAN_PURE_ONLY

// confirmed in basebot.h: DelayedFakeClientCommand(const char*) and GetCurrentTeamIndex()
#ifndef PB_FAKE_CMD
#define PB_FAKE_CMD(bot, cmd) ((bot)->DelayedFakeClientCommand(cmd))
#endif
#ifndef PB_TEAM
#define PB_TEAM(bot) ((bot)->GetCurrentTeamIndex())   // 2 = T, 3 = CT
#endif

namespace detail
{
	// settings getter, stored by the manager on every Frame() call so the eco hooks (called from css_mod.cpp) can read pb_* settings
	inline std::function<float(const char*, float)>& Getter() { static std::function<float(const char*, float)> g; return g; }
	inline float Get(const char* name, float def) { auto& g = Getter(); return g ? g(name, def) : def; }

	struct State
	{
		bool known = false;
		bool inFreeze = false;
		bool live = false;
		float stopAt = 0.0f;
		float nextEvent = 0.0f;
		float secondAt = 0.0f;
		float fidgety = 1.0f;
		float holdSince = 0.0f;
		float anchor[3] = { 0.0f, 0.0f, 0.0f };
		float nextJiggle = 0.0f;
		float jiggleRightAt = 0.0f;
		std::string returnTo;   // classname of the weapon held before the flick
	};

	inline std::unordered_map<const void*, State>& States() { static std::unordered_map<const void*, State> s; return s; }
	inline std::mt19937& Rng() { static std::mt19937 g{ std::random_device{}() }; return g; }
	inline float Rand01() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(Rng()); }
	inline float RandRange(float a, float b) { return a + (b - a) * Rand01(); }

	// Picks a weapon to flick to (a pistol, else the knife) that the bot owns and is not holding. Fills `held` with the weapon in hand.
	// "lastinv" cannot be used: it does nothing when the bot has no previous weapon, which is the normal state at round start.
	inline const char* PickFlickWeapon(CCSSBot* bot, std::string& held)
	{
		IInventory* inventory = bot->GetInventoryInterface();
		const CBotWeapon* active = inventory->GetActiveBotWeapon();
		if (active == nullptr) { return nullptr; }

		held = active->GetClassname();
		static const char* const order[] = { "weapon_usp", "weapon_glock", "weapon_deagle", "weapon_p228", "weapon_fiveseven", "weapon_elite", "weapon_knife" };

		for (const char* name : order)
		{
			if (held == name) { continue; }
			if (inventory->FindWeaponByClassname(name) != nullptr) { return name; }
		}
		return nullptr;
	}

	inline float PickContact(bool isT)
	{
		float total = 0.0f;
		for (const Route& r : Dust2Routes()) { if (r.t == isT) { total += r.weight; } }
		float pick = Rand01() * total;
		for (const Route& r : Dust2Routes())
		{
			if (r.t != isT) { continue; }
			pick -= r.weight;
			if (pick <= 0.0f) { return r.contact; }
		}
		return 6.5f;
	}

	struct EcoState
	{
		RoundTracker tracker;
		std::array<EcoMode, 4> mode { { EcoMode::Full, EcoMode::Full, EcoMode::Full, EcoMode::Full } };
		bool decided = false;
		int detectedStart = 800;   // from round-1 cash (DetectStartMoney)
	};
	inline EcoState& Eco() { static EcoState s; return s; }

	inline EcoCfg MakeEcoCfg()
	{
		EcoCfg c;
		const int forced = static_cast<int>(Get("pb_eco_startmoney", 0.0f));   // 0 = auto-detect from round-1 cash
		c.startMoney = forced > 0 ? forced : Eco().detectedStart;
		c.readyRatio = Get("pb_eco_ready_ratio", 60.0f) / 100.0;
		return c;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Fidget timer. Called about ten times a second from CPodBotManager::Frame().
//   freeze time           -> quick-switch pairs (Q-Q) at pb_fidget_freeze_rate per minute
//   freeze end .. cutoff  -> the same at pb_fidget_run_rate per minute, until contact time - (draw time + buffer)
//   holding still >= N s  -> a tiny A-D jiggle (gun stays out)
//   visible threat / damage taken -> stop at once
// ---------------------------------------------------------------------------------------------------------------------
inline void UpdateFidget(float now, const std::function<float(const char*, float)>& get, const std::function<void(CCSSBot*, const char*)>& log)
{
	detail::Getter() = get;   // the eco hooks read settings through this

	// New map / new server: the game clock restarts at 0, but the stored timestamps and per-bot states (keyed by bot address)
	// and the round tracker are from the old map. Without this reset they sit far in the "future" and everything stays silent.
	static float lastNow = 0.0f;
	if (now + 0.5f < lastNow) { detail::States().clear(); detail::Eco() = detail::EcoState(); }
	lastNow = now;

	if (get("pb_fidget", 1.0f) < 0.5f) { return; }

	const float drawTime = get("pb_fidget_draw_time", 1.0f);
	const float buffer = get("pb_fidget_buffer", 0.15f);
	const float freezeRate = std::max(0.0f, get("pb_fidget_freeze_rate", 20.0f));
	const float runRate = std::max(0.0f, get("pb_fidget_run_rate", 6.0f));
	const bool jiggle = get("pb_fidget_jiggle", 1.0f) > 0.5f;
	const float jiggleAfter = std::max(2.0f, get("pb_fidget_jiggle_after", 8.0f));
	const bool freezeNow = csslib::IsInFreezeTime();

	auto fidgetFunc = [&](CBaseBot* b)
	{
		CCSSBot* bot = static_cast<CCSSBot*>(b);
		detail::State& st = detail::States()[bot];

		if (!bot->IsAlive()) { st = detail::State(); return; }

		const bool isT = (PB_TEAM(bot) == 2);

		if (freezeNow)
		{
			if (!st.inFreeze)
			{
				st = detail::State();
				st.known = true; st.inFreeze = true;
				st.fidgety = detail::RandRange(0.5f, 1.5f);
				st.nextEvent = now + detail::RandRange(0.4f, 1.6f);
				st.stopAt = StopTime(detail::PickContact(isT), drawTime, buffer); // relative for now, made absolute at freeze end
			}
		}
		else if (st.inFreeze)
		{
			st.inFreeze = false; st.live = true;
			st.stopAt = now + st.stopAt;
		}
		else if (!st.known)
		{
			st.known = true; st.live = true; st.stopAt = 0.0f; st.fidgety = detail::RandRange(0.5f, 1.5f);
		}

		const CKnownEntity* threat = bot->GetSensorInterface()->GetPrimaryKnownThreat(true);
		const bool busy = (threat != nullptr && threat->IsVisibleNow()) || bot->HasTakenDamageRecently(1.0f);

		if (busy)
		{
			st.secondAt = 0.0f; st.jiggleRightAt = 0.0f; st.holdSince = 0.0f;
			return;
		}

		IPlayerController* control = bot->GetControlInterface();

		if (st.secondAt > 0.0f && now >= st.secondAt)
		{
			if (!st.returnTo.empty()) { PB_FAKE_CMD(bot, (std::string("use ") + st.returnTo).c_str()); }
			st.returnTo.clear();
			st.secondAt = 0.0f;
		}

		if (st.jiggleRightAt > 0.0f && now >= st.jiggleRightAt)
		{
			control->PressMoveRightButton(0.05f);
			st.jiggleRightAt = 0.0f;
		}

		float rate = 0.0f;
		if (st.inFreeze) { rate = freezeRate; }
		else if (st.live && now < st.stopAt) { rate = runRate; }

		if (rate > 0.0f && st.secondAt == 0.0f && now >= st.nextEvent)
		{
			std::string held;
			const char* flick = detail::PickFlickWeapon(bot, held);

			if (flick != nullptr)
			{
				PB_FAKE_CMD(bot, (std::string("use ") + flick).c_str());
				st.returnTo = held;
				st.secondAt = now + detail::RandRange(0.15f, 0.40f); // demos: 41% of weapon selects come within 0.4 s of the last
				log(bot, "fidget: quick-switch pair");
			}
			st.nextEvent = now + (60.0f / (rate * st.fidgety)) * detail::RandRange(0.6f, 1.4f);
		}

		const Vector position = bot->GetAbsOrigin();
		const Vector anchor(st.anchor[0], st.anchor[1], st.anchor[2]);

		if (st.holdSince == 0.0f || (position - anchor).Length2D() > 24.0f)
		{
			st.anchor[0] = position.x; st.anchor[1] = position.y; st.anchor[2] = position.z;
			st.holdSince = now;
		}
		else if (jiggle && !st.inFreeze && now - st.holdSince >= jiggleAfter && now >= st.nextJiggle)
		{
			control->PressMoveLeftButton(0.05f);
			st.jiggleRightAt = now + 0.12f;
			st.nextJiggle = now + detail::RandRange(2.5f, 5.0f);
			log(bot, "fidget: A-D jiggle");
		}
	};

	extmanager->ForEachBot(fidgetFunc);
}

// ---------------------------------------------------------------------------------------------------------------------
// Eco connection. Hooks (added by install_human1.py to css_mod.cpp):
//   FireGameEvent "round_end"     -> EcoOnRoundEnd(winner, reason)
//   FireGameEvent "bomb_planted"  -> EcoOnBombPlanted()
//   OnRoundStart (after money)    -> EcoOnRoundStart()
//   BuyWeapons: eco-limit returns are skipped while EcoActive(); after the jason-mode check: if (EcoBuy(bot, money)) return;
// Full-buy and pistol rounds fall through to the existing buy logic. Eco and force rounds are bought here.
// ---------------------------------------------------------------------------------------------------------------------
inline bool EcoEnabled() { return detail::Get("pb_eco", 1.0f) > 0.5f; }
inline bool EcoActive() { return EcoEnabled() && detail::Eco().decided; }

inline void EcoOnBombPlanted() { detail::Eco().tracker.OnPlant(); }

inline void EcoOnRoundEnd(int winner, int reason)
{
	if (!EcoEnabled()) { return; }
	auto& s = detail::Eco();
	s.tracker.halfRounds = static_cast<int>(detail::Get("pb_eco_half_rounds", 15.0f));
	s.tracker.OnRoundEnd(winner, reason);
	s.decided = false;
}

inline void EcoOnRoundStart()
{
	if (!EcoEnabled()) { return; }

	auto& s = detail::Eco();
	s.tracker.halfRounds = static_cast<int>(detail::Get("pb_eco_half_rounds", 15.0f));
	s.tracker.plantThisRound = false;

	std::array<std::vector<int>, 4> cash;

	auto func = [&cash](int client, edict_t* entity, SourceMod::IGamePlayer* player)
	{
		if (player->IsInGame() && player->GetPlayerInfo() != nullptr)
		{
			const int team = player->GetPlayerInfo()->GetTeamIndex();

			if (team == 2 || team == 3)
			{
				CBaseEntity* pEntity = UtilHelpers::EdictToBaseEntity(entity);
				cash[team].push_back(csslib::GetPlayerMoney(pEntity));
			}
		}
	};

	UtilHelpers::ForEachPlayer(func);

	{
		double total = 0.0; int n = 0;
		if (s.tracker.RoundInHalf() == 1)
		{
			for (int t = 2; t <= 3; ++t) { for (int v : cash[t]) { total += v; ++n; } }
			if (n > 0)
			{
				s.detectedStart = DetectStartMoney(total / n);
#ifdef META_CONPRINTF
				if (detail::Get("pb_eco_debug", 0.0f) > 0.5f) { META_CONPRINTF("[podbot eco] round 1 avg cash $%d -> mp_startmoney %d\n", static_cast<int>(total / n), s.detectedStart); }
#endif
			}
		}
	}

	const EcoCfg cfg = detail::MakeEcoCfg();
	const int round = s.tracker.RoundInHalf();

	for (int team = 2; team <= 3; ++team)
	{
		const bool isT = (team == 2);
		const TeamDecision d = TeamShouldEco(cfg, isT, cash[team], round, isT && s.tracker.pistolPlant);
		s.mode[team] = d.mode;

#ifdef META_CONPRINTF
		if (detail::Get("pb_eco_debug", 0.0f) > 0.5f && !cash[team].empty())
		{
			double sum = 0.0; for (int v : cash[team]) { sum += v; }
			META_CONPRINTF("[podbot eco] round %d %s: %s (%s) avg $%d, %d players, %d losses in a row\n", round, isT ? "T" : "CT", ModeName(d.mode),
				d.reason.c_str(), static_cast<int>(sum / cash[team].size()), static_cast<int>(cash[team].size()), s.tracker.losses[team]);
		}
#endif
	}

	s.decided = true;
}

// Returns true if this bot's purchases were handled here (the normal buy logic is then skipped for it).
inline bool EcoBuy(CCSSBot* bot, int& money)
{
	if (!EcoActive()) { return false; }

	auto& s = detail::Eco();
	const int team = static_cast<int>(bot->GetMyCSSTeam());
	if (team < 2 || team > 3) { return false; }

	const EcoMode mode = s.mode[team];
	if (mode == EcoMode::Pistol || mode == EcoMode::Full) { return false; }

	const EcoCfg cfg = detail::MakeEcoCfg();
	const bool isT = (team == 2);
	std::vector<std::string> items;

	if (mode == EcoMode::Eco)
	{
		items = PlayerEcoBuy(cfg, isT, money, s.tracker.losses[team], true, false).items;
	}
	else
	{
		items = ForceBuyItems(cfg, money);
	}

	for (const std::string& item : items)
	{
		const int price = ItemPrice(cfg, item);

		if (money >= price)
		{
			bot->SendBuyCommand(BuyAlias(item));
			money -= price;
		}
	}

	return true;
}

#endif // PB_HUMAN_PURE_ONLY

}} // namespace podbot::human
