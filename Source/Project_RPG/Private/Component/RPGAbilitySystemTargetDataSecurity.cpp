#include "Component/RPGAbilitySystemComponent.h"

#include "Abilities/GameplayAbility.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "Security/RPGDungeonGameplayAuthority.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "Skill/RPGSkillExecutionPolicy.h"
#if !UE_BUILD_SHIPPING
#include "Tests/RPGSkillNetworkE2EProbeComponent.h"
#endif

namespace
{
	bool IsFiniteVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	bool IsBoundedTargetData(const FGameplayAbilityTargetDataHandle& Data)
	{
		constexpr int32 MaximumEntries = 32;
		constexpr int32 MaximumActors = 64;
		if (Data.Num() > MaximumEntries) return false;
		int32 Actors = 0;
		for (int32 Index = 0; Index < Data.Num(); ++Index)
		{
			const FGameplayAbilityTargetData* Entry = Data.Get(Index);
			if (!Entry) return false;
			const UScriptStruct* Type = Entry->GetScriptStruct();
			if (Type == FGameplayAbilityTargetData_ActorArray::StaticStruct())
			{
				// Check the array length without GetActors(), which copies client-controlled data.
				const auto* ActorData = static_cast<const FGameplayAbilityTargetData_ActorArray*>(Entry);
				const int32 Count = ActorData->TargetActorArray.Num();
				if (Count > MaximumActors - Actors || ActorData->SourceLocation.LiteralTransform.ContainsNaN()) return false;
				Actors += Count;
			}
			else if (Type == FGameplayAbilityTargetData_LocationInfo::StaticStruct())
			{
				const auto* Locations = static_cast<const FGameplayAbilityTargetData_LocationInfo*>(Entry);
				if (Locations->SourceLocation.LiteralTransform.ContainsNaN() ||
					Locations->TargetLocation.LiteralTransform.ContainsNaN()) return false;
			}
			else if (Type == FGameplayAbilityTargetData_SingleTargetHit::StaticStruct())
			{
				if (++Actors > MaximumActors) return false;
				const FHitResult& Hit = static_cast<const FGameplayAbilityTargetData_SingleTargetHit*>(Entry)->HitResult;
				if (!IsFiniteVector(Hit.TraceStart) || !IsFiniteVector(Hit.TraceEnd) ||
					!IsFiniteVector(Hit.Location) || !IsFiniteVector(Hit.ImpactPoint) ||
					!IsFiniteVector(Hit.Normal) || !IsFiniteVector(Hit.ImpactNormal) ||
					!FMath::IsFinite(Hit.Distance) || !FMath::IsFinite(Hit.Time)) return false;
			}
			else return false; // Add custom wire types only with a bounded validator.
		}
		return true;
	}
}

void URPGAbilitySystemComponent::ReportTargetDataAdmissionFailure(const TCHAR* Reason, const double Now)
{
	// Do not let a burst create an unbounded audit queue or repeatedly raise risk.
	if (LastTargetDataAdmissionReportAt >= 0.0 && Now >= LastTargetDataAdmissionReportAt &&
		Now - LastTargetDataAdmissionReportAt < 1.0) return;
	LastTargetDataAdmissionReportAt = Now;
	if (AActor* Avatar = GetAvatarActor())
	{
		if (auto* Security = Avatar->FindComponentByClass<URPGSecurityValidationComponent>())
		{
			Security->ReportInvalidTargetData(Reason);
		}
	}
}

