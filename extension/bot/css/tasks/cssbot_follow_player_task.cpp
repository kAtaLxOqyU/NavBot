#include NAVBOT_PCH_FILE
#include <bot/css/cssbot.h>
#include <bot/tasks_shared/bot_shared_default_combat_tasks.h>
#include "cssbot_follow_player_task.h"

CCSSBotFollowPlayerTask::CCSSBotFollowPlayerTask(CCSSBot* bot, int clientIndex, float duration, float followRange) :
	m_client(clientIndex), m_duration(duration), m_range(followRange), m_pathcost(bot)
{
}

TaskResult<CCSSBot> CCSSBotFollowPlayerTask::OnTaskStart(CCSSBot* bot, AITask<CCSSBot>* pastTask)
{
	m_timer.Start(m_duration);
	m_counter.Reset();
	return Continue();
}

TaskResult<CCSSBot> CCSSBotFollowPlayerTask::OnTaskUpdate(CCSSBot* bot)
{
	if (m_timer.IsElapsed())
	{
		return Done("Done following!");
	}

	SourceMod::IGamePlayer* gp = playerhelpers->GetGamePlayer(m_client);

	if (gp == nullptr || !gp->IsInGame())
	{
		return Done("The player I was following left!");
	}

	CBaseExtPlayer human(gp->GetEdict());

	if (!human.IsAlive())
	{
		return Done("The player I was following died!");
	}

	const CKnownEntity* threat = bot->GetSensorInterface()->GetPrimaryKnownThreat(true);

	if (threat && threat->GetLastKnownArea() != nullptr && bot->GetMovementInterface()->IsAreaTraversable(threat->GetLastKnownArea()))
	{
		if (bot->GetBehaviorInterface()->ShouldSeekAndDestroy(bot, threat) != ANSWER_NO)
		{
			return PauseFor(new CBotSharedDefaultCombatBehaviorTask<CCSSBot, CCSSBotPathCost>(), "Attacking visible enemy!");
		}
	}

	const Vector goal = human.GetAbsOrigin();

	if (bot->GetRangeTo(goal) > m_range)
	{
		m_nav.Update(bot, goal, m_pathcost);
	}

	return Continue();
}

TaskEventResponseResult<CCSSBot> CCSSBotFollowPlayerTask::OnStuck(CCSSBot* bot)
{
	if (m_counter.Increase())
	{
		return TryDone(PRIORITY_HIGH, "Stuck while following!");
	}

	return TryToMaintain();
}

TaskEventResponseResult<CCSSBot> CCSSBotFollowPlayerTask::OnMoveToFailure(CCSSBot* bot, CPath* path, IEventListener::MovementFailureType reason)
{
	if (m_counter.Increase())
	{
		return TryDone(PRIORITY_HIGH, "Cannot reach the player!");
	}

	return TryToMaintain();
}
