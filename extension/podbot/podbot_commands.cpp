#include NAVBOT_PCH_FILE
#include <cstdlib>

#include <extension.h>
#include <manager.h>
#include "podbot_manager.h"

// pb add [skill [personality [team [model [name]]]]] | pb fillserver [skill [personality [team [model]]]] | pb reload
CON_COMMAND(pb, "PODBot-style bot control: pb add|fillserver|reload")
{
	DECLARE_COMMAND_ARGS;

	if (args.ArgC() < 2)
	{
		META_CONPRINT("Usage: pb add [skill [personality [team [model [name]]]]] | pb fillserver [skill [personality [team [model]]]] | pb reload | pb debug [aim] [events] [all] [off] | pb debug filter <bot name>\n");
		return;
	}

	if (!extmanager->AreBotsSupported())
	{
		META_CONPRINT("Bot support is disabled! Check SourceMod logs for more information. \n");
		return;
	}

	auto& pod = podbot::PodBot();
	const char* verb = args[1];

	if (strncasecmp(verb, "reload", 6) == 0)
	{
		pod.Load();
	}
	else if (strncasecmp(verb, "add", 3) == 0)
	{
		const int skill = args.ArgC() > 2 ? std::atoi(args[2]) : -1;
		const int personality = args.ArgC() > 3 ? std::atoi(args[3]) : 0;
		const std::string name = args.ArgC() > 6 ? args[6] : "";
		const int team = args.ArgC() > 4 ? std::atoi(args[4]) : 0;
		const int model = args.ArgC() > 5 ? std::atoi(args[5]) : 0;
		pod.AddBot(skill, personality, name, team, model);
	}
	else if (strncasecmp(verb, "fillserver", 10) == 0)
	{
		pod.FillServer(args.ArgC() > 2 ? std::atoi(args[2]) : -1, args.ArgC() > 3 ? std::atoi(args[3]) : 0,
			args.ArgC() > 4 ? std::atoi(args[4]) : 0, args.ArgC() > 5 ? std::atoi(args[5]) : 0);
	}
	else if (strncasecmp(verb, "debug", 5) == 0)
	{
		if (args.ArgC() >= 3 && strncasecmp(args[2], "filter", 6) == 0)
		{
			pod.SetDebugFilter(args.ArgC() > 3 ? args[3] : "");
		}
		else if (args.ArgC() >= 3)
		{
			std::string words;

			for (int i = 2; i < args.ArgC(); i++)
			{
				words += args[i];
				words += ' ';
			}

			pod.SetDebugFlags(podbot::ParseDebugFlags(words));
		}

		char text[160];
		ke::SafeSprintf(text, sizeof(text), "PODBot debug: aim %s, events %s, bot filter \"%s\". Use: pb debug aim events | all | off | filter <name>\n",
			(pod.GetDebugFlags() & podbot::kDebugAim) ? "ON" : "off", (pod.GetDebugFlags() & podbot::kDebugEvents) ? "ON" : "off", pod.GetDebugFilter().c_str());
		META_CONPRINT(text);
	}
	else
	{
		META_CONPRINT("Unknown pb command.\n");
	}
}

// Every pb_* setting of podbot.cfg as a console command: "pb_chat 0" changes it right away, "pb_chat" shows it.
// The change lasts until the next "pb reload" or map change. Edit podbot.cfg to keep a value.
#define PODBOT_SETTING_LIST(X) \
	X(pb_minbots) X(pb_maxbots) X(pb_bot_quota_match) X(pb_bot_join_team) X(pb_skin) X(pb_minbotskill) X(pb_maxbotskill) \
	X(pb_mapstartbotdelay) X(pb_detailnames) X(pb_chat) X(pb_radio) X(pb_jasonmode) X(pb_restrweapons) X(pb_restrequipammo) \
	X(pb_autokill) X(pb_autokilldelay) X(pb_dangerfactor) X(pb_maxcamptime) X(pb_use_hints) X(pb_ffa) X(pb_shootthruwalls) \
	X(pb_numfollowuser) X(pb_followtime) X(pb_crouch_chance) X(pb_debug) X(pb_maxweaponpickup) \
	X(pb_timer_sound) X(pb_timer_pickup) X(pb_timer_grenade) \
	X(pb_chat_killed_chance) X(pb_chat_bomb_chance) X(pb_chat_dead_chance) X(pb_chat_reply_chance) X(pb_chat_cooldown) \
	X(pb_radio_enemydown_chance) X(pb_radio_takingfire_chance) X(pb_radio_enemyspot_chance) X(pb_radio_cooldown) \
	X(pb_aim_type) X(pb_aim_spring_stiffness_x) X(pb_aim_spring_stiffness_y) X(pb_aim_damper_coefficient_x) X(pb_aim_damper_coefficient_y) \
	X(pb_aim_deviation_x) X(pb_aim_deviation_y) X(pb_aim_influence_x_on_y) X(pb_aim_influence_y_on_x) X(pb_aim_offset_delay) \
	X(pb_aim_notarget_slowdown_ratio) X(pb_aim_target_anticipation_ratio) X(pb_aim_damping_scale) X(pb_aim_idle_pitch_scale) \
	X(pb_aim_idle_pitch_limit) X(pb_aim_pitch_speed_ratio) \
	X(pb_sniper) X(pb_sniper_camp_time) X(pb_scope_toggle_time) X(pb_awp_headshot_chance)

#define PODBOT_SETTING_COMMAND(name) \
	CON_COMMAND(name, "PODBot setting " #name ": type a value to change it, or nothing to see it. Edit podbot.cfg to keep it.") \
	{ \
		DECLARE_COMMAND_ARGS; \
		std::string value; \
		for (int i = 1; i < args.ArgC(); i++) \
		{ \
			if (i > 1) { value += ' '; } \
			value += args[i]; \
		} \
		const std::string reply = podbot::PodBot().ConsoleSetting(#name, value, args.ArgC() > 1); \
		META_CONPRINT(reply.c_str()); \
	}

PODBOT_SETTING_LIST(PODBOT_SETTING_COMMAND)
