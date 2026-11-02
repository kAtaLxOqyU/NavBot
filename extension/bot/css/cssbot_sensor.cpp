#include NAVBOT_PCH_FILE
#include "cssbot.h"
#include "cssbot_sensor.h"
#include <podbot/podbot_manager.h>

CCSSBotSensor::CCSSBotSensor(CCSSBot* bot) :
	CSimplePvPSensor<CCSSBot>(bot)
{
}

bool CCSSBotSensor::IsFriendly(CBaseEntity* entity) const
{
	if (podbot::PodBot().IsFreeForAll(GetBot<CCSSBot>()))
	{
		return false;
	}

	return CSimplePvPSensor<CCSSBot>::IsFriendly(entity);
}

bool CCSSBotSensor::IsEnemy(CBaseEntity* entity) const
{
	if (podbot::PodBot().IsFreeForAll(GetBot<CCSSBot>()))
	{
		// anybody on a team except myself
		return entity != GetBot<CCSSBot>()->GetEntity() && modhelpers->GetEntityTeamNumber(entity) >= static_cast<int>(counterstrikesource::CSSTeam::TERRORIST);
	}

	return CSimplePvPSensor<CCSSBot>::IsEnemy(entity);
}
