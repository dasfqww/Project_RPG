#include "Tests/RPGConsumeRecoveryE2E.h"
#if !UE_BUILD_SHIPPING
#include "Tests/RPGDungeonFlowE2EProbe.h"
#include "AbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGBossCharacter.h"
#include "Component/RPGInventoryProjectionComponent.h"
#include "Component/RPGItemCommandComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "FunctionLibrary/RPGSecurityBlueprintLibrary.h"
#include "GameFramework/PlayerController.h"
#include "GameState/RPGBossBattleGameState.h"
#include "HAL/FileManager.h"
#include "Item/Definition/RPGItemDefinition.h"
#include "Item/Definition/RPGItemDefinitionFragments.h"
#include "Manager/DataManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Player/RPGPlayerState.h"
#include "RPGGameplayTags.h"

FRPGConsumeRecoveryServerProbe::~FRPGConsumeRecoveryServerProbe()
{
	if (auto* ASC = Abilities.Get()) ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(EffectHandle);
}

bool FRPGConsumeRecoveryServerProbe::SameTarget() const
{
	const auto* PC = Controller.Get(); const auto* PS = State.Get(); const auto* Avatar = Pawn.Get();
	return PC && PS && Avatar && Abilities.IsValid() && PC->GetPawn() == Avatar && PC->GetPlayerState<ARPGPlayerState>() == PS &&
		PS->HasAuthenticatedCharacter() && PS->GetBackendCharacterId() == Owner && PS->GetBackendDungeonSessionId() == Session &&
		PS->GetAuthenticatedSteamId() == Steam && PS->GetBackendConnectionGeneration() == Generation &&
		Abilities->GetAvatarActor() == Avatar && PC->GetNetConnection() && PC->GetNetConnection()->GetConnectionState() == USOCK_Open;
}

