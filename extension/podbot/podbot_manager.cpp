#include NAVBOT_PCH_FILE
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>

#include <extension.h>
#include <manager.h>
#include <util/helpers.h>
#include <bot/basebot.h>
#include <bot/css/cssbot.h>
#include <bot/interfaces/profile.h>
#include <bot/interfaces/playercontrol.h>
#include "podbot_manager.h"
#include "podbot_aim.h"

namespace podbot
{
	CPodBotManager& PodBot()
	{
		static CPodBotManager s_instance;
		return s_instance;
	}

	// Replaces NavBot's capped-speed turn with the PODBot spring-damper aim and skill based aim error.
	class PodAimOverride final : public IAimOverride
	{
	public:
		PodAimOverride(const AimParams& params, const SkillBracket& bracket, unsigned seed) :
			m_params(params), m_bracket(bracket), m_aim(params, seed), m_rng(seed * 7u + 1u)
		{
			// botskill.cfg gives degrees per GoldSrc frame; assume ~60 frames per second (tunable)
			m_maxTurn = m_rng.Float(bracket.minTurn, bracket.maxTurn) * 60.0f;
		}

		Vector AdjustTarget(CBaseBot* bot, const Vector& target, bool isEntity) override
		{
			if (!isEntity)
			{
				return target;
			}

			if (gpGlobals->curtime >= m_nextOffsetTime)
			{
				float o[3];
				SkillAimOffset(m_bracket, m_rng, o);
				m_offset = Vector(o[0], o[1], o[2]);
				m_nextOffsetTime = gpGlobals->curtime + std::max(0.2f, m_params.offsetDelay * 0.5f);
			}

			return target + m_offset;
		}

		bool Turn(CBaseBot* bot, const QAngle& current, const QAngle& desired, bool hasTarget, float distance, float dt, QAngle& out) override
		{
			m_aim.SyncCurrent(Angles{ current.x, current.y });
			const Angles result = m_aim.Update(Angles{ desired.x, desired.y }, hasTarget, m_maxTurn, distance, dt);
			out.x = result.pitch;
			out.y = result.yaw;
			out.z = 0.0f;
			return true;
		}

	private:
		AimParams m_params;
		SkillBracket m_bracket;
		SpringAim m_aim;
		Rng m_rng;
		float m_maxTurn = 0.0f;
		float m_nextOffsetTime = 0.0f;
		Vector m_offset = Vector(0.0f, 0.0f, 0.0f);
	};

	static std::string ConfigPath(const char* file)
	{
		char path[PLATFORM_MAX_PATH]{};
		std::string rel = std::string("configs/navbot/podbot/") + file;
		smutils->BuildPath(SourceMod::PathType::Path_SM, path, sizeof(path), "%s", rel.c_str());
		return path;
	}

	bool CPodBotManager::Load()
	{
		m_loaded = false;
		std::string err;

		if (!m_settings.Load(ConfigPath("podbot.cfg"), &err) ||
			!m_skills.Load(ConfigPath("botskill.cfg"), &err) ||
			!m_names.Load(ConfigPath("botnames.txt"), &err) ||
			!m_weapons.Load(ConfigPath("botweapons.cfg"), &err) ||
			!m_chat.Load(ConfigPath("botchat.txt"), &err))
		{
			smutils->LogError(myself, "[PODBot] Failed to load configuration: %s", err.c_str());
			return false;
		}

		m_loaded = true;

		for (auto& entry : m_info)
		{
			entry.second.profilePtr = nullptr; // ReapplyProfiles() then rebuilds each bot's skill and aim with the new settings
		}

		rootconsole->ConsolePrint("[PODBot] Loaded: %i names, %i chat replies.", static_cast<int>(m_names.Size()), static_cast<int>(m_chat.Replies().size()));
		return true;
	}

	const void* CPodBotManager::ApplyProfile(CBaseBot* bot, int skill, Personality p)
	{
		const SkillBracket& b = m_skills.ForSkill(skill);
		auto profile = std::make_shared<DifficultyProfile>();

		profile->SetGameAwareness(skill);
		profile->SetWeaponSkill(skill);
		// botskill.cfg stores degrees per GoldSrc frame; assume ~60 frames per second (tunable).
		profile->SetAimSpeed((b.minTurn + b.maxTurn) * 0.5f * 60.0f);
		profile->SetMinRecognitionTime(std::max(0.01f, m_rng.Float(b.minDelay, b.maxDelay)));
		profile->SetAllowHeadshots(m_rng.Int(0, 99) < b.headshot);

		switch (p)
		{
		case Personality::Aggressive:
			profile->SetAggressiveness(80);
			profile->SetTeamwork(20);
			profile->SetHealthCriticalThreshold(0.10f);
			profile->SetHealthLowThreshold(0.30f);
			break;
		case Personality::Defensive:
			profile->SetAggressiveness(10);
			profile->SetTeamwork(60);
			profile->SetHealthCriticalThreshold(0.25f);
			profile->SetHealthLowThreshold(0.60f);
			break;
		default:
			profile->SetAggressiveness(40);
			profile->SetTeamwork(40);
			break;
		}

		profile->ValidateProfileValues();
		bot->SetDifficultyProfile(profile);

		// PODBot aim: spring-damper turn + skill based aim offsets
		const unsigned seed = static_cast<unsigned>(m_rng.Int(1, 1 << 30));
		bot->GetControlInterface()->SetAimOverride(std::make_shared<PodAimOverride>(AimParams::FromSettings(m_settings), b, seed));
		return profile.get();
	}

