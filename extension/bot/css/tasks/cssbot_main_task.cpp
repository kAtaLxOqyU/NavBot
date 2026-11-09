#include NAVBOT_PCH_FILE
#include <bot/css/cssbot.h>
#include <bot/bot_shared_convars.h>
#include <bot/bot_shared_utils.h>
#include <bot/tasks_shared/bot_shared_dead.h>
#include <bot/tasks_shared/bot_shared_debug_move_to_origin.h>
#include "cssbot_tactical_task.h"
#include "cssbot_main_task.h"
#include "cssbot_follow_player_task.h"
#include <sdkports/sdk_takedamageinfo.h>
#include <podbot/podbot_manager.h>

AITask<CCSSBot>* CCSSBotMainTask::InitialNextTask(CCSSBot* bot)
{
	if (cvar_bot_disable_behavior.GetBool())
	{
		return nullptr;
	}

	return new CCSSBotTacticalTask;
}

TaskResult<CCSSBot> CCSSBotMainTask::OnTaskUpdate(CCSSBot* bot)
{
	return Continue();
}

TaskEventResponseResult<CCSSBot> CCSSBotMainTask::OnDebugMoveToCommand(CCSSBot* bot, const Vector& moveTo)
{
	return TryPauseFor(new CBotSharedDebugMoveToOriginTask<CCSSBot, CCSSBotPathCost>(bot, moveTo), PRIORITY_CRITICAL, "Responding to debug command!");
}

TaskEventResponseResult<CCSSBot> CCSSBotMainTask::OnKilled(CCSSBot* bot, const CTakeDamageInfo& info)
{
	podbot::PodBot().OnBotKilled(bot);
	return TrySwitchTo(new CBotSharedDeadTask<CCSSBot, CCSSBotMainTask>(), PRIORITY_CRITICAL, "I am dead!");
}

TaskEventResponseResult<CCSSBot> CCSSBotMainTask::OnOtherKilled(CCSSBot* bot, CBaseEntity* pVictim, const CTakeDamageInfo& info)
{
	podbot::PodBot().OnOtherKilled(bot, pVictim, info.GetAttacker());
	return TryContinue();
}

QueryAnswerType CCSSBotMainTask::ShouldScopeWithWeapon(CBaseBot* me, const CBotWeapon* activeWeapon, const bool wantstoscope, const bool isscoped)
{
	switch (podbot::PodBot().ShouldScope(static_cast<CCSSBot*>(me), activeWeapon, wantstoscope, isscoped))
	{
	case podbot::ScopeAnswer::Yes:
		return ANSWER_YES;
	case podbot::ScopeAnswer::No:
		return ANSWER_NO;
	default:
		return ANSWER_UNDEFINED;
	}
}

TaskEventResponseResult<CCSSBot> CCSSBotMainTask::OnVoiceCommand(CCSSBot* bot, CBaseEntity* subject, int command)
{
	if (command == podbot::kVoiceFollowMe)
	{
		return TryPauseFor(new CCSSBotFollowPlayerTask(bot, UtilHelpers::IndexOfEntity(subject), podbot::PodBot().GetFollowDuration()), PRIORITY_HIGH, "Following a player!");
	}

	return TryContinue();
}

TaskEventResponseResult<CCSSBot> CCSSBotMainTask::OnInjured(CCSSBot* bot, const CTakeDamageInfo& info)
{
	podbot::PodBot().OnInjured(bot);
	return TryContinue();
}

TaskEventResponseResult<CCSSBot> CCSSBotMainTask::OnSight(CCSSBot* bot, CBaseEntity* subject)
{
	podbot::PodBot().OnSight(bot, subject);
	return TryContinue();
}

TaskEventResponseResult<CCSSBot> CCSSBotMainTask::OnBombPlanted(CCSSBot* bot, const Vector& position, const int teamIndex, CBaseEntity* player, CBaseEntity* ent)
{
	podbot::PodBot().OnBombPlanted(bot);
	return TryContinue();
}

Vector CCSSBotMainTask::GetTargetAimPos(CBaseBot* me, CBaseEntity* entity, DesiredAimSpot desiredAim)
{
	return m_aim.SelectAimPosition(static_cast<CCSSBot*>(me), entity, desiredAim);
}

Vector CCSSBotMainTask::GetAimPosition(CBaseBot* me, const Vector& pos, botweapons::AttackType attacktype)
{
	return m_aim.SelectAimPosition(static_cast<CCSSBot*>(me), pos, attacktype);
}

const CKnownEntity* CCSSBotMainTask::SelectTargetThreat(CBaseBot* me, const CKnownEntity* threat1, const CKnownEntity* threat2)
{
	return botsharedutils::threat::DefaultThreatSelection(me, threat1, threat2);
}