int32 FRPGConsumeRecoveryServerProbe::Tick(UWorld& World)
{
	const auto Fail = [this](const TCHAR* Why) { Error = Why; return -1; };
	if (!Error.IsEmpty()) return -1;
	if (bStopped) return 0;
	if (World.GetNetMode() != NM_DedicatedServer) return Fail(TEXT("ConsumeObserverNeedsDedicatedServer"));
	if (!bInitialized)
	{
		FString ScopeFile, Contents, Id;
		bDeathMode = FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryRecoveryConsumeDeathE2E"));
		if (bDeathMode && (!FParse::Value(FCommandLine::Get(), TEXT("RPGConsumeDeathFile="), DeathFile) || DeathFile.IsEmpty()))
			return Fail(TEXT("ConsumeObserverMissingDeathGate"));
		if (!FParse::Value(FCommandLine::Get(), TEXT("RPGConsumeScopeFile="), ScopeFile) || ScopeFile.IsEmpty() ||
			!FParse::Value(FCommandLine::Get(), TEXT("RPGConsumeObservationStopFile="), StopFile) || StopFile.IsEmpty())
			return Fail(TEXT("ConsumeObserverMissingScope"));
		if (!IFileManager::Get().FileExists(*ScopeFile)) return 0; // Harness seeds after server readiness, before client admission.
		TSharedPtr<FJsonObject> Scope;
		if (IFileManager::Get().FileSize(*ScopeFile) > 4096 || !FFileHelper::LoadFileToString(Contents, *ScopeFile) ||
			!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Contents), Scope) || !Scope || Scope->Values.Num() != 2 ||
			!Scope->TryGetStringField(TEXT("source"), Id) || !Scope->TryGetStringField(TEXT("owner"), Owner) ||
			!FGuid::ParseExact(Id, EGuidFormats::DigitsWithHyphensLower, Source) || !Source.IsValid() ||
			Id != Source.ToString(EGuidFormats::DigitsWithHyphensLower)) return Fail(TEXT("ConsumeObserverInvalidScopeFile"));
		FGuid OwnerId;
		if (!FGuid::ParseExact(Owner, EGuidFormats::DigitsWithHyphensLower, OwnerId) || !OwnerId.IsValid()) return Fail(TEXT("ConsumeObserverInvalidOwner"));
		bInitialized = true;
	}
	if (IFileManager::Get().FileExists(*StopFile))
	{
		if (bDeathMode)
		{
			if (!bDeathResolved || Effects != 0 || !SameTarget() ||
				Abilities->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) != 0.f)
				return Fail(TEXT("ConsumeObserverStoppedBeforeVerifiedDeathResolution"));
			bStopped = true;
			UE_LOG(LogTemp, Display, TEXT("RPG_CONSUME_RECOVERY_SERVER DEATH_STOPPED Effects=0 SamePawn=1"));
			return 0;
		}
		if (!bHealed || Effects != 1 || !SameTarget()) return Fail(TEXT("ConsumeObserverStoppedBeforeVerifiedEffect"));
		bStopped = true;
		UE_LOG(LogTemp, Display, TEXT("RPG_CONSUME_RECOVERY_SERVER STOPPED Effects=1 SamePawn=1"));
		return 0;
	}
	if (!bArmed)
	{
		APlayerController* PC = nullptr; int32 Count = 0;
		for (auto It = World.GetPlayerControllerIterator(); It; ++It) { PC = It->Get(); ++Count; }
		if (Count == 0) return 0;
		if (Count != 1 || !PC) return Fail(TEXT("ConsumeObserverNeedsOneOwner"));
		auto* PS = PC->GetPlayerState<ARPGPlayerState>(); APawn* Avatar = PC->GetPawn();
		auto* Projection = PC->FindComponentByClass<URPGInventoryProjectionComponent>();
		auto* Commands = PC->FindComponentByClass<URPGItemCommandComponent>();
		if (!PS || !PS->HasAuthenticatedCharacter() || !Avatar || !Projection || Projection->GetLoadState() != ERPGInventoryProjectionLoadState::Ready) return 0;
		if (PS->GetBackendCharacterId() != Owner || !Commands || Commands->HasInFlightCommands()) return Fail(TEXT("ConsumeObserverOwnerOrPendingChanged"));
		auto* ASC = Avatar->FindComponentByClass<UAbilitySystemComponent>();
		if (!ASC || !ASC->GetSet<URPGAttributeSet>()) return 0;
		FRPGInventoryProjectionEntry Item;
		if (!Projection->FindProjectedItem(Source, Item) || Item.GetQuantity() != 7 || Item.GetRevision() != 1 || Item.GetSlotIndex() != 0 ||
			Item.GetDefinitionId() != FPrimaryAssetId(TEXT("RPGItemDefinition"), TEXT("GameItem.Consume.Potion.Red.Large"))) return Fail(TEXT("ConsumeObserverOriginalItemMismatch"));
		const auto* Data = World.GetGameInstance()->GetSubsystem<UDataManager>();
		const auto* Definition = Data ? Data->FindNativeItemDefinition(Item.GetDefinitionId()) : nullptr;
		const auto* Fragment = Definition ? Definition->FindFragmentByClass<URPGItemConsumableDefinitionFragment>() : nullptr;
		if (!Definition || Definition->GetDefinitionVersion() != Item.GetDefinitionVersion() || !Fragment || !Fragment->GameplayEffect || Fragment->QuantityPerUse != 1)
			return Fail(TEXT("ConsumeObserverDefinitionMissing"));
		const auto* Battle = World.GetGameState<ARPGBossBattleGameState>();
		auto* Boss = Battle ? Battle->GetBattleSnapshot().Boss.Get() : nullptr;
		auto* BossASC = Boss ? Boss->GetAbilitySystemComponent() : nullptr;
		if (!Boss || !BossASC || !Boss->IsNPCStartUpDataReady()) return 0;
		const float Health = ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
		const float Max = ASC->GetNumericAttribute(URPGAttributeSet::GetMaxHealthAttribute());
		const float Attack = BossASC->GetNumericAttribute(URPGAttributeSet::GetAttackAttribute());
		const float Defense = ASC->GetNumericAttribute(URPGAttributeSet::GetDefenseAttribute());
		const float Crit = BossASC->GetNumericAttribute(URPGAttributeSet::GetCriticalDamageAttribute());
		if (!FMath::IsFinite(Health) || !FMath::IsFinite(Max) || Health <= 10.f || Max < Health ||
			!FMath::IsFinite(Attack) || Attack <= 0.f || !FMath::IsFinite(Defense) || Defense <= 0.f || !FMath::IsFinite(Crit) || Crit <= 0.f)
			return Fail(TEXT("ConsumeObserverInvalidStartupAttributes"));
		FRPGSkillSecurityProfile Profile;
		const float Desired = Health * .2f;
		Profile.MaximumFinalDamagePerHit = FMath::Min(FRPGSkillSecurityProfile::MaximumSupportedFinalDamage, FMath::Max(100.f, Desired + 2.f));
		FActiveGameplayEffectHandle Damage; FText ErrorText;
		if (!URPGSecurityBlueprintLibrary::ApplyAuthorizedServerDamage(Boss, FHitResult(Avatar, nullptr, Avatar->GetActorLocation(), FVector::ForwardVector),
			URPGDungeonFlowE2EDamageEffect::StaticClass(), Desired * Defense / (Attack * FMath::Max(1.f, Crit)), FGameplayTag(), Profile, Damage, ErrorText))
			return Fail(TEXT("ConsumeObserverAuthorizedDamageRejected"));
		HealthBefore = ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
		if (!FMath::IsFinite(HealthBefore) || HealthBefore <= 0.f || HealthBefore >= Health) return Fail(TEXT("ConsumeObserverDamageNotObserved"));
		Controller = PC; State = PS; Pawn = Avatar; Abilities = ASC; Session = PS->GetBackendDungeonSessionId(); Steam = PS->GetAuthenticatedSteamId();
		Generation = PS->GetBackendConnectionGeneration(); PotionEffect = Fragment->GameplayEffect->GetDefaultObject<UGameplayEffect>();
		if (!SameTarget()) return Fail(TEXT("ConsumeObserverOriginalTargetMissing"));
		EffectHandle = ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda([this](UAbilitySystemComponent* AppliedASC, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
		{
			if (Spec.Def != PotionEffect.Get()) return;
			++Effects;
			if (bDeathMode) { Error = TEXT("ConsumeObserverPotionEffectDuringDeathRecovery"); return; }
			auto* CurrentCommands = Controller.IsValid() ? Controller->FindComponentByClass<URPGItemCommandComponent>() : nullptr;
			if (!SameTarget() || AppliedASC != Abilities.Get() || Effects != 1 || !bLockedObserved || !CurrentCommands ||
				CurrentCommands->InventoryRecovery.Num() != 1 || CurrentCommands->InventoryRecovery[0].Operation != TEXT("Item.Consume"))
			{ Error = TEXT("ConsumeObserverDuplicateOrUnscopedEffect"); return; }
			Request = CurrentCommands->InventoryRecovery[0].RequestId;
			UE_LOG(LogTemp, Display, TEXT("RPG_CONSUME_RECOVERY_SERVER EFFECT Request=%s Source=%s Effects=1 SamePawn=1"),
				*Request.ToString(EGuidFormats::DigitsWithHyphensLower), *Source.ToString(EGuidFormats::DigitsWithHyphensLower));
		});
		bArmed = true;
		UE_LOG(LogTemp, Display, TEXT("RPG_CONSUME_RECOVERY_SERVER ARMED Source=%s Health=%g Max=%g AuthorizedDamage=1 DirectHPWrite=0 Effects=0"),
			*Source.ToString(EGuidFormats::DigitsWithHyphensLower), HealthBefore, Max);
	}
	if (!SameTarget()) return Fail(TEXT("ConsumeObserverOriginalTargetChanged"));
	auto* Commands = Controller->FindComponentByClass<URPGItemCommandComponent>();
	const float Health = Abilities->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
	if (!FMath::IsFinite(Health) || Effects > 1) return Fail(TEXT("ConsumeObserverInvalidHealthOrEffectCount"));
	if (Effects == 0 && !bLockedObserved && Commands && Commands->InventoryRecovery.Num() == 1)
	{
		const auto& Pending = Commands->InventoryRecovery[0];
		if (Pending.Operation != TEXT("Item.Consume") || !Pending.RequestId.IsValid()) return Fail(TEXT("ConsumeObserverWrongPendingOperation"));
		Request = Pending.RequestId;
		bLockedObserved = true;
		UE_LOG(LogTemp, Display, TEXT("RPG_CONSUME_RECOVERY_SERVER LOCKED Request=%s Health=%g Effects=0 SamePawn=1"),
			*Request.ToString(EGuidFormats::DigitsWithHyphensLower), Health);
	}
	if (bDeathMode)
	{
		if (Effects != 0 || !Commands) return Fail(TEXT("ConsumeObserverDeathEffectOrCommandsChanged"));
		if (!bKilled)
		{
			if (!FMath::IsNearlyEqual(Health, HealthBefore)) return Fail(TEXT("ConsumeObserverHealthChangedBeforeDeathGate"));
			if (!IFileManager::Get().FileExists(*DeathFile)) return 0;
			FString Contents, GateRequest, GateSource, GateOwner;
			TSharedPtr<FJsonObject> Gate;
			if (!bLockedObserved || Commands->InventoryRecovery.Num() != 1 ||
				Commands->InventoryRecovery[0].RequestId != Request || Commands->InventoryRecovery[0].Operation != TEXT("Item.Consume") ||
				!Commands->InventoryRecovery[0].bRetrying || !Commands->HasInFlightCommands() ||
				IFileManager::Get().FileSize(*DeathFile) > 4096 || !FFileHelper::LoadFileToString(Contents, *DeathFile) ||
				!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Contents), Gate) || !Gate || Gate->Values.Num() != 3 ||
				!Gate->TryGetStringField(TEXT("request"), GateRequest) || GateRequest != Request.ToString(EGuidFormats::DigitsWithHyphensLower) ||
				!Gate->TryGetStringField(TEXT("source"), GateSource) || GateSource != Source.ToString(EGuidFormats::DigitsWithHyphensLower) ||
				!Gate->TryGetStringField(TEXT("owner"), GateOwner) || GateOwner != Owner)
				return Fail(TEXT("ConsumeObserverDeathGateOrOriginalRetryMismatch"));
			const auto* Battle = World.GetGameState<ARPGBossBattleGameState>();
			auto* Boss = Battle ? Battle->GetBattleSnapshot().Boss.Get() : nullptr;
			auto* BossASC = Boss ? Boss->GetAbilitySystemComponent() : nullptr;
			if (!BossASC) return Fail(TEXT("ConsumeObserverDeathDamageSourceMissing"));
			const float Attack = BossASC->GetNumericAttribute(URPGAttributeSet::GetAttackAttribute());
			const float Defense = Abilities->GetNumericAttribute(URPGAttributeSet::GetDefenseAttribute());
			const float Crit = BossASC->GetNumericAttribute(URPGAttributeSet::GetCriticalDamageAttribute());
			if (Health <= 0.f || !FMath::IsFinite(Attack) || Attack <= 0.f || !FMath::IsFinite(Defense) || Defense <= 0.f ||
				!FMath::IsFinite(Crit) || Crit <= 0.f) return Fail(TEXT("ConsumeObserverInvalidDeathDamageAttributes"));
			// Cover both critical outcomes using the existing approved damage path.
			const float Desired = Health + 2.f;
			const float MinimumCrit = FMath::Min(1.f, Crit);
			const float Base = Desired * Defense / (Attack * MinimumCrit);
			const float Maximum = Desired * FMath::Max(1.f, Crit) / MinimumCrit + 2.f;
			if (!FMath::IsFinite(Base) || Base <= 0.f || !FMath::IsFinite(Maximum) ||
				Maximum > FRPGSkillSecurityProfile::MaximumSupportedFinalDamage) return Fail(TEXT("ConsumeObserverDeathDamageOutsideProfile"));
			FRPGSkillSecurityProfile Profile;
			Profile.MaximumFinalDamagePerHit = FMath::Max(100.f, Maximum);
			FActiveGameplayEffectHandle Damage; FText ErrorText;
			if (!URPGSecurityBlueprintLibrary::ApplyAuthorizedServerDamage(Boss,
				FHitResult(Pawn.Get(), nullptr, Pawn->GetActorLocation(), FVector::ForwardVector),
				URPGDungeonFlowE2EDamageEffect::StaticClass(), Base, FGameplayTag(), Profile, Damage, ErrorText) ||
				!SameTarget() || Abilities->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) != 0.f)
				return Fail(TEXT("ConsumeObserverAuthorizedDeathDamageNotObserved"));
			bKilled = true;
			UE_LOG(LogTemp, Display, TEXT("RPG_CONSUME_RECOVERY_SERVER DEAD Request=%s Source=%s Health=0 Effects=0 SamePawn=1 AuthorizedDamage=1 DirectHPWrite=0"),
				*Request.ToString(EGuidFormats::DigitsWithHyphensLower), *Source.ToString(EGuidFormats::DigitsWithHyphensLower));
			return 0;
		}
		if (Health != 0.f) return Fail(TEXT("ConsumeObserverDeathRecoveryHealed"));
		if (!bDeathResolved && Commands->InventoryRecovery.IsEmpty() && !Commands->HasInFlightCommands())
		{
			const auto* Projection = Controller->FindComponentByClass<URPGInventoryProjectionComponent>();
			FRPGInventoryProjectionEntry Item;
			if (!Projection || !Projection->FindProjectedItem(Source, Item) || Item.GetQuantity() != 6 || Item.GetRevision() != 2 || Item.GetSlotIndex() != 0)
				return Fail(TEXT("ConsumeObserverDeathResolutionProjectionMismatch"));
			bDeathResolved = true;
			UE_LOG(LogTemp, Display, TEXT("RPG_CONSUME_RECOVERY_SERVER DEATH_RESOLVED Request=%s Health=0 Effects=0 SamePawn=1 Quantity=6 Revision=2"),
				*Request.ToString(EGuidFormats::DigitsWithHyphensLower));
		}
		return 0;
	}
	if (Effects == 0)
	{
		if (!FMath::IsNearlyEqual(Health, HealthBefore)) return Fail(TEXT("ConsumeObserverEffectBeforeReceipt"));
	}
	else if (!bHealed && Health > HealthBefore)
	{
		bHealed = true;
		UE_LOG(LogTemp, Display, TEXT("RPG_CONSUME_RECOVERY_SERVER HEALED Request=%s HealthBefore=%g HealthAfter=%g Effects=1 SamePawn=1"),
			*Request.ToString(EGuidFormats::DigitsWithHyphensLower), HealthBefore, Health);
	}
	return 0;
}
#else
FRPGConsumeRecoveryServerProbe::~FRPGConsumeRecoveryServerProbe() = default;
bool FRPGConsumeRecoveryServerProbe::SameTarget() const { return false; }
int32 FRPGConsumeRecoveryServerProbe::Tick(UWorld&) { return -1; }
#endif