	void CPodBotManager::CountPlayers(int& humans, int& bots) const
	{
		humans = 0;
		bots = 0;

		for (int i = 1; i <= gpGlobals->maxClients; i++)
		{
			SourceMod::IGamePlayer* gp = playerhelpers->GetGamePlayer(i);

			if (gp == nullptr || !gp->IsInGame() || gp->IsSourceTV())
			{
				continue;
			}

			if (gp->IsFakeClient())
			{
				bots++;
			}
			else
			{
				humans++;
			}
		}
	}

	bool CPodBotManager::NameInUse(const std::string& name) const
	{
		for (int i = 1; i <= gpGlobals->maxClients; i++)
		{
			SourceMod::IGamePlayer* gp = playerhelpers->GetGamePlayer(i);

			if (gp != nullptr && gp->IsInGame() && gp->GetName() != nullptr && CleanPlayerName(gp->GetName()) == name)
			{
				return true;
			}
		}

		return false;
	}

	void CPodBotManager::AddBot(int skill, int personality, const std::string& name, int team, int model)
	{
		if (!m_loaded && !Load())
		{
			return;
		}

		// never fill the last slot: a human must always be able to join (the old behaviour added bots and then kicked them)
		int humans = 0;
		int bots = 0;
		CountPlayers(humans, bots);

		if (humans + bots >= gpGlobals->maxClients - 1)
		{
			rootconsole->ConsolePrint("[PODBot] Not adding a bot: the server has no free slot left (it keeps one for a player).");
			return;
		}

		if (skill < 0)
		{
			int lo = std::max(0, std::min(100, m_settings.GetInt("pb_minbotskill", 95)));
			int hi = std::max(lo, std::min(100, m_settings.GetInt("pb_maxbotskill", 100)));
			skill = m_rng.Int(lo, hi);
		}

		skill = std::max(0, std::min(100, skill));
		Personality p = (personality >= 1 && personality <= 3) ? static_cast<Personality>(personality) : static_cast<Personality>(m_rng.Int(1, 3));
		std::string base = name;

		if (base.empty())
		{
			// avoid two bots with the same name (Source would show "(1)" in front of the second one)
			for (int attempt = 0; attempt < 40; attempt++)
			{
				base = m_names.Pick(m_rng);

				if (!NameInUse(base))
				{
					break;
				}
			}
		}
		std::string full = NameList::Format(base, p, skill, m_settings.GetInt("pb_detailnames", 1));

		edict_t* edict = nullptr;
		extmanager->AddBot(&full, &edict);

		if (edict == nullptr)
		{
			return;
		}

		CBaseBot* bot = extmanager->GetBotByIndex(gamehelpers->IndexOfEdict(edict));

		if (bot == nullptr)
		{
			return;
		}

		const void* applied = ApplyProfile(bot, skill, p);
		BotInfo info;
		info.profilePtr = applied;
		info.skill = skill;
		info.personality = p;

		// PODBot team numbers: 1 = T, 2 = CT, 5 = auto. CS:S team numbers: 2 = T, 3 = CT.
		const std::string forced = m_settings.GetString("pb_bot_join_team", "ANY");

		if (team == 1 || team == 2)
		{
			info.desiredTeam = team + 1;
		}
		else if (forced == "T" || forced == "t")
		{
			info.desiredTeam = 2;
		}
		else if (forced == "CT" || forced == "ct")
		{
			info.desiredTeam = 3;
		}

		const int skin = m_settings.GetInt("pb_skin", 5);
		info.desiredClass = (skin >= 1 && skin <= 4) ? skin : ((model >= 1 && model <= 4) ? model : 0);
		info.nextTeamAttempt = gpGlobals->curtime + 3.0f; // give NavBot's own join logic time first
		m_info[bot] = info;
	}

	void CPodBotManager::FillServer(int skill, int personality, int team, int model)
	{
		int maxbots = m_settings.GetInt("pb_maxbots", 16);

		if (maxbots <= 0 || maxbots > gpGlobals->maxClients - 1)
		{
			maxbots = gpGlobals->maxClients - 1;
		}

		int players = 0;

		for (int i = 1; i <= gpGlobals->maxClients; i++)
		{
			SourceMod::IGamePlayer* gp = playerhelpers->GetGamePlayer(i);

			if (gp != nullptr && gp->IsInGame())
			{
				players++;
			}
		}

		for (int i = players; i < maxbots; i++)
		{
			AddBot(skill, personality, std::string(), team, model);
		}
	}

