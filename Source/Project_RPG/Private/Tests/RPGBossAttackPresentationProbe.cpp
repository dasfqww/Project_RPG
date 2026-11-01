#include "Tests/RPGBossMapE2EProbeComponent.h"

#include "AbilitySystemComponent.h"
#include "AI/RPGBossAIComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/RPGBossCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

bool URPGBossMapE2EProbeComponent::ObserveBossAttackPresentation(FGameplayTag Action)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EAttackPresentation"))) { return true; }
	ARPGBossCharacter* Boss = OriginalBoss.Get();
	if (!Boss) { return false; }
	const bool bHeavy = Action == FGameplayTag::RequestGameplayTag(TEXT("Boss.Action.SwordMan.HeavyStrike"));
	if (!bHeavy && Action != FGameplayTag::RequestGameplayTag(TEXT("Boss.Action.SwordMan.Strike"))) { return false; }
	UAbilitySystemComponent* ASC = Boss->GetAbilitySystemComponent();
	UAnimMontage* Montage = ASC ? ASC->GetCurrentMontage() : nullptr;
	const UAnimInstance* Anim = Boss->GetMesh() ? Boss->GetMesh()->GetAnimInstance() : nullptr;
	const TCHAR* Expected = bHeavy ? TEXT("AM_RaidSwordMan_HeavyStrike") : TEXT("AM_RaidSwordMan_Strike");
	const FRPGBossTelegraphState Telegraph = Boss->BossAIComponent->GetTelegraphState();
	if (!Telegraph.bActive || Telegraph.ActionTag != Action || !Montage || Montage->GetName() != Expected ||
		!Anim || !Anim->Montage_IsPlaying(Montage)) { return false; }
	bool& Observed = bHeavy ? bSawBossHeavyMontage : bSawBossStrikeMontage;
	if (!Observed)
	{
		Observed = true;
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_BOSS_ATTACK_MONTAGE Action=%s Montage=%s Active=1 Authority=%d Sequence=%d"),
			bHeavy ? TEXT("HeavyStrike") : TEXT("Strike"), Expected, Boss->HasAuthority(), Telegraph.Sequence);
	}
	return true;
}
