#ifndef NAVBOT_PODBOT_MANAGER_H_
#define NAVBOT_PODBOT_MANAGER_H_

#include <array>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "podbot_data.h"

class CBaseBot;
class CCSSBot;
class CNavArea;
class CKnownEntity;
class CBotWeapon;
class CBaseEntity;

namespace podbot
{
	// Voice command id the manager sends to bots that must follow a player (see CCSSBotMainTask::OnVoiceCommand)
	constexpr int kVoiceFollowMe = 0x50420001;

	// PODBot-style layer for the Counter-Strike: Source bot (skills, personalities, names, chat).
	// Buy decisions for one PODBot-managed bot (weapon lists are best first).
	struct BuyPlan
	{
		std::vector<std::string> primaries;
		std::vector<std::string> secondaries;
		bool jasonMode = false;   // pb_jasonmode: buy nothing, knife only
		bool noArmor = false, noFlash = false, noHE = false, noSmoke = false, noDefuser = false;
	};

	class CPodBotManager
	{
	public:
		// (Re)loads every file from addons/sourcemod/configs/navbot/podbot/. Returns false on any error.
		bool Load();
		bool IsLoaded() const { return m_loaded; }
		const Settings& GetSettings() const { return m_settings; }

		// skill < 0 picks a random skill in [pb_minbotskill, pb_maxbotskill]; personality 0 picks randomly.
		// team: 1 = T, 2 = CT, 5 or 0 = automatic (or forced by pb_bot_join_team). model: class 1-4, 0 = random (or forced by pb_skin).
		void AddBot(int skill = -1, int personality = 0, const std::string& name = std::string(), int team = 0, int model = 0);
		// Adds bots until pb_maxbots (or maxClients - 1) is reached.
		void FillServer(int skill = -1, int personality = 0, int team = 0, int model = 0);
		// Runs the "pb add ..." lines found in podbot.cfg.
		void RunStartupCommands();

		// Fills the buy plan for a bot added through PODBot. Returns false for any other bot.
		bool GetBuyPlan(CCSSBot* bot, BuyPlan& plan);

		// Called by the CS:S mod: reloads the config files and restarts the startup timer.
		void OnMapStart();
		// Called by the CS:S mod at intervals: runs the "pb add" lines after pb_mapstartbotdelay and keeps the
		// bot count between pb_minbots and pb_maxbots (one bot leaves for each joining human, and returns when they leave).
		void Update();

		// A human said something: a bot may answer after a short typing delay ([REPLIES] and [UNKNOWN] in botchat.txt).
		void OnPlayerSay(int userid, const char* text);

		// Experience memory (pb_dangerfactor): bots remember where their team got killed and route around it.
		// It is saved per map in data/navbot/podbot/<map>.exp. Delete the file after regenerating a nav mesh.
		void OnBotKilled(CCSSBot* bot);
		float GetExperienceCost(CCSSBot* bot, CNavArea* area) const;
		void OnRoundEnd();
		void OnMapEnd();

		// pb_maxcamptime: longest time (seconds) a PODBot-managed bot stays camping in one place.
		float CapCampTime(CCSSBot* bot, float seconds);

		// Map hints converted from PODBot waypoints (camp spots): picks one near (x, y) for this bot's team.
		// Returns false when there is none, hints are off (pb_use_hints 0) or the bot is not managed by PODBot.
		bool PickCampSpot(CCSSBot* bot, float x, float y, float maxDist, float out[3]);

		// pb_ffa: bots treat everybody as an enemy (needs mp_friendlyfire 1 on the server to do any damage to teammates).
		bool IsFreeForAll(CCSSBot* bot);

		// pb_shootthruwalls: true when a bot may fire at an enemy it cannot see (last seen or heard behind an obstacle).
		// The chance comes from SEEN_SHOOTTHRU / HEAR_SHOOTTHRU in botskill.cfg for the bot's skill.
		bool ShouldShootThrough(CCSSBot* bot, const CKnownEntity* threat, const CBotWeapon* weapon);

		// A human used the "Follow Me" radio command: up to pb_numfollowuser bots of their team follow them.
		void OnFollowMe(int clientIndex);
		float GetFollowDuration() const { return m_settings.GetFloat("pb_followtime", 60.0f); }

		// Chat events
		void OnOtherKilled(CCSSBot* bot, CBaseEntity* victim, CBaseEntity* attacker);
		void OnBombPlanted(CCSSBot* bot);

		// Radio events (pb_radio): enemy down, taking fire / need backup, enemy spotted
		void OnInjured(CCSSBot* bot);
		void OnSight(CCSSBot* bot, CBaseEntity* subject);

	private:
		struct BotInfo { int followClient = 0; float followUntil = 0.0f; float nextWallbangRoll = 0.0f; bool wallbang = false; const void* profilePtr = nullptr; int desiredTeam = 0; int desiredClass = 0; int teamAttempts = 0; float nextTeamAttempt = 0.0f; int skill = 100; Personality personality = Personality::Normal; float nextChatTime = 0.0f; float nextDeadChat = 0.0f; float nextRadio = 0.0f; };
		struct PendingSay { const CBaseBot* bot; float time; std::string msg; };

		const void* ApplyProfile(CBaseBot* bot, int skill, Personality p); // returns the profile that was set
		void ReapplyProfiles();
		BotInfo* Find(const CBaseBot* bot);
		bool ChatAllowed(const CBaseBot* bot, BotInfo& info, const char* chanceKey, int defaultChance);
		ChatContext MakeContext(const CCSSBot* bot, const std::string& victimName);
		void Say(CBaseBot* bot, std::string msg);
		bool RadioAllowed(BotInfo& info, int chance);
		void UpdateAutoKill(float now);
		void UpdateTeams(float now);
		float m_humansDeadSince = 0.0f;

		bool m_loaded = false;
		Settings m_settings;
		SkillTable m_skills;
		NameList m_names;
		WeaponTables m_weapons;
		ChatDB m_chat;
		Rng m_rng;
		std::unordered_map<const CBaseBot*, BotInfo> m_info;
		float m_mapStartTime = 0.0f;
		float m_nextUpdate = 0.0f;
		bool m_startupDone = false;
		int m_lastHumans = -1;
		int m_kicked = 0; // bots removed to make room for humans
		std::vector<PendingSay> m_pending;
		HintTable m_hints;
		std::unordered_map<unsigned int, std::array<float, 2>> m_experience; // nav area id -> deaths for T, CT
		std::string m_experienceMap;
		void LoadExperience();
		void SaveExperience() const;
		bool IsLive(const CBaseBot* bot) const;
		std::string RandomPlayerName(int excludeIndex);
		void ProcessChat(float now);
	};

	CPodBotManager& PodBot();
}

#endif // !NAVBOT_PODBOT_MANAGER_H_