	void CPodBotManager::OnMapStart()
	{
		Load();
		m_info.clear();
		m_mapStartTime = gpGlobals->curtime;
		m_nextUpdate = 0.0f;
		m_startupDone = false;
		m_lastHumans = -1;
		m_kicked = 0;
		m_pending.clear();
		m_experienceMap = gamehelpers->GetCurrentMap();
		LoadExperience();
		m_hints = HintTable();
		m_hints.Load(ConfigPath((std::string("hints/") + m_experienceMap + ".hint").c_str())); // no file is normal
	}

	void CPodBotManager::Update()
	{
		if (!m_loaded)
		{
			return;
		}

		const float now = gpGlobals->curtime;
		ProcessChat(now);

		if (now < m_nextUpdate)
		{
			return;
		}

		m_nextUpdate = now + 1.0f;

		if (!m_startupDone)
		{
			if (now >= m_mapStartTime + m_settings.GetFloat("pb_mapstartbotdelay", 10.0f))
			{
				m_startupDone = true;
				RunStartupCommands();
			}

			return;
		}

		// forget bots that left the game
		std::unordered_set<const CBaseBot*> live;
		auto collect = [&live](CBaseBot* bot) { live.insert(bot); };
		extmanager->ForEachBot(collect);

		for (auto it = m_info.begin(); it != m_info.end();)
		{
			it = live.count(it->first) ? std::next(it) : m_info.erase(it);
		}

		ReapplyProfiles();
		UpdateAutoKill(now);
		UpdateTeams(now);

		const int maxbots = m_settings.GetInt("pb_maxbots", 16);

		if (maxbots <= 0) // 0 disables the automatic add and kick
		{
			return;
		}

		int humans = 0;

		for (int i = 1; i <= gpGlobals->maxClients; i++)
		{
			SourceMod::IGamePlayer* gp = playerhelpers->GetGamePlayer(i);

			if (gp != nullptr && gp->IsInGame() && !gp->IsFakeClient() && !gp->IsSourceTV())
			{
				humans++;
			}
		}

		const int bots = static_cast<int>(live.size());
		const int minbots = std::max(0, m_settings.GetInt("pb_minbots", 0));
		const int limit = std::min(maxbots, gpGlobals->maxClients - 1); // always leave one slot for a human
		const int match = m_settings.GetInt("pb_bot_quota_match", 0);

		if (match > 0)
		{
			// bots follow the number of humans (humans * pb_bot_quota_match), inside the min/max limits
			const int target = std::max(minbots, std::min(limit, humans * match));

			if (bots < target)
			{
				AddBot();
			}
			else if (bots > target)
			{
				extmanager->RemoveRandomBot("PODBot quota");
			}

			m_lastHumans = humans;
			return;
		}

		if (m_lastHumans < 0)
		{
			m_lastHumans = humans;
		}

		if (humans > m_lastHumans && bots > minbots)
		{
			extmanager->RemoveRandomBot("Making room for a player");
			m_kicked++;
			m_lastHumans++;
			return;
		}

		if (humans < m_lastHumans)
		{
			if (m_kicked > 0 && humans + bots < limit)
			{
				AddBot();
				m_kicked--;
			}

			m_lastHumans--;
			return;
		}

		m_lastHumans = humans;

		if (humans + bots > gpGlobals->maxClients - 1 && bots > minbots) // no free slot left for a player
		{
			extmanager->RemoveRandomBot("Server is full");
		}
	}

	void CPodBotManager::RunStartupCommands()
	{
		if (!m_loaded && !Load())
		{
			return;
		}

		for (const auto& cmd : m_settings.Commands())
		{
			if (cmd.verb == "add")
			{
				int skill = cmd.args.size() > 0 ? std::atoi(cmd.args[0].c_str()) : -1;
				int pers = cmd.args.size() > 1 ? std::atoi(cmd.args[1].c_str()) : 0;
				const int team = cmd.args.size() > 2 ? std::atoi(cmd.args[2].c_str()) : 0;
				const int model = cmd.args.size() > 3 ? std::atoi(cmd.args[3].c_str()) : 0;
				std::string name = cmd.args.size() > 4 ? cmd.args[4] : std::string();
				AddBot(skill, pers, name, team, model);
			}
			else if (cmd.verb == "fillserver")
			{
				FillServer(cmd.args.size() > 0 ? std::atoi(cmd.args[0].c_str()) : -1, cmd.args.size() > 1 ? std::atoi(cmd.args[1].c_str()) : 0,
					cmd.args.size() > 2 ? std::atoi(cmd.args[2].c_str()) : 0, cmd.args.size() > 3 ? std::atoi(cmd.args[3].c_str()) : 0);
			}
		}
	}

