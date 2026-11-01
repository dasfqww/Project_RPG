#include "Combat/RPGMontageImmunityTypes.h"
#include "Animation/AnimMontage.h"
#include "AnimNotifyState/RPGAnimNotifyState_CombatImmunity.h"

bool RPGMontageImmunity::ReadWindows(const UAnimMontage& Montage, TArray<FRPGMontageImmunityWindow>& OutWindows)
{
	OutWindows.Reset();
	for (const auto& Event : Montage.Notifies)
	{
		const auto* Notify = Cast<URPGAnimNotifyState_CombatImmunity>(Event.NotifyStateClass);
		if (!Notify) continue;
		const float Start = Event.GetTime(), End = Start + Event.GetDuration();
		if (!Notify->Immunities.IsValid() || !FMath::IsFinite(Start) || !FMath::IsFinite(End) || Start < 0.f
			|| End <= Start || End > Montage.GetPlayLength() + KINDA_SMALL_NUMBER || OutWindows.Num() >= 8)
		{ OutWindows.Reset(); return false; }
		if (Notify->Immunities.IsEnabled()) OutWindows.Add({Start, End, Notify->Immunities});
	}
	return true;
}
