#ifndef __NAVBOT_CSS_BOT_FOLLOW_PLAYER_TASK_H_
#define __NAVBOT_CSS_BOT_FOLLOW_PLAYER_TASK_H_

#include <bot/interfaces/path/meshnavigator.h>
#include <sdkports/sdk_timers.h>

// PODBot "Follow Me": the bot walks to a human player and stays within range of them for a while.
class CCSSBotFollowPlayerTask : public AITask<CCSSBot>
{
public:
	CCSSBotFollowPlayerTask(CCSSBot* bot, int clientIndex, float duration, float followRange = 200.0f);

	TaskResult<CCSSBot> OnTaskStart(CCSSBot* bot, AITask<CCSSBot>* pastTask) override;
	TaskResult<CCSSBot> OnTaskUpdate(CCSSBot* bot) override;

	TaskEventResponseResult<CCSSBot> OnStuck(CCSSBot* bot) override;
	TaskEventResponseResult<CCSSBot> OnMoveToFailure(CCSSBot* bot, CPath* path, IEventListener::MovementFailureType reason) override;

	const char* GetName() const override { return "FollowPlayer"; }

private:
	int m_client;
	float m_duration;
	float m_range;
	CountdownTimer m_timer;
	CCSSBotPathCost m_pathcost;
	CMeshNavigatorAutoRepath m_nav;
	CPathFailCounter m_counter;
};

#endif // !__NAVBOT_CSS_BOT_FOLLOW_PLAYER_TASK_H_