	bool CPodBotManager::GetBuyPlan(CCSSBot* bot, BuyPlan& plan)
	{
		BotInfo* info = Find(bot);

		if (info == nullptr || !m_loaded)
		{
			return false;
		}

		plan = BuyPlan();
		plan.jasonMode = m_settings.GetBool("pb_jasonmode", false);

		const std::string equip = m_settings.GetString("pb_restrequipammo");
		auto restrictedEquip = [&equip](size_t i) { return i < equip.size() && equip[i] == '1'; };
		plan.noArmor = restrictedEquip(1);
		plan.noFlash = restrictedEquip(2);
		plan.noHE = restrictedEquip(3);
		plan.noSmoke = restrictedEquip(4);
		plan.noDefuser = restrictedEquip(5);

		const int team = bot->GetMyCSSTeam() == counterstrikesource::CSSTeam::TERRORIST ? 0 : 1;
		const std::string restricted = m_settings.GetString("pb_restrweapons");
		const std::string map = gamehelpers->GetCurrentMap();
		const char* table = map.rfind("as_", 0) == 0 ? "AS" : "STANDARD";
		const WeaponRow& ranking = m_weapons.Ranking(info->personality); // least to most desired

		for (int i = kNumWeapons - 1; i >= 0; --i)
		{
			const int w = ranking[i];

			if (w == 0 || w == 25) // knife; shield does not exist in CS:S
			{
				continue;
			}

			if (!m_weapons.CanBuy(table, w, team))
			{
				continue;
			}

			if (static_cast<size_t>(w) < restricted.size() && restricted[w] == '1')
			{
				continue;
			}

			if (w >= 1 && w <= 6)
			{
				plan.secondaries.push_back(kWeaponNames[w]);
			}
			else
			{
				plan.primaries.push_back(kWeaponNames[w]);
			}
		}

		return true;
	}

	CPodBotManager::BotInfo* CPodBotManager::Find(const CBaseBot* bot)
	{
		auto it = m_info.find(bot);
		return it == m_info.end() ? nullptr : &it->second;
	}

	bool CPodBotManager::ChatAllowed(const CBaseBot* bot, BotInfo& info, const char* chanceKey, int defaultChance)
	{
		if (!m_loaded || !m_settings.GetBool("pb_chat", true))
		{
			return false;
		}

		const float now = gpGlobals->curtime;

		if (now < info.nextChatTime)
		{
			return false;
		}

		if (m_rng.Int(0, 99) >= m_settings.GetInt(chanceKey, defaultChance))
		{
			return false;
		}

		info.nextChatTime = now + m_settings.GetFloat("pb_chat_cooldown", 8.0f);
		return true;
	}

	static std::string PlayerName(CBaseEntity* entity)
	{
		if (entity == nullptr)
		{
			return std::string();
		}

		SourceMod::IGamePlayer* gp = playerhelpers->GetGamePlayer(UtilHelpers::IndexOfEntity(entity));
		return (gp != nullptr && gp->GetName() != nullptr) ? std::string(gp->GetName()) : std::string();
	}

	ChatContext CPodBotManager::MakeContext(const CCSSBot* bot, const std::string& victimName)
	{
		ChatContext c;
		c.lastVictim = CleanPlayerName(victimName);
		c.mapName = gamehelpers->GetCurrentMap();
		c.timeLeft = "some time"; // TODO: read the real round timer
		std::vector<std::string> mates;

		for (int i = 1; i <= gpGlobals->maxClients; i++)
		{
			SourceMod::IGamePlayer* gp = playerhelpers->GetGamePlayer(i);

			if (gp == nullptr || !gp->IsInGame() || i == bot->GetIndex())
			{
				continue;
			}

			CBaseExtPlayer other(gp->GetEdict());

			if (other.IsAlive() && csslib::GetEntityCSSTeam(other.GetEntity()) == bot->GetMyCSSTeam())
			{
				mates.push_back(CleanPlayerName(gp->GetName()));
			}
		}

		c.aliveMate = mates.empty() ? std::string("everyone") : mates[m_rng.Index(mates.size())];
		c.topFragger = c.aliveMate; // TODO: use the real scoreboard leader
		c.replyTo = CleanPlayerName(victimName);
		return c;
	}

	void CPodBotManager::Say(CBaseBot* bot, std::string msg)
	{
		// the message goes through a fake client command: keep ';' and '"' from breaking it
		std::replace(msg.begin(), msg.end(), ';', ',');
		std::replace(msg.begin(), msg.end(), '"', '\'');
		bot->SendChatMessage(msg.c_str());
	}

	bool CPodBotManager::IsLive(const CBaseBot* bot) const
	{
		bool found = false;
		auto check = [&found, bot](CBaseBot* b) { if (b == bot) { found = true; } };
		extmanager->ForEachBot(check);
		return found;
	}

