#include "PlayerClass/Development/RPGPlayerClassDevelopmentComponent.h"

#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "PlayerClass/Development/RPGPlayerClassDevelopmentProfile.h"
#include "PlayerClass/RPGPlayerClassComponent.h"
#include "PlayerClass/RPGPlayerClassDefinition.h"
#include "Player/RPGPlayerState.h"
#include "Security/RPGDungeonGameplayAuthority.h"

URPGPlayerClassDevelopmentComponent::URPGPlayerClassDevelopmentComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void URPGPlayerClassDevelopmentComponent::BeginPlay()
{
	Super::BeginPlay();
#if WITH_EDITOR
	if (!GetOwner() || !GetOwner()->HasAuthority() || !GetWorld() || !GetWorld()->IsGameWorld()) return;
	FString ProfilePath;
	if (!FParse::Value(FCommandLine::Get(), TEXT("RPGDevelopmentClassProfile="), ProfilePath)) return;
	RequestedProfile = FSoftObjectPath(ProfilePath);
	if (!RequestedProfile.IsValid() || !FPackageName::IsValidLongPackageName(RequestedProfile.GetLongPackageName()))
	{
		FailInitialization(FText::FromString(TEXT("Development class profile must be a valid asset object path.")));
		return;
	}
	ClassOwner = GetOwner()->FindComponentByClass<URPGPlayerClassComponent>();
	if (!ClassOwner.IsValid()) { FailInitialization(FText::FromString(TEXT("Class application owner is missing."))); return; }
	ApplicationChangedHandle = ClassOwner->OnApplicationChanged.AddUObject(this, &ThisClass::TryInitialize);
	if (auto* Pawn = Cast<APawn>(GetOwner()))
		Pawn->ReceiveRestartedDelegate.AddUniqueDynamic(this, &ThisClass::HandlePawnRestarted);
	TryInitialize();
#endif
}

void URPGPlayerClassDevelopmentComponent::HandlePawnRestarted(APawn* Pawn)
{
	if (Pawn == GetOwner()) TryInitialize();
}

bool URPGPlayerClassDevelopmentComponent::HasCurrentOwnerContext() const
{
	const auto* Pawn = Cast<APawn>(GetOwner());
	return IsRegistered() && IsValid(Pawn) && !Pawn->IsActorBeingDestroyed() && Pawn->HasAuthority()
		&& !IsDungeonGameplayRevoked(Pawn) && GetWorld() && !GetWorld()->bIsTearingDown
		&& ExpectedWorld.Get() == GetWorld() && Pawn->GetController() == ExpectedController.Get()
		&& ExpectedController.IsValid() && ExpectedController->GetPawn() == Pawn
		&& Pawn->GetPlayerState() == ExpectedPlayerState.Get() && ClassOwner.IsValid()
		&& (!Pawn->GetPlayerState<ARPGPlayerState>() || !Pawn->GetPlayerState<ARPGPlayerState>()->HasAuthenticatedCharacter());
}

