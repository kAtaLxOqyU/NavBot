#include NAVBOT_PCH_FILE
#include "cssbot.h"
#include "cssbot_combat.h"
#include <podbot/podbot_manager.h>

CCSSBotCombat::CCSSBotCombat(CCSSBot* bot) :
	ICombat(bot)
{
}

void CCSSBotCombat::Reset()
{
	m_flashbangedTimer.Invalidate();
	ICombat::Reset();
}

bool CCSSBotCombat::CanFireWeaponAtEnemy(const CBaseBot* bot, const CKnownEntity* threat, const CBotWeapon* activeWeapon) const
{
	if (IsFlashbanged()) { return false; }

	return ICombat::CanFireWeaponAtEnemy(bot, threat, activeWeapon);
}

void CCSSBotCombat::FireWeaponAtEnemy(const CBaseBot* bot, const CKnownEntity* threat, const CBotWeapon* activeWeapon, const bool bypassLOS)
{
	const bool throughWalls = podbot::PodBot().ShouldShootThrough(static_cast<CCSSBot*>(const_cast<CBaseBot*>(bot)), threat, activeWeapon);
	ICombat::FireWeaponAtEnemy(bot, threat, activeWeapon, bypassLOS || throughWalls);
}