	std::string CPodBotManager::RandomPlayerName(int excludeIndex)
	{
		std::vector<std::string> names;

		for (int i = 1; i <= gpGlobals->maxClients; i++)
		{
			SourceMod::IGamePlayer* gp = playerhelpers->GetGamePlayer(i);

			if (gp != nullptr && gp->IsInGame() && i != excludeIndex && !gp->IsSourceTV())
			{
				names.push_back(gp->GetName());
			}
		}

		return names.empty() ? std::string("everyone") : CleanPlayerName(names[m_rng.Index(names.size())]);
	}

	// Sends delayed replies and lets dead bots chat ([DEADCHAT]).
	void CPodBotManager::ProcessChat(float now)
	{
		for (auto it = m_pending.begin(); it != m_pending.end();)
		{
			if (it->time > now)
			{
				++it;
				continue;
			}

			if (IsLive(it->bot))
			{
				Say(const_cast<CBaseBot*>(it->bot), it->msg);
			}

			it = m_pending.erase(it);
		}

		if (!m_settings.GetBool("pb_chat", true))
		{
			return;
		}

		std::vector<CCSSBot*> deadBots;
		auto collect = [this, &deadBots, now](CBaseBot* b)
		{
			BotInfo* info = Find(b);

			if (info == nullptr)
			{
				return;
			}

			if (b->IsAlive())
			{
				info->nextDeadChat = 0.0f;
			}
			else if (info->nextDeadChat == 0.0f)
			{
				info->nextDeadChat = now + m_rng.Float(10.0f, 30.0f); // first line a while after dying
			}
			else if (now >= info->nextDeadChat)
			{
				deadBots.push_back(static_cast<CCSSBot*>(b));
			}
		};
		extmanager->ForEachBot(collect);

		for (CCSSBot* bot : deadBots)
		{
			BotInfo* info = Find(bot);
			info->nextDeadChat = now + m_rng.Float(15.0f, 40.0f);

			if (m_rng.Int(0, 99) < m_settings.GetInt("pb_chat_dead_chance", 50) && now >= info->nextChatTime)
			{
				info->nextChatTime = now + m_settings.GetFloat("pb_chat_cooldown", 8.0f);
				Say(bot, m_chat.PickDeadChat(m_rng, MakeContext(bot, RandomPlayerName(bot->GetIndex()))));
			}
		}
	}

	void CPodBotManager::OnPlayerSay(int userid, const char* text)
	{
		if (!m_loaded || text == nullptr || *text == '\0' || !m_settings.GetBool("pb_chat", true))
		{
			return;
		}

		SourceMod::IGamePlayer* speaker = playerhelpers->GetGamePlayer(playerhelpers->GetClientOfUserId(userid));

		if (speaker == nullptr || !speaker->IsInGame() || speaker->IsFakeClient()) // bots do not answer each other
		{
			return;
		}

		if (m_rng.Int(0, 99) >= m_settings.GetInt("pb_chat_reply_chance", 25))
		{
			return;
		}

		const float now = gpGlobals->curtime;
		std::vector<CCSSBot*> candidates;
		auto collect = [this, &candidates, now](CBaseBot* b)
		{
			BotInfo* info = Find(b);

			if (info != nullptr && now >= info->nextChatTime)
			{
				candidates.push_back(static_cast<CCSSBot*>(b));
			}
		};
		extmanager->ForEachBot(collect);

		if (candidates.empty())
		{
			return;
		}

		CCSSBot* bot = candidates[m_rng.Index(candidates.size())];
		BotInfo* info = Find(bot);
		const std::string reply = m_chat.Reply(text, m_rng, MakeContext(bot, speaker->GetName()));

		if (reply.empty())
		{
			return;
		}

		info->nextChatTime = now + m_settings.GetFloat("pb_chat_cooldown", 8.0f);
		const float typing = 1.0f + 0.06f * static_cast<float>(std::strlen(text)) + m_rng.Float(0.0f, 1.5f);
		m_pending.push_back({ bot, now + typing, reply });
	}

	// pb_autokill: when every human is dead, bots are killed after pb_autokilldelay seconds so rounds do not drag on.
	void CPodBotManager::UpdateAutoKill(float now)
	{
		if (!m_settings.GetBool("pb_autokill", false))
		{
			m_humansDeadSince = 0.0f;
			return;
		}

		int playing = 0;
		int alive = 0;

		for (int i = 1; i <= gpGlobals->maxClients; i++)
		{
			SourceMod::IGamePlayer* gp = playerhelpers->GetGamePlayer(i);

			if (gp == nullptr || !gp->IsInGame() || gp->IsFakeClient() || gp->IsSourceTV())
			{
				continue;
			}

			CBaseExtPlayer player(gp->GetEdict());
			const auto team = csslib::GetEntityCSSTeam(player.GetEntity());

			if (team != counterstrikesource::CSSTeam::TERRORIST && team != counterstrikesource::CSSTeam::COUNTERTERRORIST)
			{
				continue;
			}

			playing++;

			if (player.IsAlive())
			{
				alive++;
			}
		}

		if (playing == 0 || alive > 0)
		{
			m_humansDeadSince = 0.0f;
			return;
		}

		if (m_humansDeadSince == 0.0f)
		{
			m_humansDeadSince = now;
			return;
		}

		if (now - m_humansDeadSince < m_settings.GetFloat("pb_autokilldelay", 45.0f))
		{
			return;
		}

		auto killBot = [this](CBaseBot* bot)
		{
			if (Find(bot) != nullptr && bot->IsAlive())
			{
				bot->FakeClientCommand("kill");
			}
		};
		extmanager->ForEachBot(killBot);
		m_humansDeadSince = 0.0f;
	}

