#ifndef __NAVBOT_CSS_BOT_SENSOR_INTERFACE_H_
#define __NAVBOT_CSS_BOT_SENSOR_INTERFACE_H_

#include "../interfaces/sensor_templates.h"

class CCSSBot;

class CCSSBotSensor : public CSimplePvPSensor<CCSSBot>
{
public:
	CCSSBotSensor(CCSSBot* bot);

	// PODBot free-for-all mode (pb_ffa): everybody except me is an enemy
	bool IsFriendly(CBaseEntity* entity) const override;
	bool IsEnemy(CBaseEntity* entity) const override;

private:

};

#endif // !__NAVBOT_CSS_BOT_SENSOR_INTERFACE_H_
