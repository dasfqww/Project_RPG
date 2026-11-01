#include "Tests/RPGMontageImmunityTestSupport.h"
#if !UE_BUILD_SHIPPING
#include "Animation/AnimMontage.h"
#include "AnimNotifyState/RPGAnimNotifyState_CombatImmunity.h"
#include "Skill/RPGSkillDefinition.h"
UAnimMontage* RPGMontageImmunityTests::LoadPlayerMontage()
{
	const auto* Definition = LoadObject<URPGSkillDefinition>(nullptr, TEXT("/Game/Blueprints/GameData/Skill/DA_Skill_ChargeSlash.DA_Skill_ChargeSlash"));
	return Definition ? Definition->SkillMontage.Get() : nullptr;
}
void RPGMontageImmunityTests::AddWindow(UAnimMontage& Montage, float StartFraction, float EndFraction, int32 Flags)
{
	auto* Notify = NewObject<URPGAnimNotifyState_CombatImmunity>(&Montage); Notify->Immunities.Flags = Flags;
	auto& Event = Montage.Notifies.AddDefaulted_GetRef(); Event.NotifyStateClass = Notify;
	Event.Link(&Montage, StartFraction * Montage.GetPlayLength()); Event.SetDuration((EndFraction - StartFraction) * Montage.GetPlayLength());
	Event.MontageTickType = EMontageNotifyTickType::BranchingPoint;
}
FORCENOINLINE void RPGMontageImmunityTests::Refresh(UAnimSequenceBase& Animation) { Animation.RefreshCacheData(); }
#endif