	// NavBot replaces every bot's difficulty profile when its own difficulty setting changes: put the PODBot skill back.
	void CPodBotManager::ReapplyProfiles()
	{
		auto check = [this](CBaseBot* bot)
		{
			BotInfo* info = Find(bot);

			if (info != nullptr && &*bot->GetDifficultyProfile() != info->profilePtr)
			{
				info->profilePtr = ApplyProfile(bot, info->skill, info->personality);
			}
		};
		extmanager->ForEachBot(check);
	}

	// Moves bots to the team and class asked for in pb add / pb_bot_join_team / pb_skin (a few tries, then gives up).
	void CPodBotManager::UpdateTeams(float now)
	{
		auto fix = [this, now](CBaseBot* b)
		{
			BotInfo* info = Find(b);

			if (info == nullptr || info->desiredTeam == 0 || info->teamAttempts >= 4 || now < info->nextTeamAttempt)
			{
				return;
			}

			CCSSBot* bot = static_cast<CCSSBot*>(b);

			if (static_cast<int>(bot->GetMyCSSTeam()) == info->desiredTeam)
			{
				info->desiredTeam = 0; // done
				return;
			}

			char command[32];
			ke::SafeSprintf(command, sizeof(command), "jointeam %d", info->desiredTeam);
			bot->FakeClientCommand(command);
			ke::SafeSprintf(command, sizeof(command), "joinclass %d", info->desiredClass);
			bot->FakeClientCommand(command);
			info->teamAttempts++;
			info->nextTeamAttempt = now + 3.0f;
		};
		extmanager->ForEachBot(fix);
	}

	bool CPodBotManager::PickCampSpot(CCSSBot* bot, float x, float y, float maxDist, float out[3])
	{
		if (!m_loaded || !m_settings.GetBool("pb_use_hints", true) || Find(bot) == nullptr)
		{
			return false;
		}

		Hint hint;

		if (!m_hints.Pick("camp", x, y, maxDist, static_cast<int>(bot->GetMyCSSTeam()), m_rng, hint))
		{
			return false;
		}

		out[0] = hint.pos[0];
		out[1] = hint.pos[1];
		out[2] = hint.pos[2];
		return true;
	}

	void CPodBotManager::OnFireAtEnemy(CCSSBot* bot, const CKnownEntity* threat, const CBotWeapon* weapon)
	{
		const int chance = m_settings.GetInt("pb_crouch_chance", 35);

		if (!m_loaded || chance <= 0 || threat == nullptr || weapon == nullptr || !threat->IsVisibleNow())
		{
			return;
		}

		BotInfo* info = Find(bot);

		if (info == nullptr || weapon->GetWeaponInfo()->GetSlot() != counterstrikesource::CSS_WEAPON_SLOT_PRIMARY)
		{
			return;
		}

		const float now = gpGlobals->curtime;

		if (now >= info->nextCrouchRoll) // decide again every couple of seconds
		{
			info->crouching = bot->GetRangeTo(threat->GetEntity()) >= 700.0f && m_rng.Int(0, 99) < chance;
			info->nextCrouchRoll = now + m_rng.Float(1.5f, 3.5f);
		}

		if (info->crouching)
		{
			bot->GetControlInterface()->PressCrouchButton(0.2f);
		}
	}