bool URPGAbilitySystemComponent::AdmitTargetDataRequest(const FGameplayAbilitySpecHandle Handle,
	const FPredictionKey OriginalKey, const FPredictionKey MessageKey, const bool bCancellation,
	const FGameplayAbilityTargetDataHandle* Data)
{
	const AActor* Owner = GetOwnerActor();
	const AActor* Avatar = GetAvatarActor();
	if (bDungeonGameplayAuthorityRevoked || IsDungeonGameplayRevoked(Avatar)
		|| !Owner || !Owner->HasAuthority() || !Avatar || !Avatar->HasAuthority() ||
		!GetWorld() || Avatar->GetWorld() != GetWorld()) return false;
	const double Now = GetWorld()->GetTimeSeconds();
	if (!TargetDataRequestWindow.TryConsume(Now, MaximumTargetDataRequestsPerSecond))
	{
		ReportTargetDataAdmissionFailure(TEXT("TargetData request rate exceeded the server cap."), Now);
		return false;
	}
	FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle);
	if (!Spec || !Spec->Ability)
	{
		ReportTargetDataAdmissionFailure(TEXT("TargetData references an ungranted ability."), Now);
		return false;
	}
	// Late reliable RPCs after termination/re-activation are normal under high RTT.
	// Drop them without attributing cheating or allocating an engine cache entry.
	if (!Spec->IsActive() || !OriginalKey.IsValidKey() || !MessageKey.IsLocalClientKey()) return false;
	bool bMatchingActivation = false;
	for (UGameplayAbility* Instance : Spec->GetAbilityInstances())
	{
		if (Instance && Instance->IsActive() &&
			Instance->GetCurrentActivationInfo().GetActivationPredictionKey() == OriginalKey)
		{
			bMatchingActivation = true;
			break;
		}
	}
	if (!bMatchingActivation) return false;
	if (Data && !IsBoundedTargetData(*Data))
	{
		ReportTargetDataAdmissionFailure(TEXT("TargetData contains an oversized, non-finite or unsupported payload."), Now);
		return false;
	}
	const FGameplayAbilitySpecHandleAndPredictionKey Key(Handle, OriginalKey);
	if (!TargetDataRequestSequences.Contains(Key) && TargetDataRequestSequences.Num() >= 64)
	{
		ReportTargetDataAdmissionFailure(TEXT("Too many concurrent TargetData activation sequences."), Now);
		return false;
	}
	// Reserve before broadcasting: reentrant callbacks cannot replay the request.
	return TargetDataRequestSequences.FindOrAdd(Key).TryAccept(MessageKey.Current, bCancellation);
}

void URPGAbilitySystemComponent::ServerSetReplicatedEvent_Implementation(EAbilityGenericReplicatedEvent::Type EventType,
	FGameplayAbilitySpecHandle AbilityHandle, FPredictionKey AbilityOriginalPredictionKey, FPredictionKey CurrentPredictionKey)
{
	if (bDungeonGameplayAuthorityRevoked || IsDungeonGameplayRevoked(GetAvatarActor())) return;
	Super::ServerSetReplicatedEvent_Implementation(EventType, AbilityHandle, AbilityOriginalPredictionKey, CurrentPredictionKey);
}

void URPGAbilitySystemComponent::ServerSetReplicatedEventWithPayload_Implementation(EAbilityGenericReplicatedEvent::Type EventType,
	FGameplayAbilitySpecHandle AbilityHandle, FPredictionKey AbilityOriginalPredictionKey, FPredictionKey CurrentPredictionKey,
	FVector_NetQuantize100 VectorPayload)
{
	if (bDungeonGameplayAuthorityRevoked || IsDungeonGameplayRevoked(GetAvatarActor())) return;
	Super::ServerSetReplicatedEventWithPayload_Implementation(EventType, AbilityHandle, AbilityOriginalPredictionKey, CurrentPredictionKey, VectorPayload);
}

void URPGAbilitySystemComponent::ServerSetReplicatedTargetData_Implementation(
	const FGameplayAbilitySpecHandle AbilityHandle, const FPredictionKey AbilityOriginalPredictionKey,
	const FGameplayAbilityTargetDataHandle& TargetData, const FGameplayTag ApplicationTag,
	const FPredictionKey CurrentPredictionKey)
{
	const bool bAdmitted = AdmitTargetDataRequest(AbilityHandle, AbilityOriginalPredictionKey, CurrentPredictionKey, false, &TargetData);
#if !UE_BUILD_SHIPPING
	// Only admitted requests get per-request traces, bounded by the server cap.
	if (bAdmitted && FParse::Param(FCommandLine::Get(), TEXT("RPGSkillNetE2E")))
	{
		UE_LOG(LogTemp, Display, TEXT("RPG_TARGETDATA_ADMISSION Avatar=%s Handle=%s Original=%d Message=%d Kind=Confirm Accepted=%d"),
			*GetNameSafe(GetAvatarActor()), *AbilityHandle.ToString(), AbilityOriginalPredictionKey.Current,
			CurrentPredictionKey.Current, bAdmitted);
	}
#endif
	if (bAdmitted)
	{
		Super::ServerSetReplicatedTargetData_Implementation(AbilityHandle, AbilityOriginalPredictionKey,
			TargetData, ApplicationTag, CurrentPredictionKey);
	}
#if !UE_BUILD_SHIPPING
	URPGSkillNetworkE2EProbeComponent::ObserveTargetData(*this, AbilityHandle, AbilityOriginalPredictionKey,
		TargetData, ApplicationTag, CurrentPredictionKey, false, bAdmitted);
#endif
}