void URPGPlayerClassDevelopmentComponent::TryInitialize()
{
	if (bFinished || bInitializing || RequestedProfile.IsNull() || !ClassOwner.IsValid()
		|| !ClassOwner->IsAbilitySystemReady()) return;
	auto* Pawn = Cast<APawn>(GetOwner());
	auto* Growth = Pawn ? Pawn->FindComponentByClass<URPGPlayerSkillComponent>() : nullptr;
	auto* Slots = Pawn ? Pawn->FindComponentByClass<UQuickSlotComponent>() : nullptr;
	if (!IsValid(Pawn) || !Pawn->HasAuthority() || IsDungeonGameplayRevoked(Pawn) || !Growth || !Slots) return;
	if (const auto* State = Pawn->GetPlayerState<ARPGPlayerState>(); State && State->HasAuthenticatedCharacter()) return;
	if (!Pawn->GetController() || Pawn->GetController()->GetPawn() != Pawn) return;
	if (!bRequestStarted)
	{
		// Never use a development profile after real restore has begun, failed or completed.
		if (Growth->GetSkillLoadState() != ERPGPlayerSkillLoadState::Uninitialized || Slots->HasInitializedSkillSlots())
		{
			FailInitialization(FText::FromString(TEXT("Development profile cannot replace an existing character restore.")));
			return;
		}
		bRequestStarted = true;
		ExpectedWorld = GetWorld();
		ExpectedController = Pawn->GetController();
		ExpectedPlayerState = Pawn->GetPlayerState();
		if (!HasCurrentOwnerContext()) { FailInitialization(FText::FromString(TEXT("Development Pawn identity is not ready."))); return; }
		const uint64 Serial = ++LoadSerial;
		LoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(RequestedProfile,
			FStreamableDelegate::CreateWeakLambda(this, [this, Serial]() { HandleProfileLoaded(Serial); }));
		if (bFinished) LoadHandle.Reset(); // Completion can be synchronous when streamable delegate delay is disabled.
		else if (!LoadHandle.IsValid()) FailInitialization(FText::FromString(TEXT("Could not request the development class profile.")));
		return;
	}
	if (!LoadedProfile) return;
	TGuardValue<bool> Guard(bInitializing, true);
	if (!HasCurrentOwnerContext() || Growth->GetSkillLoadState() != ERPGPlayerSkillLoadState::Uninitialized)
	{
		FailInitialization(FText::FromString(TEXT("Development profile completion no longer belongs to this Pawn/restore.")));
		return;
	}
	FRPGPlayerSkillRestoreData Record;
	FText Error;
	if (!LoadedProfile->BuildRestoreData(Record, Error)) { FailInitialization(Error); return; }
	const TArray<FGameplayTag> InitialSlots = LoadedProfile->SkillSlots;
	RestoreRequestId = Growth->BeginClassSkillRestore(Record.ClassId);
	if (!RestoreRequestId || !HasCurrentOwnerContext()
		|| !Growth->CompleteClassSkillRestore(RestoreRequestId, LoadedProfile->ClassDefinition, Record, Error))
	{
		FailInitialization(Error.IsEmpty() ? FText::FromString(TEXT("Development growth restore could not begin.")) : Error);
		return;
	}
	if (bFinished) return; // Growth/grant callbacks may have ended this Pawn.
	if (!HasCurrentOwnerContext()) { FailInitialization(FText::FromString(TEXT("Development identity changed during class grants."))); return; }
	ClassOwner->RefreshAppliedClass();
	if (!HasCurrentOwnerContext() || ClassOwner->GetApplyState() != ERPGPlayerClassApplyState::Applied
		|| !Slots->InitializeSkillSlotsFromRecord(RestoreRequestId, InitialSlots, Error))
	{
		FailInitialization(Error.IsEmpty() ? FText::FromString(TEXT("Development class application did not complete.")) : Error);
		return;
	}
	if (bFinished) return; // Slot presentation delegates can also tear down the Pawn.
	if (!HasCurrentOwnerContext()) { FailInitialization(FText::FromString(TEXT("Development identity changed during slot restore."))); return; }
	bFinished = true;
	UE_LOG(LogTemp, Display, TEXT("RPG_CLASS_DEVELOPMENT_INITIALIZED Pawn=%s Class=%s Acquired=%d SlotRecords=%d"),
		*GetNameSafe(Pawn), *Record.ClassId.ToString(), Record.AcquiredSkills.Num(), InitialSlots.Num());
}

void URPGPlayerClassDevelopmentComponent::HandleProfileLoaded(const uint64 Serial)
{
	if (bFinished || Serial != LoadSerial) return;
	if (!HasCurrentOwnerContext()) { FailInitialization(FText::FromString(TEXT("Stale development profile completion was discarded."))); return; }
	LoadedProfile = Cast<URPGPlayerClassDevelopmentProfile>(RequestedProfile.ResolveObject());
	LoadHandle.Reset();
	if (!LoadedProfile) { FailInitialization(FText::FromString(TEXT("Development profile is missing or has the wrong type."))); return; }
	TryInitialize();
}

void URPGPlayerClassDevelopmentComponent::FailInitialization(const FText& Error)
{
	if (bFinished) return;
	bFinished = true; // No fallback, retry or recursive initialization through cleanup callbacks.
	if (RestoreRequestId && GetOwner() && GetOwner()->HasAuthority())
	{
		if (auto* Growth = GetOwner()->FindComponentByClass<URPGPlayerSkillComponent>();
			Growth && Growth->GetSkillRestoreGeneration() == RestoreRequestId)
		{
			Growth->InvalidateClassSkillRestore(RestoreRequestId);
			if (ClassOwner.IsValid()) ClassOwner->ReleaseClassAbilities();
		}
	}
	UE_LOG(LogTemp, Error, TEXT("RPG_CLASS_DEVELOPMENT_FAILED Pawn=%s Reason=%s"), *GetNameSafe(GetOwner()), *Error.ToString());
}

void URPGPlayerClassDevelopmentComponent::StopPendingInitialization()
{
	bFinished = true;
	++LoadSerial;
	if (LoadHandle.IsValid()) LoadHandle->CancelHandle();
	LoadHandle.Reset();
	if (ClassOwner.IsValid()) ClassOwner->OnApplicationChanged.Remove(ApplicationChangedHandle);
	if (auto* Pawn = Cast<APawn>(GetOwner()))
		Pawn->ReceiveRestartedDelegate.RemoveDynamic(this, &ThisClass::HandlePawnRestarted);
	ApplicationChangedHandle.Reset();
	ClassOwner.Reset();
	LoadedProfile = nullptr;
}

void URPGPlayerClassDevelopmentComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	StopPendingInitialization();
	Super::EndPlay(Reason);
}

void URPGPlayerClassDevelopmentComponent::OnUnregister()
{
	StopPendingInitialization();
	Super::OnUnregister();
}