	bool CPodBotManager::ShouldShootThrough(CCSSBot* bot, const CKnownEntity* threat, const CBotWeapon* weapon)
	{
		if (!m_loaded || threat == nullptr || weapon == nullptr || threat->IsVisibleNow() || !m_settings.GetBool("pb_shootthruwalls", false))
		{
			return false;
		}

		BotInfo* info = Find(bot);

		if (info == nullptr)
		{
			return false;
		}

		// only primary weapons that can penetrate (rifles, SMGs, snipers), not shotguns
		if (weapon->GetWeaponInfo()->GetSlot() != counterstrikesource::CSS_WEAPON_SLOT_PRIMARY ||
			weapon->GetClassname() == "weapon_m3" || weapon->GetClassname() == "weapon_xm1014")
		{
			return false;
		}

		const SkillBracket& bracket = m_skills.ForSkill(info->skill);
		int chance = 0;

		if (threat->WasEverFullyVisible() && threat->GetTimeSinceLastVisible() <= 3.0f)
		{
			chance = bracket.seenThru; // SEEN_SHOOTTHRU: seen this enemy a moment ago
		}
		else if (threat->GetTimeSinceLastHeard() <= 2.0f)
		{
			chance = bracket.hearThru; // HEAR_SHOOTTHRU: only heard the enemy
		}

		if (chance <= 0)
		{
			return false;
		}

		const float now = gpGlobals->curtime;

		if (now >= info->nextWallbangRoll) // decide once every couple of seconds so the bot does not flicker
		{
			info->wallbang = m_rng.Int(0, 99) < chance;
			info->nextWallbangRoll = now + 2.0f;
		}

		return info->wallbang;
	}

	void CPodBotManager::OnFollowMe(int clientIndex)
	{
		const int limit = m_settings.GetInt("pb_numfollowuser", 5);

		if (!m_loaded || limit <= 0)
		{
			return;
		}

		SourceMod::IGamePlayer* gp = playerhelpers->GetGamePlayer(clientIndex);

		if (gp == nullptr || !gp->IsInGame() || gp->IsFakeClient())
		{
			return;
		}

		CBaseExtPlayer human(gp->GetEdict());
		const auto team = csslib::GetEntityCSSTeam(human.GetEntity());

		if (!human.IsAlive() || (team != counterstrikesource::CSSTeam::TERRORIST && team != counterstrikesource::CSSTeam::COUNTERTERRORIST))
		{
			return;
		}

		const float now = gpGlobals->curtime;
		int following = 0;
		std::vector<CCSSBot*> candidates;
		auto collect = [this, &following, &candidates, clientIndex, now, team](CBaseBot* b)
		{
			BotInfo* info = Find(b);

			if (info == nullptr || !b->IsAlive() || static_cast<CCSSBot*>(b)->GetMyCSSTeam() != team)
			{
				return;
			}

			if (info->followClient == clientIndex && now < info->followUntil)
			{
				following++;
			}
			else
			{
				candidates.push_back(static_cast<CCSSBot*>(b));
			}
		};
		extmanager->ForEachBot(collect);

		while (following < limit && !candidates.empty())
		{
			const size_t pick = m_rng.Index(candidates.size());
			CCSSBot* bot = candidates[pick];
			candidates.erase(candidates.begin() + pick);
			BotInfo* info = Find(bot);
			info->followClient = clientIndex;
			info->followUntil = now + GetFollowDuration();
			bot->OnVoiceCommand(human.GetEntity(), kVoiceFollowMe);

			if (m_rng.Int(0, 99) < 50 && m_settings.GetBool("pb_radio", true))
			{
				bot->FakeClientCommand("roger");
			}

			following++;
		}
	}

	bool CPodBotManager::IsFreeForAll(CCSSBot* bot)
	{
		return m_loaded && m_settings.GetBool("pb_ffa", false) && Find(bot) != nullptr;
	}

	float CPodBotManager::CapCampTime(CCSSBot* bot, float seconds)
	{
		const float limit = m_settings.GetFloat("pb_maxcamptime", 0.0f);

		if (!m_loaded || limit <= 0.0f || Find(bot) == nullptr)
		{
			return seconds;
		}

		return std::min(seconds, limit);
	}

	bool CPodBotManager::RadioAllowed(BotInfo& info, int chance)
	{
		if (!m_loaded || !m_settings.GetBool("pb_radio", true))
		{
			return false;
		}

		const float now = gpGlobals->curtime;

		if (now < info.nextRadio || m_rng.Int(0, 99) >= chance)
		{
			return false;
		}

		info.nextRadio = now + m_settings.GetFloat("pb_radio_cooldown", 10.0f) + m_rng.Float(0.0f, 5.0f);
		return true;
	}

	static bool IsEnemyPlayer(CCSSBot* bot, CBaseEntity* entity)
	{
		if (entity == nullptr || !UtilHelpers::IsPlayer(entity))
		{
			return false;
		}

		const auto team = csslib::GetEntityCSSTeam(entity);
		return (team == counterstrikesource::CSSTeam::TERRORIST || team == counterstrikesource::CSSTeam::COUNTERTERRORIST) && team != bot->GetMyCSSTeam();
	}

	void CPodBotManager::OnInjured(CCSSBot* bot)
	{
		BotInfo* info = Find(bot);

		if (info == nullptr || !bot->IsAlive() || bot->GetHealthPercentage() > 0.35f)
		{
			return;
		}

		if (RadioAllowed(*info, m_settings.GetInt("pb_radio_takingfire_chance", 30)))
		{
			bot->FakeClientCommand(m_rng.Int(0, 1) ? "takingfire" : "needbackup");
		}
	}

