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
		META_CONPRINT("Usage: pb add [skill [personality [team [model [name]]]]] | pb fillserver [skill [personality [team [model]]]] | pb reload\n");
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
	else
	{
		META_CONPRINT("Unknown pb command.\n");
	}
}