void URPGAbilitySystemComponent::ServerSetReplicatedTargetDataCancelled_Implementation(
	const FGameplayAbilitySpecHandle AbilityHandle, const FPredictionKey AbilityOriginalPredictionKey,
	const FPredictionKey CurrentPredictionKey)
{
	const bool bAdmitted = AdmitTargetDataRequest(AbilityHandle, AbilityOriginalPredictionKey, CurrentPredictionKey, true, nullptr);
#if !UE_BUILD_SHIPPING
	if (bAdmitted && FParse::Param(FCommandLine::Get(), TEXT("RPGSkillNetE2E")))
	{
		UE_LOG(LogTemp, Display, TEXT("RPG_TARGETDATA_ADMISSION Avatar=%s Handle=%s Original=%d Message=%d Kind=Cancel Accepted=%d"),
			*GetNameSafe(GetAvatarActor()), *AbilityHandle.ToString(), AbilityOriginalPredictionKey.Current,
			CurrentPredictionKey.Current, bAdmitted);
	}
#endif
	if (bAdmitted)
	{
		Super::ServerSetReplicatedTargetDataCancelled_Implementation(AbilityHandle,
			AbilityOriginalPredictionKey, CurrentPredictionKey);
	}
}

void URPGAbilitySystemComponent::ServerEndAbility_Implementation(const FGameplayAbilitySpecHandle AbilityToEnd,
	const FGameplayAbilityActivationInfo ActivationInfo, const FPredictionKey PredictionKey)
{
	if (const FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(AbilityToEnd))
	{
		for (const UGameplayAbility* Instance : Spec->GetAbilityInstances())
		{
			const auto* Skill = Cast<URPGGameplayAbility_SkillContainer>(Instance);
			if (Skill && Skill->IsActive() && Skill->GetSkillRuntimeSpec().ExecutionPolicyClass &&
				Skill->GetCurrentActivationInfo().GetActivationPredictionKey() == ActivationInfo.GetActivationPredictionKey())
			{
				// A predicted montage finishing is not permission to end server execution.
				// In GAS, even a natural remote end can force-cancel waiting input tasks.
				// Preserve explicit ServerCancelAbility and unmigrated Action abilities.
#if !UE_BUILD_SHIPPING
				const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
				if (FParse::Param(FCommandLine::Get(), TEXT("RPGSkillNetE2E")) &&
					(LastSkillClientEndTraceAt < 0.0 || Now < LastSkillClientEndTraceAt || Now - LastSkillClientEndTraceAt >= 1.0))
				{
					LastSkillClientEndTraceAt = Now;
					UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_CLIENT_END_IGNORED Avatar=%s Handle=%s Original=%d"),
						*GetNameSafe(GetAvatarActor()), *AbilityToEnd.ToString(), ActivationInfo.GetActivationPredictionKey().Current);
				}
#endif
				return;
			}
		}
	}
	Super::ServerEndAbility_Implementation(AbilityToEnd, ActivationInfo, PredictionKey);
}

void URPGAbilitySystemComponent::NotifyAbilityEnded(const FGameplayAbilitySpecHandle Handle,
	UGameplayAbility* Ability, const bool bWasCancelled)
{
	if (Ability && Ability->IsInstantiated())
	{
		TargetDataRequestSequences.Remove(FGameplayAbilitySpecHandleAndPredictionKey(
			Handle, Ability->GetCurrentActivationInfo().GetActivationPredictionKey()));
	}
	Super::NotifyAbilityEnded(Handle, Ability, bWasCancelled);
}