	void CPodBotManager::OnSight(CCSSBot* bot, CBaseEntity* subject)
	{
		BotInfo* info = Find(bot);

		if (info == nullptr || !bot->IsAlive() || !IsEnemyPlayer(bot, subject))
		{
			return;
		}

		if (RadioAllowed(*info, m_settings.GetInt("pb_radio_enemyspot_chance", 25)))
		{
			bot->FakeClientCommand("enemyspot");
		}
	}

	static int ExperienceTeam(counterstrikesource::CSSTeam team)
	{
		return team == counterstrikesource::CSSTeam::TERRORIST ? 0 : (team == counterstrikesource::CSSTeam::COUNTERTERRORIST ? 1 : -1);
	}

	static std::string ExperienceFile(const std::string& map)
	{
		char path[PLATFORM_MAX_PATH]{};
		smutils->BuildPath(SourceMod::PathType::Path_SM, path, sizeof(path), "data/navbot/podbot/%s.exp", map.c_str());
		return path;
	}

	void CPodBotManager::LoadExperience()
	{
		m_experience.clear();
		std::ifstream file(ExperienceFile(m_experienceMap));
		unsigned int id = 0;
		float t = 0.0f;
		float ct = 0.0f;

		while (file >> id >> t >> ct)
		{
			m_experience[id] = { std::max(0.0f, std::min(50.0f, t)), std::max(0.0f, std::min(50.0f, ct)) };
		}
	}

	void CPodBotManager::SaveExperience() const
	{
		if (m_experienceMap.empty() || m_experience.empty())
		{
			return;
		}

		const std::string path = ExperienceFile(m_experienceMap);
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
		std::ofstream file(path, std::ios::trunc);

		for (const auto& entry : m_experience)
		{
			if (entry.second[0] > 0.05f || entry.second[1] > 0.05f)
			{
				file << entry.first << ' ' << entry.second[0] << ' ' << entry.second[1] << '\n';
			}
		}
	}

	void CPodBotManager::OnBotKilled(CCSSBot* bot)
	{
		if (!m_loaded || m_settings.GetInt("pb_dangerfactor", 2000) <= 0)
		{
			return;
		}

		CNavArea* area = bot->GetLastKnownNavArea();
		const int team = ExperienceTeam(bot->GetMyCSSTeam());

		if (area == nullptr || team < 0)
		{
			return;
		}

		float& deaths = m_experience[area->GetID()][team];
		deaths = std::min(50.0f, deaths + 1.0f);
	}

	float CPodBotManager::GetExperienceCost(CCSSBot* bot, CNavArea* area) const
	{
		if (!m_loaded || area == nullptr)
		{
			return 0.0f;
		}

		const int factor = m_settings.GetInt("pb_dangerfactor", 2000);
		const int team = ExperienceTeam(bot->GetMyCSSTeam());
		auto it = m_experience.find(area->GetID());

		if (factor <= 0 || team < 0 || it == m_experience.end() || it->second[team] <= 0.0f)
		{
			return 0.0f;
		}

		// logarithmic: 1 death adds about 100 units of path cost at the default factor of 2000
		return static_cast<float>(factor) * 0.05f * std::log2(1.0f + it->second[team]);
	}

	void CPodBotManager::OnRoundEnd()
	{
		for (auto it = m_experience.begin(); it != m_experience.end();)
		{
			it->second[0] *= 0.97f;
			it->second[1] *= 0.97f;
			it = (it->second[0] < 0.05f && it->second[1] < 0.05f) ? m_experience.erase(it) : std::next(it);
		}
	}

	void CPodBotManager::OnMapEnd()
	{
		SaveExperience();
	}

	void CPodBotManager::OnOtherKilled(CCSSBot* bot, CBaseEntity* victim, CBaseEntity* attacker)
	{
		BotInfo* info = Find(bot);

		if (info == nullptr || attacker != bot->GetEntity() || victim == bot->GetEntity() || !UtilHelpers::IsPlayer(victim))
		{
			return;
		}

		if (IsEnemyPlayer(bot, victim) && RadioAllowed(*info, m_settings.GetInt("pb_radio_enemydown_chance", 20)))
		{
			bot->FakeClientCommand("enemydown");
		}

		if (!ChatAllowed(bot, *info, "pb_chat_killed_chance", 30))
		{
			return;
		}

		Say(bot, m_chat.PickKilled(m_rng, MakeContext(bot, PlayerName(victim))));
	}

	void CPodBotManager::OnBombPlanted(CCSSBot* bot)
	{
		BotInfo* info = Find(bot);

		if (info == nullptr || bot->GetMyCSSTeam() != counterstrikesource::CSSTeam::COUNTERTERRORIST)
		{
			return;
		}

		if (!ChatAllowed(bot, *info, "pb_chat_bomb_chance", 40))
		{
			return;
		}

		Say(bot, m_chat.PickBombPlant(m_rng, MakeContext(bot, std::string())));
	}
}
