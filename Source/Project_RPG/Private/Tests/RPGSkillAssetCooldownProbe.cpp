#include "Tests/RPGSkillAssetCooldownProbe.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "GameplayEffect/GE_CoolDown.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "Skill/RPGSkillDefinition.h"
#include "UObject/UnrealType.h"

namespace
{
	constexpr float AssetReadyScore = 4201.0f;
	constexpr float AssetActiveScore = 4202.0f;
	constexpr float AssetCancelledScore = 4203.0f;
	constexpr float AssetExpiredScore = 4204.0f;

	void Checkpoint(APlayerState& State, const float Score)
	{
		if (State.GetScore() == Score) return;
		State.SetScore(Score);
		State.ForceNetUpdate();
	}
}

FRPGSkillAssetCooldownProbe::EResult FRPGSkillAssetCooldownProbe::Tick(APlayerController& Controller)
{
	auto Fail = [this](const TCHAR* Reason) { Failure = Reason; return EResult::Failed; };
	if (!Definition.IsValid())
		Definition.Reset(LoadObject<URPGSkillDefinition>(nullptr,
			TEXT("/Game/Blueprints/GameData/Skill/DA_Skill_ChargeSlash.DA_Skill_ChargeSlash")));
	if (!Definition.IsValid() || !Definition->IsSupportedForNetworking())
		return Fail(TEXT("AuthoredCooldownDefinitionHasNoStableNetworkIdentity"));
	auto* ASC = Cast<URPGAbilitySystemComponent>(
		UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Controller.GetPawn()));
	if (!ASC || !Controller.PlayerState) return EResult::Waiting;
	FGameplayAbilitySpec* Spec = nullptr;
	for (FGameplayAbilitySpec& Candidate : ASC->GetActivatableAbilities())
		if (Cast<URPGGameplayAbility_SkillContainer>(Candidate.Ability) &&
			Candidate.Ability->GetClass()->GetName().Contains(TEXT("ChargeSlash"))) { Spec = &Candidate; break; }
	auto* Ability = Spec ? Cast<URPGGameplayAbility_SkillContainer>(Spec->GetPrimaryInstance()) : nullptr;
	if (!Ability) return EResult::Waiting;
	// Restore only the disposable ability instance's original Definition pointer.
	// No asset, CDO, catalog entry or authored montage is modified.
	if (!Spec->IsActive())
	{
		const auto* Property = FindFProperty<FObjectPropertyBase>(
			URPGGameplayAbility_SkillContainer::StaticClass(), TEXT("SkillDefinition"));
		check(Property);
		Property->SetObjectPropertyValue_InContainer(Ability, Definition.Get());
	}
	FGameplayEffectQuery Query;
	Query.EffectDefinition = UGE_CoolDown::StaticClass();
	Query.EffectSource = Definition.Get();
	const auto Effects = ASC->GetActiveEffects(Query);
	auto CheckEffect = [&](const bool bRequireReplication)
	{
		if (Effects.Num() != 1) return false; // prediction catch-up can briefly overlap two effects
		const auto* Effect = ASC->GetActiveGameplayEffect(Effects[0]);
		if (!Effect || Effect->Spec.GetContext().GetSourceObject() != Definition.Get() ||
			!FMath::IsNearlyEqual(Effect->Spec.GetDuration(), ExpectedDuration)) return false;
		// A local predicted GE alone is not evidence of actual server replication.
		// TargetData has its own scoped prediction key on the server. It need not
		// equal the activation key; source identity and fresh-effect barriers below
		// bind this received effect to the activation instead.
		if (bRequireReplication && !Effect->PredictionKey.WasReceived()) return false;
		float Remaining = 0.0f, Duration = 0.0f;
		Ability->GetCooldownTimeRemainingAndDuration(Spec->Handle, ASC->AbilityActorInfo.Get(), Remaining, Duration);
		return Remaining > 0.0f && Remaining <= ExpectedDuration + KINDA_SMALL_NUMBER &&
			FMath::IsNearlyEqual(Duration, ExpectedDuration) &&
			FMath::IsNearlyEqual(Remaining, Ability->GetCooldownTimeRemaining(ASC->AbilityActorInfo.Get())) &&
			!Ability->CheckCooldown(Spec->Handle, ASC->AbilityActorInfo.Get());
	};
	if (Controller.HasAuthority())
	{
		if (!bServerSawActivation)
		{
			if (!Spec->IsActive()) { Checkpoint(*Controller.PlayerState, AssetReadyScore); return EResult::Waiting; }
			if (Effects.IsEmpty()) return EResult::Waiting; // activation may still be waiting for TargetData
			ExpectedDuration = Ability->GetActiveSkillSpec().GetCooldownDuration();
			if (!CheckEffect(false)) return Fail(TEXT("AuthoredServerCooldownEffectMismatch"));
			bServerSawActivation = true;
			Checkpoint(*Controller.PlayerState, AssetActiveScore);
			UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_TRIPOD_NETTEST SERVER_ASSET_COOLDOWN Duration=%f Source=%s Controller=%s"),
				ExpectedDuration, *Definition->GetPathName(), *Controller.GetName());
		}
		else if (!bServerSawExpiry && !Spec->IsActive())
		{
			if (Ability->CheckCooldown(Spec->Handle, ASC->AbilityActorInfo.Get()))
			{
				bServerSawExpiry = true;
				Checkpoint(*Controller.PlayerState, AssetExpiredScore);
			}
			else Checkpoint(*Controller.PlayerState, AssetCancelledScore);
		}
		else if (bServerSawExpiry && Spec->IsActive())
		{
			if (!CheckEffect(false)) return Fail(TEXT("AuthoredServerCooldownReinputMismatch"));
			UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_TRIPOD_NETTEST SERVER_ASSET_REINPUT Duration=%f Controller=%s"),
				ExpectedDuration, *Controller.GetName());
			ASC->CancelAbilityHandle(Spec->Handle);
			return EResult::Passed;
		}
		return EResult::Waiting;
	}
	if (ClientPhase == 0)
	{
		if (Controller.PlayerState->GetScore() != AssetReadyScore) return EResult::Waiting;
		ASC->OnAbilitySpecInputPressed(Spec->Handle);
		if (!Spec->IsActive()) return Fail(TEXT("AuthoredCooldownInputDidNotActivate"));
		ExpectedDuration = Ability->GetActiveSkillSpec().GetCooldownDuration();
		ExpectedPredictionKey = Ability->GetCurrentActivationInfo().GetActivationPredictionKey().Current;
		if (ExpectedPredictionKey == 0 || Effects.Num() != 0) return Fail(TEXT("AuthoredCooldownPredictionPreconditionFailed"));
		const auto PredictedEffects = ASC->GetActiveEffects(Query);
		const auto* PredictedEffect = PredictedEffects.Num() == 1 ? ASC->GetActiveGameplayEffect(PredictedEffects[0]) : nullptr;
		if (!PredictedEffect || PredictedEffect->PredictionKey.WasReceived() ||
			!PredictedEffect->PredictionKey.IsLocalClientKey() ||
			!FMath::IsNearlyEqual(PredictedEffect->Spec.GetDuration(), ExpectedDuration) ||
			Ability->CheckCooldown(Spec->Handle, ASC->AbilityActorInfo.Get()))
			return Fail(TEXT("AuthoredLocalPredictedCooldownMissing"));
		UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_TRIPOD_NETTEST CLIENT_ASSET_PREDICTED Duration=%f PredictionKey=%d ActivationKey=%d"),
			ExpectedDuration, PredictedEffect->PredictionKey.Current, ExpectedPredictionKey);
		ClientPhase = 1;
	}
	else if (ClientPhase == 1)
	{
		if (!CheckEffect(true) || Controller.PlayerState->GetScore() != AssetActiveScore) return EResult::Waiting;
		UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_TRIPOD_NETTEST CLIENT_ASSET_COOLDOWN Replicated=1 QueriesAgree=1 Duration=%f Source=%s PredictionKey=%d ActivationKey=%d"),
			ExpectedDuration, *Definition->GetPathName(), ASC->GetActiveGameplayEffect(Effects[0])->PredictionKey.Current, ExpectedPredictionKey);
		ASC->CancelAbilityHandle(Spec->Handle);
		ASC->OnAbilitySpecInputReleased(Spec->Handle);
		ClientPhase = 2;
	}
	else if (ClientPhase == 2)
	{
		if (Controller.PlayerState->GetScore() != AssetCancelledScore) return EResult::Waiting;
		if (!CheckEffect(true)) return Fail(TEXT("AuthoredCooldownLostAfterCancellation"));
		ASC->OnAbilitySpecInputPressed(Spec->Handle);
		const bool bRejected = !Spec->IsActive();
		ASC->OnAbilitySpecInputReleased(Spec->Handle);
		if (!bRejected) return Fail(TEXT("AuthoredCooldownAcceptedRapidReinput"));
		UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_TRIPOD_NETTEST CLIENT_ASSET_REINPUT_BLOCKED Cooldown=1"));
		ClientPhase = 3;
	}
	else if (ClientPhase == 3)
	{
		// The rejection and UI queries above are entirely local; only the final
		// success is synchronized to avoid racing clock estimation at expiry.
		if (Controller.PlayerState->GetScore() != AssetExpiredScore ||
			!Ability->CheckCooldown(Spec->Handle, ASC->AbilityActorInfo.Get())) return EResult::Waiting;
		if (!Effects.IsEmpty() || Ability->GetCooldownTimeRemaining(ASC->AbilityActorInfo.Get()) != 0.0f)
			return Fail(TEXT("AuthoredCooldownDidNotExpire"));
		ASC->OnAbilitySpecInputPressed(Spec->Handle);
		if (!Spec->IsActive()) return Fail(TEXT("AuthoredCooldownDidNotPermitFreshReinput"));
		ExpectedPredictionKey = Ability->GetCurrentActivationInfo().GetActivationPredictionKey().Current;
		ClientPhase = 4;
	}
	else if (ClientPhase == 4 && CheckEffect(true))
	{
		UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_TRIPOD_NETTEST CLIENT_ASSET_REINPUT Replicated=1 Duration=%f"), ExpectedDuration);
		ASC->CancelAbilityHandle(Spec->Handle);
		ASC->OnAbilitySpecInputReleased(Spec->Handle);
		return EResult::Passed;
	}
	return EResult::Waiting;
}
