#include "Tests/RPGDungeonFlowE2EProbe.h"
#include "Tests/RPGInventoryStackMergeE2E.h"
#include "Tests/RPGInventoryStackSplitE2E.h"
#include "Tests/RPGConsumeRecoveryE2E.h"

#include "AI/RPGBossAIComponent.h"
#include "AI/RPGBossThreatComponent.h"
#include "AIController.h"
#include "Ability/RPGNPCGameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "BrainComponent.h"
#include "Character/RPGBossCharacter.h"
#include "Character/RPGPlayer.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/RPGInventoryProjectionComponent.h"
#include "Component/Combat/PlayerCombatComponent.h"
#include "Component/Equipment/RPGAuthoritativeEquipmentComponent.h"
#include "Component/Equipment/RPGPlayerWeaponLoadoutComponent.h"
#include "Components/Button.h"
#include "Components/CapsuleComponent.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Controller/RPGPlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/NetDriver.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "DataAsset/Input/DataAsset_InputConfig.h"
#include "Item/Weapon/RPGPlayerWeapon.h"
#include "Sound/SoundClass.h"
#include "GameInstance/RPGGameInstance.h"
#include "GameMode/RPGBossBattleGameMode.h"
#include "GameState/RPGBossBattleGameState.h"
#include "GameplayEffect/GE_ExecCalc_TakeDamage.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Manager/HttpWebManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Online/RPGDungeonDepartureJournal.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "Online/RPGOnlineLobbySubsystem.h"
#include "Online/RPGSteamAuthSubsystem.h"
#include "Player/RPGPlayerState.h"
#include "Item/Persistence/RPGItemRecord.h"
#include "Tests/RPGDungeonEjectionNetworkProbe.h"
#include "RPGGameplayTags.h"
#include "UI/Content/RPGDungeonResultActionsWidget.h"
#include "UI/Lobby/RPGOnlineLobbyWidget.h"

namespace
{
	bool Enabled()
	{
#if UE_BUILD_SHIPPING
		return false;
#else
		return FParse::Param(FCommandLine::Get(), TEXT("RPGDungeonFlowE2E"))
			&& FParse::Param(FCommandLine::Get(), TEXT("nosteam"));
#endif
	}
	bool Same(const FString& A, const FString& B) { return A.Equals(B, ESearchCase::CaseSensitive); }
	FString FlowFixtureMapPackage(const UWorld& World)
	{
		const FString Package = World.GetOutermost()->GetName();
		// A rendered Editor client travels to UEDPIE_<id>_LobbyMap. Normalize
		// only actual PIE worlds; a differently named game map is not accepted.
		return World.WorldType == EWorldType::PIE ? UWorld::RemovePIEPrefix(Package) : Package;
	}
	double FlowFixtureTimeout()
	{
#if !UE_BUILD_SHIPPING
		// Opt-in real-clock grace test only; production grace and authored timer stay unchanged.
		if (Enabled() && FParse::Param(FCommandLine::Get(), TEXT("RPGDungeonUnexpectedDisconnectE2E"))) return 1200.0;
#endif
		return 600.0;
	}
	// Authored fresh-character fixture, not a client-side reward or difficulty override.
	struct FRewardExpectation
	{
		const TCHAR* Difficulty;
		const TCHAR* Version;
		int64 Gold;
		int32 Potions;
		int32 Helms;
	};
	const FRewardExpectation* FindRewardExpectation(const FString& Difficulty)
	{
		static const FRewardExpectation Expectations[] = {
			{ TEXT("Easy"), TEXT("pve_easy_v1"), 100, 1, 0 },
			{ TEXT("Normal"), TEXT("pve_normal_v1"), 250, 2, 0 },
			{ TEXT("Hard"), TEXT("pve_hard_v1"), 500, 3, 1 },
			{ TEXT("Hell"), TEXT("pve_hell_v1"), 1000, 5, 1 }
		};
		for (const auto& Expected : Expectations)
			if (Same(Difficulty, Expected.Difficulty)) return &Expected;
		return nullptr;
	}
	bool ObserveAbandonmentSimulation(UWorld* World, const TCHAR* Role, int32 PlayerId)
	{
#if !UE_BUILD_SHIPPING && DO_ENABLE_NET_TEST
		const UNetDriver* Driver = World ? World->GetNetDriver() : nullptr;
		if (!Driver || !Driver->IsUsingIrisReplication()) return false;
		const auto& Sim = Driver->PacketSimulationSettings;
		if (Sim.PktLagMin != 80 || Sim.PktLagMax != 160 || Sim.PktLoss != 5 || Sim.PktLag || Sim.PktOrder || Sim.PktDup
			|| Sim.PktIncomingLagMin || Sim.PktIncomingLagMax || Sim.PktIncomingLoss) return false;
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_ABANDON_SETTLEMENT SIMULATION Role=%s PlayerId=%d Iris=1 LagMin=80 LagMax=160 Loss=5"), Role, PlayerId);
		return true;
#else
		return false;
#endif
	}
}

bool URPGDungeonFlowE2EClient::ShouldCreateSubsystem(UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer) && Enabled() && !IsRunningDedicatedServer()
		&& !FParse::Param(FCommandLine::Get(), TEXT("RPGItemE2EServer"));
}

void URPGDungeonFlowE2EClient::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<URPGSteamAuthSubsystem>();
	Collection.InitializeDependency<URPGDungeonSessionSubsystem>();
	Collection.InitializeDependency<UHttpWebManager>();
	FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2ECharacter="), CharacterId);
	FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2ESteam="), SteamId);
	FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2ESession="), SessionId);
	bLobbyUI = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2ELobbyUI"));
	bLobbyCreationOnly = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2ELobbyCreationOnly"));
	bLobbyConfigurationOnly = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2ELobbyConfigurationOnly"));
	FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2EConfigurationOtherCharacter="), ConfigurationOtherCharacterId);
	bLobbyDifficultyOnly = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2ELobbyDifficultyOnly"));
	FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2ELobbyDifficulty="), LobbyDifficulty);
	bVerifyLobbyLogout = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2ELobbyLogout"));
	FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2ELogoutNextSteam="), LogoutNextSteamId);
	FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2ELogoutNextCharacter="), LogoutNextCharacterId);
	bNaturalPlayerAttack = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2ENaturalPlayerAttack"));
	bAbandonmentUI = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2EAbandonmentUI"));
	bAbandonmentReauthenticate = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2EAbandonmentReauthenticate"));
	bVerifyAbandonedReturn = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2EVerifyAbandonedReturn"));
	bVerifyAbandonedReceipt = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2EVerifyAbandonedReceipt"));
	bVerifyDungeonClientInitialAbandonment = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2EInitialAbandonmentFromDungeon"));
	bResultAbandonment = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2EResultAbandonment"));
	bGameplayAbandonment = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2EGameplayAbandonment"));
	bGameplayAbandonmentResponseLoss = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2EGameplayAbandonmentResponseLoss"));
	FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2EGameplayConsentGate="), GameplayConsentGateFile);
	FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2EGameplayConfirmGate="), GameplayConfirmGateFile);
	bSlatePointerInput = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2ESlatePointerInput"));
	bConfirmationNavigation = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2EConfirmationNavigation"));
	if (bConfirmationNavigation && !bSlatePointerInput)
	{ Fail(TEXT("ConfirmationNavigationRequiresSlatePointerInput")); return; }
	if (bSlatePointerInput && (!bResultAbandonment || !FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen"))
		|| FParse::Param(FCommandLine::Get(), TEXT("nullrhi"))))
	{ Fail(TEXT("PointerInputRequiresRenderedResultFixture")); return; }
	bResultAbandonmentResponseLoss = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2EResultAbandonmentResponseLoss"));
	FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2EResultAbandonCharacter="), ResultAbandonCharacter);
	FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2EResultRetryGate="), ResultRetryGateFile);
	FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2EInitialAbandonmentGateFile="), InitialAbandonmentGateFile);
	FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2EAbandonmentStartFile="), AbandonmentStartFile);
	FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2EAbandonmentRetryFile="), AbandonmentRetryFile);
	FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2EAbandonmentRenewedRetryFile="), AbandonmentRenewedRetryFile);
	bLobbyLeader = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2ELobbyLeader"));
	bCreateCharacter = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2ECreateCharacter"));
	FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2ECharacterName="), CharacterName);
	FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2ESessionFile="), SessionFile);
	FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2EServerReadyFile="), ServerReadyFile);
	FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2ENaturalGateFile="), NaturalAttackGateFile);
	bPartyHudInput = FParse::Param(FCommandLine::Get(), TEXT("RPGPartyHudInputE2E"));
	FParse::Value(FCommandLine::Get(), TEXT("RPGPartyHudInputGate="), PartyHudInputGate);
	bReauthenticate = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2EReauthenticate"));
	FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2EAuthExpiredFile="), AuthExpiredFile);
	bReauthenticateAfterAdmission = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2EReauthenticateAfterAdmission"));
	FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2EDungeonAuthExpiredFile="), DungeonAuthExpiredFile);
	Deadline = FPlatformTime::Seconds() + FlowFixtureTimeout();
	if (bVerifyLobbyLogout || bLobbyConfigurationOnly)
	{
		Collection.InitializeDependency<URPGOnlineLobbySubsystem>();
		if (bVerifyLobbyLogout)
			GetGameInstance()->GetSubsystem<URPGOnlineLobbySubsystem>()->OnLogoutCompleted.AddUniqueDynamic(
				this, &ThisClass::HandleLobbyLogout);
	}
	else
		GetGameInstance()->GetSubsystem<URPGSteamAuthSubsystem>()->OnLoginCompleted.AddUniqueDynamic(
			this, &ThisClass::HandleLogin);
	if (bCreateCharacter)
		GetGameInstance()->GetSubsystem<UHttpWebManager>()->OnCharacterCreated.AddUniqueDynamic(
			this, &ThisClass::HandleCharacterCreated);
	GetGameInstance()->GetSubsystem<URPGDungeonSessionSubsystem>()->OnLobbyTravelStarted.AddUniqueDynamic(
		this, &ThisClass::HandleLobbyTravel);
	if (bVerifyDungeonClientInitialAbandonment)
		GetGameInstance()->GetSubsystem<UHttpWebManager>()->OnDungeonAbandonmentCompleted.AddUniqueDynamic(
			this, &ThisClass::HandleInitialAbandonment);
	if (bResultAbandonment)
		GetGameInstance()->GetSubsystem<UHttpWebManager>()->OnDungeonAbandonmentCompleted.AddUniqueDynamic(
			this, &ThisClass::HandleResultAbandonment);
	if (bGameplayAbandonment)
		GetGameInstance()->GetSubsystem<UHttpWebManager>()->OnDungeonAbandonmentCompleted.AddUniqueDynamic(
			this, &ThisClass::HandleGameplayAbandonment);
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &ThisClass::TickProbe), .1f);
}

void URPGDungeonFlowE2EClient::Deinitialize()
{
	StopPartyHudInput();
	InventoryStackMergeProbe.Reset();
	InventoryStackSplitProbe.Reset();
	StopNaturalPlayerAttack();
	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	if (LogoutCredentialProbe.IsValid())
	{
		LogoutCredentialProbe->OnProcessRequestComplete().Unbind();
		LogoutCredentialProbe->CancelRequest();
		LogoutCredentialProbe.Reset();
	}
	if (auto* Lobby = GetGameInstance()->GetSubsystem<URPGOnlineLobbySubsystem>())
		Lobby->OnLogoutCompleted.RemoveDynamic(this, &ThisClass::HandleLobbyLogout);
	if (auto* Auth = GetGameInstance()->GetSubsystem<URPGSteamAuthSubsystem>())
		Auth->OnLoginCompleted.RemoveDynamic(this, &ThisClass::HandleLogin);
	if (auto* Web = GetGameInstance()->GetSubsystem<UHttpWebManager>())
	{
		Web->OnCharacterCreated.RemoveDynamic(this, &ThisClass::HandleCharacterCreated);
		Web->OnDungeonAbandonmentCompleted.RemoveDynamic(this, &ThisClass::HandleInitialAbandonment);
		Web->OnDungeonAbandonmentCompleted.RemoveDynamic(this, &ThisClass::HandleResultAbandonment);
		Web->OnDungeonAbandonmentCompleted.RemoveDynamic(this, &ThisClass::HandleGameplayAbandonment);
	}
	if (auto* Flow = GetGameInstance()->GetSubsystem<URPGDungeonSessionSubsystem>())
		Flow->OnLobbyTravelStarted.RemoveDynamic(this, &ThisClass::HandleLobbyTravel);
	Super::Deinitialize();
}

bool URPGDungeonFlowE2EClient::Fail(const TCHAR* Reason)
{
	StopPartyHudInput();
	StopNaturalPlayerAttack();
	UE_LOG(LogTemp, Error, TEXT("DUNGEON_FLOW_E2E_FAIL Reason=%s ClientStage=%d"), Reason, Stage);
	Stage = -1;
	return false;
}

void URPGDungeonFlowE2EClient::StopNaturalPlayerAttack()
{
	if (auto* Input = NaturalAttackInput.Get())
	{
		if (auto* Action = NaturalAttackMoveAction.Get()) Input->StopContinuousInputInjectionForAction(Action);
		if (auto* Action = NaturalAttackAction.Get()) Input->StopContinuousInputInjectionForAction(Action);
	}
	NaturalAttackInput.Reset();
	NaturalAttackMoveAction.Reset();
	NaturalAttackAction.Reset();
}

void URPGDungeonFlowE2EClient::TickNaturalPlayerAttack(UWorld* World, APlayerController* PC)
{
	if (!bNaturalPlayerAttack || Stage != 3 || NaturalAttackGateFile.IsEmpty()
		|| !IFileManager::Get().FileExists(*NaturalAttackGateFile)) return;
	const auto* Battle = World->GetGameState<ARPGBossBattleGameState>();
	if (!Battle || Battle->GetBattleSnapshot().BattleState != EBossBattleState::InProgress) return;
	ARPGBossCharacter* Boss = Battle->GetBattleSnapshot().Boss;
	ARPGPlayer* Player = Cast<ARPGPlayer>(PC->GetPawn());
	if (!IsValid(Boss) || !Player || !PC->IsLocalController() || !PC->GetLocalPlayer()) return;
	auto* Weapon = Player->GetPlayerCombatComponent()->GetPlayerCurrentEquippedWeapon();
	auto* ASC = Player->GetAbilitySystemComponent();
	const FGameplayTag AttackTag = RPGGameplayTags::InputTag_LightAttack_GreatSword;
	const FGameplayAbilitySpec* Spec = ASC ? ASC->GetActivatableAbilities().FindByPredicate([&](const FGameplayAbilitySpec& Entry)
	{
		return Entry.SourceObject.Get() == Weapon && Entry.GetDynamicSpecSourceTags().HasTagExact(AttackTag);
	}) : nullptr;
	const auto* Equipment = Player->FindComponentByClass<URPGAuthoritativeEquipmentComponent>();
	const auto* Loadout = Player->FindComponentByClass<URPGPlayerWeaponLoadoutComponent>();
	if (!Weapon || !Spec || !Equipment || !Equipment->HasWeaponSnapshots() || !Loadout || !Loadout->IsNativeManaged()) return;
	if (!NaturalAttackInput.IsValid())
	{
		const auto* Config = LoadObject<UDataAsset_InputConfig>(nullptr,
			TEXT("/Game/Blueprints/Character/Player/Input/DA_InputConfig.DA_InputConfig"));
		auto* Input = Cast<UEnhancedInputComponent>(PC->InputComponent);
		auto* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
		const UInputAction* Move = Config ? Config->FindNativeInputActionByTag(RPGGameplayTags::InputTag_Move) : nullptr;
		const UInputAction* Attack = Config ? Config->FindAbilityInputActionByTag(AttackTag) : nullptr;
		if (!Input || !Subsystem || !Move || !Attack || Attack->ValueType != EInputActionValueType::Boolean ||
			!Subsystem->HasMappingContext(Weapon->PlayerWeaponData.WeaponInputMappingContext) ||
			!Input->GetActionEventBindings().ContainsByPredicate([Move](const auto& Binding)
			{ return Binding && Binding->GetAction() == Move && Binding->GetTriggerEvent() == ETriggerEvent::Triggered; }) ||
			!Input->GetActionEventBindings().ContainsByPredicate([Attack](const auto& Binding)
			{ return Binding && Binding->GetAction() == Attack && Binding->GetTriggerEvent() == ETriggerEvent::Started; }) ||
			!Input->GetActionEventBindings().ContainsByPredicate([Attack](const auto& Binding)
			{ return Binding && Binding->GetAction() == Attack && Binding->GetTriggerEvent() == ETriggerEvent::Completed; })) return;
		NaturalAttackInput = Subsystem;
		NaturalAttackMoveAction = Move;
		NaturalAttackAction = Attack;
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_NATURAL_ATTACK_READY PersistedWeapon=1 NativeLoadout=1 EnhancedInput=1"));
	}
	const double Now = FPlatformTime::Seconds();
	const FVector Toward = (Boss->GetActorLocation() - Player->GetActorLocation()).GetSafeNormal2D();
	const float Distance = FVector::Dist2D(Player->GetActorLocation(), Boss->GetActorLocation());
	const float Reach = Player->GetCapsuleComponent()->GetScaledCapsuleRadius() +
		Boss->GetCapsuleComponent()->GetScaledCapsuleRadius() + 10.f;
	if (NaturalAttackPhase == 0)
	{
		if (Now < NaturalAttackPhaseAt) return;
		const float Facing = FVector::DotProduct(Player->GetActorForwardVector().GetSafeNormal2D(), Toward);
		if (Distance > Reach || Facing < .95f)
		{
			const FRotationMatrix Basis(FRotator(0.f, PC->GetControlRotation().Yaw, 0.f));
			const FVector2D Move(FVector::DotProduct(Toward, Basis.GetUnitAxis(EAxis::Y)),
				FVector::DotProduct(Toward, Basis.GetUnitAxis(EAxis::X)));
			NaturalAttackInput->StartContinuousInputInjectionForAction(NaturalAttackMoveAction.Get(), FInputActionValue(Move), {}, {});
			return;
		}
		NaturalAttackInput->StopContinuousInputInjectionForAction(NaturalAttackMoveAction.Get());
		NaturalAttackInput->StartContinuousInputInjectionForAction(NaturalAttackAction.Get(), FInputActionValue(true), {}, {});
		NaturalAttackPhase = 1;
		NaturalAttackPhaseAt = Now;
		bNaturalAttackSawActivation = false;
		return;
	}
	bNaturalAttackSawActivation |= Spec->IsActive();
	if (NaturalAttackPhase == 1 && Now - NaturalAttackPhaseAt >= .15)
	{
		NaturalAttackInput->StopContinuousInputInjectionForAction(NaturalAttackAction.Get());
		NaturalAttackPhase = 2;
		NaturalAttackPhaseAt = Now;
	}
	if (NaturalAttackPhase == 2 && ((bNaturalAttackSawActivation && !Spec->IsActive()) || Now - NaturalAttackPhaseAt > 2.0))
	{
		NaturalAttackPhase = 0;
		NaturalAttackPhaseAt = Now + .25;
	}
}

void URPGDungeonFlowE2EClient::HandleLogin(bool bSuccess)
{
	if (Stage == 10 && DeparturePhase == 12 && FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2EDepartureRecovery")))
	{
		auto* Web = GetGameInstance()->GetSubsystem<UHttpWebManager>();
		auto* Flow = GetGameInstance()->GetSubsystem<URPGDungeonSessionSubsystem>();
		if (!bSuccess || !Web->HasAccessToken() || !Same(Web->GetAuthenticatedSteamId(), SteamId)
			|| !Flow->HasUnresolvedDeparture() || !Same(Flow->GetSelectedCharacterId(), CharacterId)
			|| !Same(Flow->GetCurrentDungeonSession().DungeonSessionId, SessionId))
		{ Fail(TEXT("DepartureReauthenticationIdentityMismatch")); return; }
		DeparturePhase = 13;
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_DEPARTURE_RECOVERY REAUTHENTICATED SameAccount=1 OriginalContext=1"));
		return;
	}
	if (bAbandonmentReauthenticate && Stage == 45)
	{
		auto* Web = GetGameInstance()->GetSubsystem<UHttpWebManager>();
		auto* Flow = GetGameInstance()->GetSubsystem<URPGDungeonSessionSubsystem>();
		if (!bSuccess || !Web->HasAccessToken() || !Same(Web->GetAuthenticatedSteamId(), SteamId)
			|| !Flow->HasUnresolvedDungeonAbandonment()
			|| !Same(Flow->GetSelectedCharacterId(), CharacterId)
			|| !Same(Flow->GetCurrentDungeonSession().DungeonSessionId, SessionId))
		{ Fail(TEXT("AbandonmentReauthenticationIdentityMismatch")); return; }
		Stage = 46; // the real lobby widget must reload the owned roster
		return;
	}
	if (bReauthenticateAfterAdmission && Stage == 30)
	{
		auto* Web = GetGameInstance()->GetSubsystem<UHttpWebManager>();
		auto* Flow = GetGameInstance()->GetSubsystem<URPGDungeonSessionSubsystem>();
		if (!bSuccess || !Web->HasAccessToken() || !Same(Web->GetAuthenticatedSteamId(), SteamId)
			|| !Same(Flow->GetSelectedCharacterId(), CharacterId)
			|| !Same(Flow->GetCurrentDungeonSession().DungeonSessionId, SessionId))
		{ Fail(TEXT("ResultReauthenticationIdentityMismatch")); return; }
		bResultReauthenticated = true;
		Stage = 3;
		return;
	}
	if (bReauthenticate && Stage == 11)
	{
		if (!bSuccess) { Fail(TEXT("LobbyReauthenticationFailed")); return; }
		Stage = 12; // the real widget must reload its owned roster before this fixture continues
		return;
	}
	if (Stage != 0 || !bLoginStarted) { Fail(TEXT("UnexpectedLoginCallback")); return; }
	if (!bSuccess) { Fail(TEXT("DevelopmentBackendLogin")); return; }
	auto* Flow = GetGameInstance()->GetSubsystem<URPGDungeonSessionSubsystem>();
	Stage = 1;
	if (bLobbyUI) return; // the actual widget owns roster load, selection and session requests
	if (!Flow->SelectCharacter(CharacterId) || !Flow->ResumeDungeonSession())
		Fail(TEXT("SelectOrResumeRejected"));
}

void URPGDungeonFlowE2EClient::HandleCharacterCreated(const FRPGBackendCharacter& Character, bool bSuccess)
{
	FGuid Parsed;
	if (!bCreateCharacter || !bCharacterCreateStarted || bCharacterCreated || Stage != 1 || !bSuccess
		|| !Same(Character.Name, CharacterName) || !FGuid::Parse(Character.CharacterId, Parsed) || !Parsed.IsValid())
	{ Fail(TEXT("UnexpectedCharacterCreationResponse")); return; }
	CharacterId = Character.CharacterId;
	bCharacterCreated = true;
	UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_LOBBY_CHARACTER CreatedInUI=1 Character=%s"), *CharacterId);
}

void URPGDungeonFlowE2EClient::HandleLobbyTravel(const FString& Destination)
{
	const auto* Flow = GetGameInstance()->GetSubsystem<URPGDungeonSessionSubsystem>();
	if (bGameplayAbandonment && Stage == 60 && !bSawReturning && Same(Destination, TEXT("/Game/Maps/LobbyMap"))
		&& Flow->GetFlowState() == ERPGDungeonClientFlowState::ReturningToLobby && Flow->HasQueuedDungeonAbandonment())
	{
		bSawReturning = true;
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_GAMEPLAY_ABANDON TRAVEL Session=%s Character=%s PlannedReturn=1 ConsentRetained=1"), *SessionId, *CharacterId);
		return;
	}
	if (bResultAbandonment && Stage == 60 && Same(CharacterId, ResultAbandonCharacter)
		&& Same(Destination, TEXT("/Game/Maps/LobbyMap"))
		&& Flow->GetFlowState() == ERPGDungeonClientFlowState::ReturningToLobby
		&& Flow->HasQueuedDungeonAbandonment() && Flow->IsRequestInFlight())
	{
		bSawReturning = true;
		return;
	}
	if (bVerifyAbandonedReturn && Stage == 3 && Same(Destination, TEXT("/Game/Maps/LobbyMap"))
		&& Flow->GetFlowState() == ERPGDungeonClientFlowState::ReturningToLobby)
	{
		bSawReturning = true;
		Stage = 50;
		return;
	}
	if (Stage != 4 || !Same(Destination, TEXT("/Game/Maps/LobbyMap"))
		|| Flow->GetFlowState() != ERPGDungeonClientFlowState::ReturningToLobby)
	{ Fail(TEXT("UnexpectedLobbyTravel")); return; }
	bSawReturning = true;
}

void URPGDungeonFlowE2EClient::HandleInitialAbandonment(const FRPGDungeonAbandonmentReceipt& Receipt,
	bool bSuccess, const FString& ErrorCode)
{
	if (bInitialAbandonmentReceiptObserved) return; // the later lobby replay belongs to the normal flow subsystem
	if (!bVerifyDungeonClientInitialAbandonment || !bInitialAbandonmentSubmitted
		|| Stage != 3 || !bSuccess
		|| !Receipt.IsValidFor(SessionId, CharacterId) || !ErrorCode.IsEmpty())
	{
		Fail(TEXT("InitialDungeonAbandonmentReceiptMismatch"));
		return;
	}
	bInitialAbandonmentReceiptObserved = true;
	UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_INITIAL_ABANDONMENT_RECEIPT SameProcess=1 UEPlayerPost=1 Session=%s Character=%s AbandonedAt=%s RewardExcluded=1"),
		*SessionId, *CharacterId, *Receipt.AbandonedAt);
}

bool URPGDungeonFlowE2EClient::TickDepartureRecoveryProbe()
{
	const bool bRequestRecovery = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2EDepartureRequestRecovery"));
	auto* GI = GetGameInstance();
	auto* Web = GI->GetSubsystem<UHttpWebManager>();
	auto* Flow = GI->GetSubsystem<URPGDungeonSessionSubsystem>();
	auto* Widget = GI->GetSubsystem<URPGOnlineLobbySubsystem>()->GetLobbyWidget();
	if (!Widget || Flow->IsRequestInFlight()) return true;
	auto* Retry = Cast<UButton>(Widget->GetWidgetFromName(TEXT("RetryDungeonDepartureButton")));
	auto* Request = Cast<UButton>(Widget->GetWidgetFromName(TEXT("RequestDungeonDepartureButton")));
	auto* Cancel = Cast<UButton>(Widget->GetWidgetFromName(TEXT("CancelDungeonDepartureButton")));
	if (!Retry || !Request || !Cancel || !Same(Flow->GetSelectedCharacterId(), CharacterId)
		|| !Same(Flow->GetCurrentDungeonSession().DungeonSessionId, SessionId)) return Fail(TEXT("DepartureRecoveryContext"));
	if (DeparturePhase == (bRequestRecovery ? 1 : 2))
	{
		if (!Flow->HasUnresolvedDeparture() || Request->GetIsEnabled() || Cancel->GetIsEnabled()
			|| !Retry->GetIsEnabled()) return Fail(TEXT("DepartureUncertaintyUI"));
		DeparturePhase = 10;
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_DEPARTURE_RECOVERY UNCONFIRMED SameContext=1 NewActionsLocked=1 RetryEnabled=1"));
		if (FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2EDeparturePrepareRestart")))
		{
			FRPGDungeonDepartureJournal Journal(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Online/DepartureCommands")),
				Web->GetApiUrl(), SteamId, CharacterId);
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_DEPARTURE_RECOVERY JOURNAL_FILE Path=\"%s\""), *Journal.GetPath());
		}
	}
	if (DeparturePhase == 10)
	{
		FString Gate;
		FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2ERecoveryRetryFile="), Gate);
		if (!IFileManager::Get().FileExists(*Gate)) return true;
		if (!Retry->GetIsEnabled()) return true;
		DeparturePhase = 11;
		Retry->OnClicked.Broadcast();
		if (!Flow->IsRequestInFlight()) return Fail(TEXT("DepartureRetryNotDispatched"));
		return true;
	}
	if (DeparturePhase == 11)
	{
		if (Web->HasAccessToken()) return true; // await the backend's real 401
		if (!Flow->HasUnresolvedDeparture() || Retry->GetIsEnabled() || !Same(Web->GetAuthenticatedSteamId(), SteamId))
			return Fail(TEXT("Departure401ContextLost"));
		auto* Login = Cast<UButton>(Widget->GetWidgetFromName(TEXT("LoginDevelopmentButton")));
		auto* Input = Cast<UEditableTextBox>(Widget->GetWidgetFromName(TEXT("DevelopmentSteamId")));
		if (!Login || !Input || !Login->GetIsEnabled()) return true;
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_DEPARTURE_RECOVERY AUTH_REQUIRED OriginalContext=1 RetryLocked=1 LiveLoginButton=1"));
		Input->SetText(FText::FromString(SteamId));
		DeparturePhase = 12;
		Login->OnClicked.Broadcast();
		return true;
	}
	if (DeparturePhase == 13)
	{
		if (!Retry->GetIsEnabled()) return true; // real widget must reload owned roster
		DeparturePhase = 14;
		Retry->OnClicked.Broadcast();
		if (!Flow->IsRequestInFlight()) return Fail(TEXT("DepartureAuthenticatedReplayNotDispatched"));
		return true;
	}
	if (DeparturePhase == 14)
	{
		if (Flow->HasUnresolvedDeparture()) return true;
		const auto Session = Flow->GetCurrentDungeonSession();
		if (!Web->HasAccessToken() || !Same(Web->GetAuthenticatedSteamId(), SteamId)
			|| Session.DepartureRequestedAt.IsEmpty() == bRequestRecovery || Session.State != TEXT("Waiting"))
			return Fail(TEXT("DepartureReplayDidNotRestoreExpectedLobby"));
		if (bRequestRecovery)
		{
			if (Request->GetIsEnabled() || !Cancel->GetIsEnabled()) return Fail(TEXT("DepartureQueueControlsNotRestored"));
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_DEPARTURE_RECOVERY REQUEST_QUEUE_RESTORED RequestLocked=1 CancelEnabled=1"));
		}
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_DEPARTURE_RECOVERY RECOVERED SameProcess=1 SameContext=1 OriginalCommand=1 NoNewDeparture=1"));
		Stage = 55;
		return false;
	}
	return true;
}

bool URPGDungeonFlowE2EClient::TickProbe(float)
{
	if (Stage < 0) return false;
	if (FPlatformTime::Seconds() > Deadline) return Fail(TEXT("ClientTimeout"));
	UGameInstance* GI = GetGameInstance();
	UWorld* World = GI->GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!World || !PC || !PC->IsLocalController()) return true;
	auto* Flow = GI->GetSubsystem<URPGDungeonSessionSubsystem>();
	auto* Web = GI->GetSubsystem<UHttpWebManager>();
	if (!Flow || !Web) return Fail(TEXT("MissingClientSubsystem"));
	if (Web->HasAccessToken())
	{
		auto* Lobby = GI->GetSubsystem<URPGOnlineLobbySubsystem>();
		auto* Widget = Lobby ? Lobby->GetLobbyWidget() : nullptr;
		if (Widget && Lobby->IsLobbyController(PC))
		{
			// Follow the new frontend through its real buttons before legacy
			// configuration/session probes operate on the detail page.
			auto* Server = Cast<UButton>(Widget->GetWidgetFromName(TEXT("SelectServerButton")));
			if (Server && Server->IsVisible() && Server->GetIsEnabled())
			{ Server->OnClicked.Broadcast(); return true; }
			if (!bLobbyCreationOnly && !bVerifyLobbyLogout)
			{
				auto* Section = Widget->GetWidgetFromName(bLobbyConfigurationOnly ? TEXT("OnlineLobbyConfigurationSection") : TEXT("OnlineLobbyDungeonSection"));
				auto* Open = Cast<UButton>(Widget->GetWidgetFromName(bLobbyConfigurationOnly ? TEXT("OpenSkillMenuButton") : TEXT("OpenDungeonMenuButton")));
				if (Section && !Section->IsVisible() && Open && Open->IsVisible() && Open->GetIsEnabled())
				{ Open->OnClicked.Broadcast(); return true; }
			}
		}
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2EDepartureRestartRecovery"))) return TickDepartureRestartProbe();
	if (bGameplayAbandonment && Stage == 60) return TickGameplayAbandonmentProbe(World, PC);
	if (bResultAbandonment && Stage == 60) return TickResultAbandonmentProbe(World, PC);
	if (((bResultAbandonmentResponseLoss && Same(CharacterId, ResultAbandonCharacter)) || bGameplayAbandonmentResponseLoss)
		&& Stage >= 41 && Stage <= 48)
		return TickAbandonmentProbe(World, PC); // same-process arrival reuses the existing lobby recovery UI
	if (bVerifyLobbyLogout) return TickLobbyLogoutProbe(World, PC);
	if (bLobbyConfigurationOnly) return TickLobbyConfigurationProbe(World, PC);
	const bool bExpectedAuthenticationFailure = bReauthenticate && !bReauthenticated
		&& (Stage == 10 || Stage == 11 || Stage == 12) && IFileManager::Get().FileExists(*AuthExpiredFile);
	const bool bExpectedDungeonAuthenticationFailure = bReauthenticateAfterAdmission && !bResultReauthenticated
		&& (Stage == 3 || Stage == 30) && IFileManager::Get().FileExists(*DungeonAuthExpiredFile);
	if (Flow->GetFlowState() == ERPGDungeonClientFlowState::Error && !bExpectedAuthenticationFailure && !bExpectedDungeonAuthenticationFailure
		&& !(bAbandonmentUI && Flow->HasUnresolvedDungeonAbandonment())
		&& !(FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2EDepartureRecovery")) && Flow->HasUnresolvedDeparture()))
		return Fail(TEXT("ClientFlowError"));
	if (Stage == 0 && !bLoginStarted)
	{
		FGuid Parsed;
		FString NormalizedName;
		const bool bValidCharacter = bCreateCharacter
			? bLobbyUI && CharacterId.IsEmpty() && URPGOnlineLobbyWidget::TryNormalizeCharacterName(CharacterName, NormalizedName)
				&& Same(NormalizedName, CharacterName)
			: FGuid::Parse(CharacterId, Parsed);
		if (!bValidCharacter || ((!bLobbyUI || bAbandonmentUI) && !FGuid::Parse(SessionId, Parsed)) || SteamId.IsEmpty())
			return Fail(TEXT("InvalidClientArguments"));
		if (bResultAbandonment && (bLobbyUI || bAbandonmentUI || bNaturalPlayerAttack || bVerifyAbandonedReturn
			|| bReauthenticate || bReauthenticateAfterAdmission || ResultRetryGateFile.IsEmpty()
			|| !FGuid::ParseExact(ResultAbandonCharacter, EGuidFormats::DigitsWithHyphens, Parsed) || !Parsed.IsValid()
			|| !Flow->IsResultAbandonmentTestingEnabled()))
			return Fail(TEXT("InvalidResultAbandonmentArguments"));
		if (bGameplayAbandonment && (bLobbyUI || bAbandonmentUI || bNaturalPlayerAttack || bVerifyAbandonedReturn
			|| bResultAbandonment || bReauthenticate || bReauthenticateAfterAdmission || bVerifyDungeonClientInitialAbandonment
			|| GameplayConsentGateFile.IsEmpty() || GameplayConfirmGateFile.IsEmpty() || !Flow->IsGameplayAbandonmentTestingEnabled()))
			return Fail(TEXT("InvalidGameplayAbandonmentArguments"));
		if (bResultAbandonmentResponseLoss && (!bResultAbandonment || !Same(CharacterId, ResultAbandonCharacter)
			|| AbandonmentRetryFile.IsEmpty())) return Fail(TEXT("InvalidResultAbandonmentLossArguments"));
		if (bGameplayAbandonmentResponseLoss && (!bGameplayAbandonment || AbandonmentRetryFile.IsEmpty()))
			return Fail(TEXT("InvalidGameplayAbandonmentLossArguments"));
		if (bLobbyDifficultyOnly && (!bLobbyUI || bAbandonmentUI || bReauthenticate || bReauthenticateAfterAdmission || bNaturalPlayerAttack))
			return Fail(TEXT("InvalidLobbyDifficultyArguments"));
		if (bLobbyCreationOnly && (!bLobbyUI || !bCreateCharacter || bVerifyLobbyLogout || bAbandonmentUI
			|| bLobbyDifficultyOnly || bReauthenticate || bReauthenticateAfterAdmission || bNaturalPlayerAttack))
			return Fail(TEXT("InvalidLobbyCreationOnlyArguments"));
		if (bAbandonmentUI && (!bLobbyUI || AbandonmentStartFile.IsEmpty() || AbandonmentRetryFile.IsEmpty()))
			return Fail(TEXT("InvalidAbandonmentArguments"));
		if (bAbandonmentReauthenticate && ((!bAbandonmentUI && !bResultAbandonmentResponseLoss && !bGameplayAbandonmentResponseLoss)
			|| AbandonmentRenewedRetryFile.IsEmpty()))
			return Fail(TEXT("InvalidAbandonmentReauthenticationArguments"));
		if (bVerifyAbandonedReturn && (bLobbyUI || bAbandonmentUI)) return Fail(TEXT("InvalidAbandonedReturnArguments"));
		if (bVerifyAbandonedReceipt && (!bVerifyAbandonedReturn || !Flow->IsAbandonmentTestingEnabled()))
			return Fail(TEXT("InvalidAbandonedReceiptArguments"));
		if (bVerifyDungeonClientInitialAbandonment && (!bVerifyAbandonedReceipt || InitialAbandonmentGateFile.IsEmpty()
			|| !FParse::Param(FCommandLine::Get(), TEXT("RPGDungeonMembershipBackendE2E"))))
			return Fail(TEXT("InvalidInitialDungeonAbandonmentArguments"));
		if (bReauthenticate && (!bLobbyUI || AuthExpiredFile.IsEmpty())) return Fail(TEXT("InvalidReauthenticationArguments"));
		if (bReauthenticateAfterAdmission && (!bLobbyUI || DungeonAuthExpiredFile.IsEmpty())) return Fail(TEXT("InvalidDungeonReauthenticationArguments"));
		if (bLobbyUI)
		{
			auto* Lobby = GI->GetSubsystem<URPGOnlineLobbySubsystem>();
			auto* Widget = Lobby ? Lobby->GetLobbyWidget() : nullptr;
			auto* Input = Widget ? Cast<UEditableTextBox>(Widget->GetWidgetFromName(TEXT("DevelopmentSteamId"))) : nullptr;
			auto* Button = Widget ? Cast<UButton>(Widget->GetWidgetFromName(TEXT("LoginDevelopmentButton"))) : nullptr;
			if (!Input || !Button || !Button->GetIsEnabled()) return true;
			if (!bAbandonmentUI && !bLobbyCreationOnly && (SessionFile.IsEmpty() || ServerReadyFile.IsEmpty())) return Fail(TEXT("MissingLobbyFixtureFiles"));
			if (bLobbyCreationOnly) UE_LOG(LogTemp, Display, TEXT("LOBBY_FRONTEND_E2E START LiveMap=1"));
			bLoginStarted = true;
			Input->SetText(FText::FromString(SteamId));
			Button->OnClicked.Broadcast();
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_LOBBY_LOGIN ActualMap=1 LiveButton=1"));
			return true;
		}
		bLoginStarted = true;
		GI->GetSubsystem<URPGSteamAuthSubsystem>()->LoginToDevelopmentBackend(SteamId);
		return true;
	}
	if (bAbandonmentUI) return TickAbandonmentProbe(World, PC);
	if (Stage == 1 && bLobbyUI)
	{
		auto* Lobby = GI->GetSubsystem<URPGOnlineLobbySubsystem>();
		auto* Widget = Lobby ? Lobby->GetLobbyWidget() : nullptr;
		auto* Selector = Widget ? Cast<UComboBoxString>(Widget->GetWidgetFromName(TEXT("OwnedCharacters"))) : nullptr;
		if (!Widget || !Selector || Flow->IsRequestInFlight()) return true;
		if (bCreateCharacter && !bCharacterCreated)
		{
			if (bCharacterCreateStarted) return true;
			auto* Input = Cast<UEditableTextBox>(Widget->GetWidgetFromName(TEXT("NewCharacterName")));
			auto* Button = Cast<UButton>(Widget->GetWidgetFromName(TEXT("CreateCharacterButton")));
			if (!Input || !Button || !Button->GetIsEnabled()) return true; // roster HTTP must complete first
			if (Selector->GetOptionCount() != 0) return Fail(TEXT("CharacterCreationRequiresEmptyRoster"));
			for (const FString Invalid : {FString(TEXT("x")), FString::ChrN(21, TEXT('x')), FString(TEXT("a\tb"))})
			{
				Input->SetText(FText::FromString(Invalid));
				Button->OnClicked.Broadcast();
				if (!Button->GetIsEnabled() || !Widget->GetDisplayedStatus().ToString().Contains(TEXT("invalid_character_name")))
					return Fail(TEXT("InvalidCharacterNameNotRejectedLocally"));
			}
			bCharacterCreateStarted = true;
			Input->SetText(FText::FromString(TEXT("  ") + CharacterName + TEXT("  ")));
			Button->OnClicked.Broadcast();
			if (Button->GetIsEnabled() || Input->GetIsEnabled()) return Fail(TEXT("CharacterCreationDidNotLockInput"));
			Button->OnClicked.Broadcast(); // forced second dispatcher must not issue another POST
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_LOBBY_CHARACTER_GUARDS InvalidNames=3 Trimmed=1 DuplicateClickLocked=1"));
			return true;
		}
		if (!bCharacterSelected)
		{
			const FString Option = Widget->GetCharacterOption(CharacterId);
			if (Option.IsEmpty() || !Selector->GetIsEnabled()) return true;
			if (bCreateCharacter)
			{
				auto* Input = Cast<UEditableTextBox>(Widget->GetWidgetFromName(TEXT("NewCharacterName")));
				if (Selector->GetOptionCount() != 1 || !Input || !Input->GetText().IsEmpty())
					return Fail(TEXT("CreatedCharacterRosterOrInputMismatch"));
				UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_LOBBY_CHARACTER_ROSTER OwnedCharacter=1 Refreshed=1 InputCleared=1"));
			}
			bCharacterSelected = true;
			Selector->SetSelectedOption(Option); // live OnSelectionChanged -> selected character + real Resume HTTP
			return true;
		}
		if (!Same(Flow->GetSelectedCharacterId(), CharacterId)) return Fail(TEXT("LobbySelectionMismatch"));
		if (auto* Continue = Cast<UButton>(Widget->GetWidgetFromName(TEXT("ContinueWithCharacterButton"))); Continue && Continue->GetIsEnabled())
		{
			Continue->OnClicked.Broadcast(); // explicit character page -> preparation, no session creation
			return true;
		}
		if (!bSessionActionStarted)
		{
			if (bLobbyCreationOnly)
			{
				auto* LoginPage = Widget->GetWidgetFromName(TEXT("OnlineLobbyLoginContent"));
				auto* CharactersPage = Widget->GetWidgetFromName(TEXT("OnlineLobbyCharacterContent"));
				auto* PreparationPage = Widget->GetWidgetFromName(TEXT("OnlineLobbyAuthenticatedContent"));
				auto* Classes = Cast<UComboBoxString>(Widget->GetWidgetFromName(TEXT("CharacterCreationClass")));
				auto* Details = Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("OnlineLobbyCharacterDetails")));
				auto* Back = Cast<UButton>(Widget->GetWidgetFromName(TEXT("ChangeCharacterButton")));
				auto* Continue = Cast<UButton>(Widget->GetWidgetFromName(TEXT("ContinueWithCharacterButton")));
				if (!bCharacterCreated || !LoginPage || !CharactersPage || !PreparationPage || !Classes || !Details || !Back || !Continue
					|| LoginPage->GetVisibility() != ESlateVisibility::Collapsed || CharactersPage->GetVisibility() != ESlateVisibility::Collapsed
					|| PreparationPage->GetVisibility() != ESlateVisibility::Visible || !Widget->IsInViewport()
					|| Classes->GetSelectedIndex() == INDEX_NONE || !Details->GetText().ToString().Contains(Classes->GetSelectedOption())
					|| !Flow->GetCurrentDungeonSession().DungeonSessionId.IsEmpty() || !Back->GetIsEnabled())
					return Fail(TEXT("LobbyCreationPreparationMismatch"));
				Back->OnClicked.Broadcast();
				if (CharactersPage->GetVisibility() != ESlateVisibility::Visible || !Continue->GetIsEnabled()
					|| !Same(Flow->GetSelectedCharacterId(), CharacterId)) return Fail(TEXT("LobbyCreationBackLostSelection"));
				Continue->OnClicked.Broadcast();
				if (PreparationPage->GetVisibility() != ESlateVisibility::Visible || !Same(Flow->GetSelectedCharacterId(), CharacterId)
					|| !Flow->GetCurrentDungeonSession().DungeonSessionId.IsEmpty()) return Fail(TEXT("LobbyCreationContinueMutatedSession"));
				UE_LOG(LogTemp, Display, TEXT("LOBBY_FRONTEND_E2E PASS LiveMap=1 CatalogSelected=1 CreatedInUI=1 OwnedSelection=1 Preparation=1 BackPreserved=1 NoDungeon=1"));
				Stage = 7;
				FPlatformMisc::RequestExit(false);
				return false;
			}
			UButton* Button = nullptr;
			if (bLobbyLeader)
			{
				Button = Cast<UButton>(Widget->GetWidgetFromName(TEXT("CreateDungeonSessionButton")));
				FString RegisteredDungeon;
				if (FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2ERegisteredDungeon="), RegisteredDungeon))
				{
					if (!Widget->CanSelectDungeon()) return true;
					FString SelectedId;
					auto* Dungeons = Cast<UComboBoxString>(Widget->GetWidgetFromName(TEXT("DungeonSelectionOptions")));
					if (!Dungeons || !Dungeons->GetIsEnabled() || Dungeons->GetOptionCount() != 1
						|| !Dungeons->OnSelectionChanged.IsBound() || Widget->GetDungeonOptions().Num() != 1
						|| !Widget->GetSelectedDungeonId(SelectedId) || !Same(SelectedId, RegisteredDungeon)
						|| Same(Widget->GetDungeonDisplayName(RegisteredDungeon).ToString(), RegisteredDungeon)
						|| !Widget->SelectDungeon(RegisteredDungeon) || Flow->IsRequestInFlight()
						|| !Flow->GetCurrentDungeonSession().DungeonSessionId.IsEmpty())
						return Fail(TEXT("RegisteredDungeonDefinitionNotLoadedOrSelectionNotLocal"));
					if (Widget->SelectDungeon(RegisteredDungeon + TEXT(" ")) || Widget->SelectDungeon(RegisteredDungeon.ToLower())
						|| !Widget->GetSelectedDungeonId(SelectedId) || !Same(SelectedId, RegisteredDungeon))
						return Fail(TEXT("RegisteredDungeonInvalidSelectionAccepted"));
					UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_DUNGEON_DEFINITION_SELECTED Dungeon=%s ConfiguredDefinition=1 AuthoredLabel=1 LiveSelector=1 LocalOnly=1 InvalidRejected=1"), *RegisteredDungeon);
				}
				{
					auto* DifficultyBox = Cast<UComboBoxString>(Widget->GetWidgetFromName(TEXT("DungeonDifficulty")));
					if (!DifficultyBox || !DifficultyBox->GetIsEnabled() || !Widget->CanSelectDungeonDifficulty()) return true;
					if (DifficultyBox->GetOptionCount() != 4 || DifficultyBox->FindOptionIndex(LobbyDifficulty) == INDEX_NONE)
						return Fail(TEXT("LobbyDifficultyOptionsInvalid"));
					for (const auto Value : Widget->GetDungeonDifficultyOptions())
					{
						const FString Name = StaticEnum<ERPGGameDifficulty>()->GetNameStringByValue(static_cast<int64>(Value));
						DifficultyBox->SetSelectedOption(Name);
						ERPGGameDifficulty Selected = ERPGGameDifficulty::Easy;
						if (!Widget->GetSelectedDungeonDifficulty(Selected) || Selected != Value || Flow->IsRequestInFlight()
							|| !Flow->GetCurrentDungeonSession().DungeonSessionId.IsEmpty()) return Fail(TEXT("LobbyDifficultySelectionNotLocal"));
					}
					if (Widget->SelectDungeonDifficulty(static_cast<ERPGGameDifficulty>(255))) return Fail(TEXT("InvalidLobbyDifficultyAccepted"));
					const FString BeforeInvalid = DifficultyBox->GetSelectedOption();
					DifficultyBox->OnSelectionChanged.Broadcast(TEXT("hard "), ESelectInfo::Direct);
					if (!Same(DifficultyBox->GetSelectedOption(), BeforeInvalid)) return Fail(TEXT("MalformedLobbyDifficultyAccepted"));
					DifficultyBox->SetSelectedOption(LobbyDifficulty);
					UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_LOBBY_DIFFICULTY_SELECTED Difficulty=%s Options=4 LocalOnly=1 InvalidRejected=1"), *LobbyDifficulty);
				}
			}
			else
			{
				FString SharedSession;
				FGuid Parsed;
				if (!FFileHelper::LoadFileToString(SharedSession, *SessionFile)) return true;
				SharedSession.TrimStartAndEndInline();
				if (!FGuid::Parse(SharedSession, Parsed)) return Fail(TEXT("InvalidSharedSession"));
				auto* Input = Cast<UEditableTextBox>(Widget->GetWidgetFromName(TEXT("DevelopmentSessionId")));
				if (!Input) return Fail(TEXT("MissingDevelopmentJoinInput"));
				Input->SetText(FText::FromString(SharedSession));
				Button = Cast<UButton>(Widget->GetWidgetFromName(TEXT("JoinDevelopmentSessionButton")));
			}
			if (!Button || !Button->GetIsEnabled()) return true;
			bSessionActionStarted = true;
			Button->OnClicked.Broadcast();
			{
				if (!Flow->IsRequestInFlight() || Widget->CanSelectDungeonDifficulty()
					|| Widget->SelectDungeonDifficulty(ERPGGameDifficulty::Easy) || Widget->CreateSelectedDungeonSession())
					return Fail(TEXT("LobbyDifficultyRequestNotLocked"));
				Button->OnClicked.Broadcast(); // native handler must reject this duplicate as well
			}
			return true;
		}
		const auto Session = Flow->GetCurrentDungeonSession();
		if (!Same(Session.State, TEXT("Waiting")) || Session.DungeonSessionId.IsEmpty()) return Fail(TEXT("LobbyWaitingSessionMissing"));
		SessionId = Session.DungeonSessionId;
		{
			auto* DifficultyBox = Cast<UComboBoxString>(Widget->GetWidgetFromName(TEXT("DungeonDifficulty")));
			ERPGGameDifficulty Selected = ERPGGameDifficulty::Easy;
			if (!DifficultyBox || DifficultyBox->GetIsEnabled() || Widget->CanSelectDungeonDifficulty()
				|| !Same(Session.Difficulty, LobbyDifficulty) || !Widget->GetSelectedDungeonDifficulty(Selected)
				|| !Same(DifficultyBox->GetSelectedOption(), LobbyDifficulty)) return Fail(TEXT("LobbyBackendDifficultyNotDisplayed"));
			DifficultyBox->SetSelectedOption(LobbyDifficulty == TEXT("Hell") ? TEXT("Easy") : TEXT("Hell"));
			if (!Same(DifficultyBox->GetSelectedOption(), LobbyDifficulty) || Widget->SelectDungeonDifficulty(ERPGGameDifficulty::Easy)
				|| Widget->CreateSelectedDungeonSession() || !Same(Flow->GetCurrentDungeonSession().Difficulty, LobbyDifficulty))
				return Fail(TEXT("LobbySessionDifficultyChanged"));
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_LOBBY_DIFFICULTY_SESSION Difficulty=%s BackendSnapshot=1 SelectorLocked=1 NativeGuards=1"), *Session.Difficulty);
		}
		FString RegisteredDungeon;
		if (FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2ERegisteredDungeon="), RegisteredDungeon))
		{
			FString SelectedId;
			if (!Same(Session.DungeonId, RegisteredDungeon) || !Widget->GetSelectedDungeonId(SelectedId)
				|| !Same(SelectedId, RegisteredDungeon) || Widget->CanSelectDungeon() || Widget->SelectDungeon(RegisteredDungeon))
				return Fail(TEXT("RegisteredDungeonSessionIdentityOrLockMismatch"));
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_DUNGEON_DEFINITION_SESSION Dungeon=%s BackendSnapshot=1 SelectorLocked=1 SameIdentity=1"), *RegisteredDungeon);
		}
		Stage = 10;
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_LOBBY_SESSION %s=1 Session=%s"),
			bLobbyLeader ? TEXT("Created") : TEXT("Joined"), *SessionId);
		if (bLobbyDifficultyOnly) { Stage = 55; return false; }
	}
	if (Stage == 10 && bReauthenticate && !bReauthenticated)
	{
		if (!IFileManager::Get().FileExists(*AuthExpiredFile) || Flow->IsRequestInFlight()) return true;
		auto* Widget = GI->GetSubsystem<URPGOnlineLobbySubsystem>()->GetLobbyWidget();
		if (!Widget) return true;
		if (Web->HasAccessToken())
		{
			auto* Refresh = Cast<UButton>(Widget->GetWidgetFromName(TEXT("RefreshDungeonSessionButton")));
			if (!Refresh || !Refresh->GetIsEnabled()) return true;
			Refresh->OnClicked.Broadcast(); // backend DB expiry -> genuine HTTP 401, not client token injection
			return true;
		}
		if (!Same(Flow->GetSelectedCharacterId(), CharacterId)
			|| !Same(Flow->GetCurrentDungeonSession().DungeonSessionId, SessionId)
			|| !Same(Web->GetAuthenticatedSteamId(), SteamId)) return Fail(TEXT("ExpiredAuthenticationLostSelection"));
		auto* Login = Cast<UButton>(Widget->GetWidgetFromName(TEXT("LoginDevelopmentButton")));
		auto* Input = Cast<UEditableTextBox>(Widget->GetWidgetFromName(TEXT("DevelopmentSteamId")));
		if (!Login || !Login->GetIsEnabled()) return true;
		if (!Input || Input->GetIsEnabled() || !Same(Input->GetText().ToString(), SteamId))
			return Fail(TEXT("ReauthenticationAccountNotPinned"));
		Stage = 11;
		Login->OnClicked.Broadcast();
		if (Login->GetIsEnabled()) return Fail(TEXT("ReauthenticationDuplicateNotLocked"));
		Login->OnClicked.Broadcast();
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_AUTH_EXPIRED TokenInvalidated=1 AccountPinned=1 SelectionRetained=1 DuplicateLoginLocked=1"));
		return true;
	}
	if (Stage == 11) return true;
	if (Stage == 12)
	{
		auto* Widget = GI->GetSubsystem<URPGOnlineLobbySubsystem>()->GetLobbyWidget();
		auto* Selector = Widget ? Cast<UComboBoxString>(Widget->GetWidgetFromName(TEXT("OwnedCharacters"))) : nullptr;
		auto* Refresh = Widget ? Cast<UButton>(Widget->GetWidgetFromName(TEXT("RefreshDungeonSessionButton"))) : nullptr;
		if (!Widget || !Selector || !Refresh || !Refresh->GetIsEnabled()) return true;
		if (!Web->HasAccessToken() || !Same(Web->GetAuthenticatedSteamId(), SteamId)
			|| !Same(Flow->GetSelectedCharacterId(), CharacterId)
			|| !Same(Flow->GetCurrentDungeonSession().DungeonSessionId, SessionId)
			|| Widget->GetCharacterOption(CharacterId).IsEmpty()
			|| !Same(Selector->GetSelectedOption(), Widget->GetCharacterOption(CharacterId)))
			return Fail(TEXT("ReauthenticationRosterOrSessionMismatch"));
		Refresh->OnClicked.Broadcast(); // recover the previous flow error using a fresh authenticated read
		if (!Flow->IsRequestInFlight()) return Fail(TEXT("ReauthenticationRefreshNotStarted"));
		bReauthenticated = true;
		Stage = 10;
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_REAUTH_RESTORED SameAccount=1 LiveLoginButton=1 OwnedRoster=1 CharacterSessionRetained=1"));
	}
	if (Stage == 10)
	{
		const int32 RecoveryStartPhase = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2EDepartureRequestRecovery")) ? 1 : 2;
		if (bLobbyLeader && DeparturePhase >= RecoveryStartPhase && FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2EDepartureRecovery")))
			return TickDepartureRecoveryProbe();
		if (!bLobbyLeader && DeparturePhase == 0 && FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2EDepartureGate")))
		{
			auto* Widget = GI->GetSubsystem<URPGOnlineLobbySubsystem>()->GetLobbyWidget();
			auto* Request = Widget ? Cast<UButton>(Widget->GetWidgetFromName(TEXT("RequestDungeonDepartureButton"))) : nullptr;
			auto* Cancel = Widget ? Cast<UButton>(Widget->GetWidgetFromName(TEXT("CancelDungeonDepartureButton"))) : nullptr;
			if (!Request || !Cancel || Flow->IsRequestInFlight()) return true;
			if (Request->GetIsEnabled() || Cancel->GetIsEnabled() || Flow->CanSetDeparture(false) || Flow->CanSetDeparture(true))
				return Fail(TEXT("NonleaderDepartureActionEnabled"));
			DeparturePhase = 1;
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_DEPARTURE_UI NONLEADER_LOCKED RequestCancel=0"));
		}
		if (bLobbyLeader && FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2EDepartureGate")))
		{
			FString Gate;
			FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2EDepartureGateFile="), Gate);
			if (!IFileManager::Get().FileExists(*Gate) || Flow->IsRequestInFlight()) return true;
			auto* Widget = GI->GetSubsystem<URPGOnlineLobbySubsystem>()->GetLobbyWidget();
			const auto Current = Flow->GetCurrentDungeonSession();
			int32 ExpectedMembers = 2;
			FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2EPlayers="), ExpectedMembers);
			if (Current.Members.Num() != ExpectedMembers) return true; // await authoritative roster revision
			const bool bCancel = DeparturePhase == 1;
			auto* Button = Widget ? Cast<UButton>(Widget->GetWidgetFromName(bCancel
				? TEXT("CancelDungeonDepartureButton") : TEXT("RequestDungeonDepartureButton"))) : nullptr;
			if (DeparturePhase == 0 || (DeparturePhase == 1 && !Current.DepartureRequestedAt.IsEmpty())
				|| (DeparturePhase == 2 && Current.DepartureRequestedAt.IsEmpty()))
			{
				if (!Button || !Button->GetIsEnabled()) return true;
				if (DeparturePhase == 1) FirstDepartureRequestId = Current.DepartureRequestId;
				Button->OnClicked.Broadcast();
				if (!Flow->IsRequestInFlight()) return Fail(TEXT("DepartureButtonDidNotStartRequest"));
				++DeparturePhase;
				return true;
			}
			if (DeparturePhase == 3 && !Current.DepartureRequestedAt.IsEmpty())
			{
				if (Current.DepartureRequestId == FirstDepartureRequestId) return Fail(TEXT("CancelledDepartureIdentityReused"));
				DeparturePhase = 4;
				UE_LOG(LogTemp, Display, TEXT("DUNGEON_DEPARTURE_UI READY_FOR_CLAIM Leader=1 RequestCancelRequeue=1 Revision=%lld"), Current.LobbyRevision);
			}
		}
		if (!IFileManager::Get().FileExists(*ServerReadyFile) || Flow->IsRequestInFlight()) return true;
		auto* Widget = GI->GetSubsystem<URPGOnlineLobbySubsystem>()->GetLobbyWidget();
		auto* Button = Widget ? Cast<UButton>(Widget->GetWidgetFromName(TEXT("EnterDungeonButton"))) : nullptr;
		if (!Button || !Button->GetIsEnabled()) return true;
		if (!Same(Flow->GetCurrentDungeonSession().DungeonSessionId, SessionId)) return Fail(TEXT("LobbySessionChangedBeforeEntry"));
		Stage = 2;
		Button->OnClicked.Broadcast();
		if (!Flow->IsRequestInFlight()) return Fail(TEXT("LobbyEntryDidNotRequestTicket"));
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_LOBBY_ENTRY LiveButton=1 FreshTicket=1 TrustedBackendAddress=1"));
	}
	if (Stage == 1 && !bLobbyUI && !Flow->IsRequestInFlight())
	{
		const auto Session = Flow->GetCurrentDungeonSession();
		if (!Web->HasAccessToken() || !Same(Session.DungeonSessionId, SessionId)
			|| !Same(Flow->GetSelectedCharacterId(), CharacterId) || !Same(Session.State, TEXT("InProgress")))
			return Fail(TEXT("ResumeIdentityOrStateMismatch"));
		Stage = 2;
		if (!Flow->ConnectToDungeon(PC, Session.ServerAddress)) return Fail(TEXT("ConnectRejected"));
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_CONNECT RealAuth=1 Resume=1 FreshTicket=1"));
#if !UE_BUILD_SHIPPING
		if (FParse::Param(FCommandLine::Get(), TEXT("RPGPendingAdmissionAbandonmentE2E")))
		{
			FString PendingTarget;
			FParse::Value(FCommandLine::Get(), TEXT("RPGAbandonCharacter="), PendingTarget);
			if (Same(CharacterId, PendingTarget))
			{
				// This client must never be admitted. The server observes the real
				// consumed ticket/socket; do not run the positive reward/return probe
				// through its expected network rejection or manufacture client identity.
				Flow->OnLobbyTravelStarted.RemoveDynamic(this, &ThisClass::HandleLobbyTravel);
				UE_LOG(LogTemp, Display, TEXT("DUNGEON_PENDING_ADMISSION TARGET_CONNECT RealAuth=1 FreshTicket=1 ServerOwnsNegativeObservation=1"));
				return false;
			}
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("RPGReconnectAdmissionLossE2E"))
			&& FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2EReconnectReplacement")))
		{
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_RECONNECT_SETTLEMENT ATTEMPT Character=%s Session=%s FreshTicket=1"), *CharacterId, *SessionId);
		}
#endif
	}
	if (Stage == 2 || Stage == 3 || Stage == 30)
	{
		if (World->GetNetMode() != NM_Client) return true;
		auto* State = PC->GetPlayerState<ARPGPlayerState>();
		auto* Projection = PC->FindComponentByClass<URPGInventoryProjectionComponent>();
		auto* Player = Cast<ARPGPlayer>(PC->GetPawn());
		auto* ASC = Player ? Player->GetAbilitySystemComponent() : nullptr;
		auto* Battle = World->GetGameState<ARPGBossBattleGameState>();
		bool bAdmissionOnly = false;
		bool bAbandonmentSettlement = false;
#if !UE_BUILD_SHIPPING
		bAbandonmentSettlement = FParse::Param(FCommandLine::Get(), TEXT("RPGDungeonAbandonmentSettlementE2E"));
		FString AbandonCharacter;
		FParse::Value(FCommandLine::Get(), TEXT("RPGAbandonCharacter="), AbandonCharacter);
		bAdmissionOnly = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2EAdmissionOnly"))
			|| FParse::Param(FCommandLine::Get(), TEXT("RPGNPCDropPickupE2E"))
			|| (FParse::Param(FCommandLine::Get(), TEXT("RPGDungeonMembershipBackendE2E"))
				&& (!bAbandonmentSettlement || (Same(CharacterId, AbandonCharacter)
					&& !(FParse::Param(FCommandLine::Get(), TEXT("RPGDungeonReconnectE2E"))
						&& FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2EReconnectReplacement"))))));
#endif
		if (!State || !State->HasAuthenticatedCharacter() || !Projection || !ASC || (!Battle && !bAdmissionOnly)) return true;
		if (!Same(State->GetBackendCharacterId(), CharacterId) || !Same(State->GetBackendDungeonSessionId(), SessionId)
			|| !Same(State->GetAuthenticatedSteamId(), SteamId) || !Same(Flow->GetSelectedCharacterId(), CharacterId))
			return Fail(TEXT("AdmissionIdentityMismatch"));
		if (Stage == 2)
		{
			const UNetDriver* Driver = World->GetNetDriver();
			if (!Driver || !Driver->IsUsingIrisReplication() || ASC->GetAvatarActor() != Player || !Player->InputComponent
				|| Projection->GetLoadState() != ERPGInventoryProjectionLoadState::Ready
				|| (!bAdmissionOnly && Battle->GetBattleSnapshot().BattleState != EBossBattleState::InProgress)) return true;
			Stage = 3;
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_ADMITTED Iris=1 Identity=1 PawnGASInput=1"));
			if (bAbandonmentSettlement)
			{
				if (!ObserveAbandonmentSimulation(World, TEXT("Client"), State->GetPlayerId())) return Fail(TEXT("AbandonmentClientSimulation"));
				if (auto* Observer = GI->GetSubsystem<URPGDungeonEjectionNetworkClient>()) Observer->PlayerId = State->GetPlayerId();
				else return Fail(TEXT("MissingEjectionObserver"));
				UE_LOG(LogTemp, Display, TEXT("DUNGEON_ABANDON_SETTLEMENT ADMITTED Character=%s Steam=%s PlayerId=%d Session=%s"),
					*CharacterId, *SteamId, State->GetPlayerId(), *SessionId);
			}
		}
		// Admission-only reuse on testmap; the opted-in network probe owns the rest.
		if (bPartyHudInput) return TickPartyHudInput(World, PC);
		if (bGameplayAbandonment) return TickGameplayAbandonmentProbe(World, PC);
		if (bAdmissionOnly)
		{
			if (bVerifyDungeonClientInitialAbandonment && !bInitialAbandonmentSubmitted
				&& IFileManager::Get().FileExists(*InitialAbandonmentGateFile))
			{
				if (!Web->HasAccessToken() || !Same(Web->GetAuthenticatedSteamId(), SteamId)
					|| !Same(Flow->GetCurrentDungeonSession().DungeonSessionId, SessionId)
					|| Flow->IsRequestInFlight()) return Fail(TEXT("InitialDungeonAbandonmentIdentityMismatch"));
				bInitialAbandonmentSubmitted = true;
				Web->AbandonDungeonSession(SessionId, CharacterId, SteamId);
				UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_INITIAL_ABANDONMENT_SUBMITTED SameProcess=1 AdmittedClient=1 PlayerPost=1"));
			}
			if (bVerifyAbandonedReturn) return true; // await the production network-failure lobby travel
			// Stop observing the full reward/return scenario as well as its ticker.
			// An actual ejection may legitimately trigger production lobby recovery.
			Flow->OnLobbyTravelStarted.RemoveDynamic(this, &ThisClass::HandleLobbyTravel);
#if !UE_BUILD_SHIPPING
			if (FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2EAdmissionOnly")))
			{
				if (!bLobbyUI || UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) != TEXT("/Game/Maps/bossmap"))
					return Fail(TEXT("LobbyAdmissionOnlyRequiresActualBossMap"));
				UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_ADMISSION_ONLY_COMPLETE Session=%s Character=%s Steam=%s ActualBossMap=1 NoSettlementRequested=1"),
					*SessionId, *CharacterId, *SteamId);
			}
#endif
			return false;
		}
#if !UE_BUILD_SHIPPING
		if (FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryStackSplitE2E")))
		{
			if (!InventoryStackSplitProbe) InventoryStackSplitProbe = MakeShared<FRPGInventoryStackSplitE2E>();
			const int32 Result = InventoryStackSplitProbe->Tick(*PC);
			if (Result < 0) return Fail(*InventoryStackSplitProbe->Error);
			if (Result > 0 && FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryRecoveryRevocationE2E")))
				Flow->OnLobbyTravelStarted.RemoveDynamic(this, &ThisClass::HandleLobbyTravel);
			return Result == 0;
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryStackMergeE2E")))
		{
			if (!InventoryStackMergeProbe) InventoryStackMergeProbe = MakeShared<FRPGInventoryStackMergeE2E>();
			const int32 Result = InventoryStackMergeProbe->Tick(*PC);
			if (Result < 0) return Fail(*InventoryStackMergeProbe->Error);
			return Result == 0;
		}
#endif
		const auto Notice = State->GetRewardDeliveryNotice();
		if (bResultAbandonment && ResultAbandonmentPhase < 3) return TickResultAbandonmentProbe(World, PC);
		if (Stage == 30) return true;
		if (bReauthenticateAfterAdmission && !bExpiredInDungeon)
		{
			if (!IFileManager::Get().FileExists(*DungeonAuthExpiredFile) || Flow->IsRequestInFlight()) return true;
			if (Battle->GetBattleSnapshot().BattleState != EBossBattleState::InProgress || Notice.bApplied)
				return Fail(TEXT("AuthenticationNotExpiredDuringActiveDungeon"));
			if (Web->HasAccessToken()) { Flow->RefreshDungeonSession(); return true; }
			if (!Same(Web->GetAuthenticatedSteamId(), SteamId)
				|| !Same(Flow->GetCurrentDungeonSession().DungeonSessionId, SessionId)) return Fail(TEXT("DungeonExpiryLostIdentity"));
			bExpiredInDungeon = true;
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_DUNGEON_AUTH_EXPIRED InProgress=1 BeforeReward=1 TokenInvalidated=1 IdentityRetained=1"));
		}
		if (Battle->GetBattleSnapshot().BattleState == EBossBattleState::Defeated) return Fail(TEXT("UnexpectedDefeat"));
		if (Battle->GetBattleSnapshot().BattleState == EBossBattleState::Victory) StopNaturalPlayerAttack();
		TickNaturalPlayerAttack(World, PC);
		if (Battle->GetBattleSnapshot().BattleState != EBossBattleState::Victory || !Notice.bApplied) return true;
		StopNaturalPlayerAttack();
		// Use the authenticated session snapshot, including non-lobby probe callers.
		const auto* Expected = FindRewardExpectation(Flow->GetCurrentDungeonSession().Difficulty);
		if (!Expected) return Fail(TEXT("UnsupportedRewardDifficulty"));
		const int32 InventoryQuantity = Expected->Potions + Expected->Helms;
		const bool bFirstSliceStarter = Projection->GetProjectedItems().ContainsByPredicate([](const FRPGInventoryProjectionEntry& Item)
		{
			return Item.GetDefinitionId().PrimaryAssetName == TEXT("GameItem.Equipment.Progression.Starter.GreatSword")
				&& Item.GetContainerType() == ERPGItemContainerType::Equipment
				&& Item.GetSlotIndex() == int32(EEquipmentSlotType::Weapon_Primary_R);
		});
		// New characters have three starters; old characters retain their one legacy sword.
		const int32 ItemRecords = (bFirstSliceStarter ? 4 : 2) + (Expected->Helms > 0 ? 1 : 0);
		if (!Same(Notice.DungeonSessionId, SessionId) || !Same(Notice.RewardVersion, Expected->Version)
			|| Notice.InventoryQuantity != InventoryQuantity || Notice.MailQuantity != 0)
			return Fail(TEXT("RewardNoticeMismatch"));
		if (State->GetCurrencyBalance(TEXT("RosterGold")) != Expected->Gold || Projection->GetProjectedItems().Num() != ItemRecords
			|| !Projection->GetMailItems().IsEmpty()) return true; // independent owner-only replication can arrive later
		int32 Potions = 0, Helms = 0, Swords = 0, StarterChests = 0, StarterGloves = 0;
		for (const auto& Item : Projection->GetProjectedItems())
		{
			if (Item.GetDefinitionId().PrimaryAssetName == TEXT("GameItem.Consume.Potion.Red.Large")
				&& Item.GetContainerType() == ERPGItemContainerType::Inventory) Potions += Item.GetQuantity();
			if (Item.GetDefinitionId().PrimaryAssetName == TEXT("GameItem.Equipment.Helm.Default")
				&& Item.GetContainerType() == ERPGItemContainerType::Inventory) Helms += Item.GetQuantity();
			const FName SwordName = bFirstSliceStarter ? FName(TEXT("GameItem.Equipment.Progression.Starter.GreatSword"))
				: FName(TEXT("GameItem.Equipment.Weapon.GreatSword"));
			if (Item.GetDefinitionId().PrimaryAssetName == SwordName
				&& Item.GetContainerType() == ERPGItemContainerType::Equipment
				&& Item.GetSlotIndex() == int32(EEquipmentSlotType::Weapon_Primary_R)) Swords += Item.GetQuantity();
			if (bFirstSliceStarter && Item.GetContainerType() == ERPGItemContainerType::Equipment)
			{
				const auto& Growth = Item.GetEquipmentProgression();
				if (Item.GetDefinitionId().PrimaryAssetType.ToString() != TEXT("RPGItemDefinition")
					|| Item.GetDefinitionVersion() != 1 || Item.GetQuantity() != 1
					|| Item.GetBindState() != ERPGItemBindState::CharacterBound
					|| !Growth.GrowthBandId.Equals(TEXT("FirstSlice"), ESearchCase::CaseSensitive)
					|| Growth.RuleVersion != 1 || Growth.Level != 0)
					return Fail(TEXT("StarterProjectionMetadataMismatch"));
				if (Item.GetDefinitionId().PrimaryAssetName == TEXT("GameItem.Equipment.Progression.Starter.Chest")
					&& Item.GetSlotIndex() == int32(EEquipmentSlotType::Chest)) ++StarterChests;
				if (Item.GetDefinitionId().PrimaryAssetName == TEXT("GameItem.Equipment.Progression.Starter.Gloves")
					&& Item.GetSlotIndex() == int32(EEquipmentSlotType::Hands)) ++StarterGloves;
			}
		}
		if (Potions != Expected->Potions || Helms != Expected->Helms || Swords != 1
			|| (bFirstSliceStarter && (StarterChests != 1 || StarterGloves != 1))) return Fail(TEXT("RewardProjectionMismatch"));
		TArray<UUserWidget*> Widgets;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, URPGDungeonResultActionsWidget::StaticClass(), false);
		URPGDungeonResultActionsWidget* Result = nullptr;
		for (auto* Widget : Widgets)
			if (Widget->GetOwningPlayer() == PC && Widget->IsInViewport()) Result = Cast<URPGDungeonResultActionsWidget>(Widget);
		UButton* Button = Result ? Cast<UButton>(Result->GetWidgetFromName(TEXT("ReturnToLobbyButton"))) : nullptr;
		if (bReauthenticateAfterAdmission && !bResultReauthenticated)
		{
			auto* Login = Result ? Cast<UButton>(Result->GetWidgetFromName(TEXT("ResultLoginDevelopmentButton"))) : nullptr;
			auto* Refresh = Result ? Cast<UButton>(Result->GetWidgetFromName(TEXT("RefreshDungeonButton"))) : nullptr;
			if (!Login || !Login->GetIsEnabled() || !Button || !Refresh) return true;
			if (Button->GetIsEnabled() || Refresh->GetIsEnabled() || Web->HasAccessToken()) return Fail(TEXT("ExpiredResultActionsNotLocked"));
			Button->OnClicked.Broadcast(); Refresh->OnClicked.Broadcast(); // forced clicks must not bypass authentication
			if (Flow->IsRequestInFlight() || World->GetNetMode() != NM_Client) return Fail(TEXT("ExpiredResultClickBypassedGuard"));
			Stage = 30;
			Login->OnClicked.Broadcast();
			if (Login->GetIsEnabled()) return Fail(TEXT("ResultDuplicateLoginNotLocked"));
			Login->OnClicked.Broadcast();
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_RESULT_REAUTH_CLICK LiveButton=1 DuplicateLocked=1 ExpiredReturnRefreshBlocked=1"));
			return true;
		}
		if (!Button || !Button->GetIsEnabled() || Result->GetDisplayedRewardSummary().IsEmpty() || Flow->IsRequestInFlight()) return true;
		if (!Same(Flow->GetCurrentDungeonSession().State, TEXT("Cleared"))) return Fail(TEXT("PrematureReturnEnabled"));
		if (bReauthenticateAfterAdmission && !bResultReauthenticationVerified)
		{
			bResultReauthenticationVerified = true;
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_RESULT_REAUTH_RESTORED SameAccount=1 FreshTerminalRead=1 SelectionSessionRetained=1"));
		}
		Stage = 4;
		if (bFirstSliceStarter)
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_STARTER_READMODEL Family=FirstSlice Equipped=3 ExplicitZero=1 LegacyMigrated=0"));
		if (!ClickFlowButton(Button)) return false;
		if (!Flow->IsRequestInFlight()) return Fail(TEXT("ResultClickDidNotStartFreshRead"));
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_RETURN_CLICK LiveWidget=1 Gold=%lld InventoryQuantity=%d MailQuantity=0 RewardVersion=%s"),
			Expected->Gold, InventoryQuantity, Expected->Version);
	}
	if (Stage == 4)
	{
		bSawReturning |= Flow->GetFlowState() == ERPGDungeonClientFlowState::ReturningToLobby;
		if (World->GetNetMode() != NM_Standalone || FlowFixtureMapPackage(*World) != TEXT("/Game/Maps/LobbyMap")) return true;
		if (bLobbyUI)
		{
			auto* Widget = GI->GetSubsystem<URPGOnlineLobbySubsystem>()->GetLobbyWidget();
			if (!Widget || Widget->GetCharacterOption(CharacterId).IsEmpty()) return true;
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_LOBBY_UI_RESTORED SelectedRosterCharacter=1"));
		}
		if (!bSawReturning || Flow->GetFlowState() != ERPGDungeonClientFlowState::Idle || Flow->IsRequestInFlight()
			|| !Web->HasAccessToken() || !Same(Flow->GetSelectedCharacterId(), CharacterId)
			|| !Same(Flow->GetCurrentDungeonSession().DungeonSessionId, SessionId)
			|| !Same(Flow->GetCurrentDungeonSession().State, TEXT("Cleared"))
			|| World->URL.HasOption(TEXT("JoinTicket=")) || !World->URL.Host.IsEmpty())
			return Fail(TEXT("LobbyReturnStateMismatch"));
		Stage = 5;
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_LOBBY_COMPLETE ActualMap=1 Standalone=1 Idle=1 AuthSelectionRetained=1 NoJoinTicket=1"));
		return false;
	}
	if (Stage == 50)
	{
		if (World->GetNetMode() != NM_Standalone
			|| !Same(FlowFixtureMapPackage(*World), TEXT("/Game/Maps/LobbyMap"))) return true;
		if (!bSawReturning || Flow->GetFlowState() != ERPGDungeonClientFlowState::Idle
			|| Flow->IsRequestInFlight() || !Web->HasAccessToken()
			|| !Same(Web->GetAuthenticatedSteamId(), SteamId)
			|| !Same(Flow->GetSelectedCharacterId(), CharacterId)
			|| !Same(Flow->GetCurrentDungeonSession().DungeonSessionId, SessionId)
			|| Flow->HasUnresolvedDungeonAbandonment()
			|| World->URL.HasOption(TEXT("JoinTicket=")) || !World->URL.Host.IsEmpty())
			return Fail(TEXT("AbandonedClientLocalRecoveryStateMismatch"));
		Stage = 51;
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_ABANDONED_LOBBY_RETURN SameProcess=1 ActualMap=1 Standalone=1 AuthSelectionSessionRetained=1 NoImplicitReceipt=1 NoJoinTicket=1"));
		if (!bVerifyAbandonedReceipt) return false;
		Stage = 52;
		return true;
	}
	if (Stage == 52)
	{
		if (!Web->HasAccessToken() || !Same(Web->GetAuthenticatedSteamId(), SteamId)
			|| !Same(Flow->GetSelectedCharacterId(), CharacterId)
			|| !Same(Flow->GetCurrentDungeonSession().DungeonSessionId, SessionId)
			|| Flow->HasUnresolvedDungeonAbandonment())
			return Fail(TEXT("AbandonedClientReceiptIdentityChanged"));
		auto* Lobby = GI->GetSubsystem<URPGOnlineLobbySubsystem>();
		auto* Widget = Lobby ? Lobby->GetLobbyWidget() : nullptr;
		if (!Widget || Widget->GetOwningPlayer() != PC || Widget->GetCharacterOption(CharacterId).IsEmpty()) return true;
		auto* Abandon = Cast<UButton>(Widget->GetWidgetFromName(TEXT("AbandonDungeonSessionButton")));
		auto* Confirm = Cast<UButton>(Widget->GetWidgetFromName(TEXT("ConfirmDungeonAbandonmentButton")));
		if (!Abandon || !Confirm || !Abandon->GetIsEnabled()) return Fail(TEXT("AbandonedClientReceiptButtonUnavailable"));
		Abandon->OnClicked.Broadcast();
		if (!Confirm->GetIsEnabled()) return Fail(TEXT("AbandonedClientReceiptConfirmationUnavailable"));
		Stage = 53;
		Confirm->OnClicked.Broadcast();
		if (!Flow->IsRequestInFlight()) return Fail(TEXT("AbandonedClientReceiptPostNotStarted"));
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_ABANDONED_RECEIPT_SUBMITTED SameProcess=1 LiveLobbyConfirmation=1 PlayerPost=1"));
		return true;
	}
	if (Stage == 53)
	{
		if (Flow->IsRequestInFlight()) return true;
		auto* Lobby = GI->GetSubsystem<URPGOnlineLobbySubsystem>();
		auto* Widget = Lobby ? Lobby->GetLobbyWidget() : nullptr;
		auto* Create = Widget ? Cast<UButton>(Widget->GetWidgetFromName(TEXT("CreateDungeonSessionButton"))) : nullptr;
		if (Flow->HasUnresolvedDungeonAbandonment() || Flow->HasActiveDungeonSession()
			|| !Flow->GetCurrentDungeonSession().DungeonSessionId.IsEmpty()
			|| !Same(Flow->GetSelectedCharacterId(), CharacterId)
			|| !Web->HasAccessToken() || !Same(Web->GetAuthenticatedSteamId(), SteamId)
			|| Flow->GetFlowState() != ERPGDungeonClientFlowState::Idle
			|| !Same(Flow->GetLastErrorCode(), TEXT("dungeon_abandoned"))
			|| !Widget || Widget->GetOwningPlayer() != PC || Widget->GetCharacterOption(CharacterId).IsEmpty()
			|| !Create || !Create->GetIsEnabled())
			return Fail(TEXT("AbandonedClientReceiptNotApplied"));
		Stage = 54;
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_ABANDONED_RECEIPT_APPLIED SameProcess=1 ReceiptAccepted=1 SelectionAuthRetained=1 SessionCleared=1 NewDungeonEnabled=1"));
		return false;
	}
	return true;
}

bool URPGDungeonFlowE2EClient::TickAbandonmentProbe(UWorld* World, APlayerController* PC)
{
	auto* GI = GetGameInstance();
	auto* Flow = GI->GetSubsystem<URPGDungeonSessionSubsystem>();
	auto* Web = GI->GetSubsystem<UHttpWebManager>();
	auto* Lobby = GI->GetSubsystem<URPGOnlineLobbySubsystem>();
	auto* Widget = Lobby ? Lobby->GetLobbyWidget() : nullptr;
	if (!Flow || !Web || !Lobby || World->GetNetMode() != NM_Standalone
		|| !Same(World->GetOutermost()->GetName(), TEXT("/Game/Maps/LobbyMap"))
		|| !Flow->IsAbandonmentTestingEnabled())
		return Fail(TEXT("AbandonmentLobbyUnavailable"));
	// The automatic POST can fail before the lobby's .25s creation tick runs.
	// Wait only on this new arrival; map, account and operation checks stay intact.
	if (!Widget && (bResultAbandonmentResponseLoss || bGameplayAbandonmentResponseLoss) && Stage == 41) return true;
	if (!Widget || Widget->GetOwningPlayer() != PC) return Fail(TEXT("AbandonmentLobbyUnavailable"));
	if ((bResultAbandonmentResponseLoss || bGameplayAbandonmentResponseLoss) && Stage == 41
		&& (Widget->IsFrontendRequestInFlight() || Widget->GetCharacterOption(CharacterId).IsEmpty())) return true;
	if (Stage == 1)
	{
		auto* Selector = Cast<UComboBoxString>(Widget->GetWidgetFromName(TEXT("OwnedCharacters")));
		if (!Selector || !Web->HasAccessToken() || Flow->IsRequestInFlight()) return true;
		if (!bCharacterSelected)
		{
			const FString Option = Widget->GetCharacterOption(CharacterId);
			if (Option.IsEmpty() || !Selector->GetIsEnabled()) return true;
			bCharacterSelected = true;
			Selector->SetSelectedOption(Option); // real widget selection and Resume HTTP
			return true;
		}
		if (!Same(Flow->GetSelectedCharacterId(), CharacterId)) return Fail(TEXT("AbandonmentSelectionChanged"));
		const auto Session = Flow->GetCurrentDungeonSession();
		if (!Same(Session.DungeonSessionId, SessionId) || !Same(Session.State, TEXT("InProgress"))) return true;
		if (auto* Continue = Cast<UButton>(Widget->GetWidgetFromName(TEXT("ContinueWithCharacterButton"))); Continue && Continue->GetIsEnabled())
		{
			Continue->OnClicked.Broadcast();
			return true;
		}
		auto* Abandon = Cast<UButton>(Widget->GetWidgetFromName(TEXT("AbandonDungeonSessionButton")));
		if (!Abandon || !Abandon->GetIsEnabled()) return Fail(TEXT("AbandonmentActionUnavailable"));
		Stage = 40;
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_ABANDON_UI READY LiveLobby=1 OwnedSelection=1 ResumedSession=1"));
	}
	if (Stage == 40)
	{
		if (!IFileManager::Get().FileExists(*AbandonmentStartFile)) return true;
		auto* Abandon = Cast<UButton>(Widget->GetWidgetFromName(TEXT("AbandonDungeonSessionButton")));
		auto* Confirm = Cast<UButton>(Widget->GetWidgetFromName(TEXT("ConfirmDungeonAbandonmentButton")));
		if (!Abandon || !Confirm || !Abandon->GetIsEnabled()) return Fail(TEXT("AbandonmentInitialButtonUnavailable"));
		Abandon->OnClicked.Broadcast();
		if (!Confirm->GetIsEnabled()) return Fail(TEXT("AbandonmentConfirmationUnavailable"));
		Stage = 41;
		Confirm->OnClicked.Broadcast();
		if (!Flow->IsRequestInFlight()) return Fail(TEXT("AbandonmentInitialPostNotStarted"));
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_ABANDON_UI SUBMITTED LiveConfirmation=1 PlayerPost=1"));
		return true;
	}
	if (Stage == 41)
	{
		if (Flow->IsRequestInFlight()) return true;
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_ABANDON_UI INITIAL_RESULT Unresolved=%d SessionRetained=%d State=%d Error=%s"),
			Flow->HasUnresolvedDungeonAbandonment() ? 1 : 0,
			Same(Flow->GetCurrentDungeonSession().DungeonSessionId, SessionId) ? 1 : 0,
			static_cast<int32>(Flow->GetFlowState()), *Flow->GetLastErrorCode());
		if (!Flow->HasUnresolvedDungeonAbandonment() || !Same(Flow->GetCurrentDungeonSession().DungeonSessionId, SessionId)
			|| !Same(Flow->GetSelectedCharacterId(), CharacterId)) return Fail(TEXT("AbandonmentLostResponseForgotIdentity"));
		auto* Create = Cast<UButton>(Widget->GetWidgetFromName(TEXT("CreateDungeonSessionButton")));
		auto* Enter = Cast<UButton>(Widget->GetWidgetFromName(TEXT("EnterDungeonButton")));
		auto* Abandon = Cast<UButton>(Widget->GetWidgetFromName(TEXT("AbandonDungeonSessionButton")));
		if (!Create || !Enter || !Abandon || Create->GetIsEnabled() || Enter->GetIsEnabled() || !Abandon->GetIsEnabled())
			return Fail(TEXT("AbandonmentUnconfirmedUILockMismatch"));
		Stage = 42;
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_ABANDON_UI UNCONFIRMED IdentityRetained=1 NewDungeonLocked=1 RetryEnabled=1"));
	}
	if (Stage == 42)
	{
		if (!IFileManager::Get().FileExists(*AbandonmentRetryFile)) return true;
		auto* Abandon = Cast<UButton>(Widget->GetWidgetFromName(TEXT("AbandonDungeonSessionButton")));
		auto* Confirm = Cast<UButton>(Widget->GetWidgetFromName(TEXT("ConfirmDungeonAbandonmentButton")));
		if (!Abandon || !Confirm || !Abandon->GetIsEnabled()) return Fail(TEXT("AbandonmentRetryButtonUnavailable"));
		if (!ClickFlowButton(Abandon)) return false;
		if (!Confirm->GetIsEnabled()) return Fail(TEXT("AbandonmentRetryConfirmationUnavailable"));
		if (!VerifyFlowConfirmationNavigation(Abandon, Confirm,
			Cast<UButton>(Widget->GetWidgetFromName(TEXT("CancelDungeonAbandonmentButton"))))) return false;
		Stage = 43;
		if (!ClickFlowButton(Confirm)) return false;
		if (!Flow->IsRequestInFlight()) return Fail(TEXT("AbandonmentRetryPostNotStarted"));
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_ABANDON_UI RETRY_SUBMITTED LiveConfirmation=1 SameIdentity=1"));
		return true;
	}
	if (Stage == 43)
	{
		if (Flow->IsRequestInFlight()) return true;
		if (bAbandonmentReauthenticate)
		{
			auto* Login = Cast<UButton>(Widget->GetWidgetFromName(TEXT("LoginDevelopmentButton")));
			auto* Input = Cast<UEditableTextBox>(Widget->GetWidgetFromName(TEXT("DevelopmentSteamId")));
			auto* Create = Cast<UButton>(Widget->GetWidgetFromName(TEXT("CreateDungeonSessionButton")));
			auto* Enter = Cast<UButton>(Widget->GetWidgetFromName(TEXT("EnterDungeonButton")));
			if (!Flow->HasUnresolvedDungeonAbandonment() || Web->HasAccessToken()
				|| !Same(Web->GetAuthenticatedSteamId(), SteamId)
				|| !Same(Flow->GetSelectedCharacterId(), CharacterId)
				|| !Same(Flow->GetCurrentDungeonSession().DungeonSessionId, SessionId)
				|| !Login || !Login->GetIsEnabled() || !Input || Input->GetIsEnabled()
				|| !Same(Input->GetText().ToString(), SteamId)
				|| !Create || Create->GetIsEnabled() || !Enter || Enter->GetIsEnabled())
				return Fail(TEXT("AbandonmentExpiredAuthenticationNotPinned"));
			Stage = 45;
			if (!ClickFlowButton(Login)) return false;
			if (Login->GetIsEnabled()) return Fail(TEXT("AbandonmentDuplicateLoginNotLocked"));
			Login->OnClicked.Broadcast();
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_ABANDON_UI AUTH_EXPIRED Real401=1 AccountPinned=1 IdentityRetained=1 NewDungeonLocked=1 LiveLogin=1"));
			return true;
		}
	}
	if (Stage == 46)
	{
		auto* Selector = Cast<UComboBoxString>(Widget->GetWidgetFromName(TEXT("OwnedCharacters")));
		auto* Abandon = Cast<UButton>(Widget->GetWidgetFromName(TEXT("AbandonDungeonSessionButton")));
		auto* Create = Cast<UButton>(Widget->GetWidgetFromName(TEXT("CreateDungeonSessionButton")));
		if (!Selector || !Abandon || !Create || !Web->HasAccessToken() || Flow->IsRequestInFlight()
			|| Widget->GetCharacterOption(CharacterId).IsEmpty() || !Abandon->GetIsEnabled()) return true;
		if (!Same(Web->GetAuthenticatedSteamId(), SteamId)
			|| !Same(Selector->GetSelectedOption(), Widget->GetCharacterOption(CharacterId))
			|| !Same(Flow->GetSelectedCharacterId(), CharacterId)
			|| !Same(Flow->GetCurrentDungeonSession().DungeonSessionId, SessionId)
			|| !Flow->HasUnresolvedDungeonAbandonment() || Create->GetIsEnabled())
			return Fail(TEXT("AbandonmentReauthenticatedRosterOrSessionMismatch"));
		Stage = 47;
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_ABANDON_UI AUTH_RESTORED SameAccount=1 OwnedRoster=1 IdentityRetained=1 RetryEnabled=1"));
	}
	if (Stage == 47)
	{
		if (!IFileManager::Get().FileExists(*AbandonmentRenewedRetryFile)) return true;
		auto* Abandon = Cast<UButton>(Widget->GetWidgetFromName(TEXT("AbandonDungeonSessionButton")));
		auto* Confirm = Cast<UButton>(Widget->GetWidgetFromName(TEXT("ConfirmDungeonAbandonmentButton")));
		if (!Abandon || !Confirm || !Abandon->GetIsEnabled()) return Fail(TEXT("AbandonmentRenewedRetryUnavailable"));
		if (!ClickFlowButton(Abandon)) return false;
		if (!Confirm->GetIsEnabled()) return Fail(TEXT("AbandonmentRenewedConfirmationUnavailable"));
		if (!VerifyFlowConfirmationNavigation(Abandon, Confirm,
			Cast<UButton>(Widget->GetWidgetFromName(TEXT("CancelDungeonAbandonmentButton"))))) return false;
		Stage = 48;
		if (!ClickFlowButton(Confirm)) return false;
		if (!Flow->IsRequestInFlight()) return Fail(TEXT("AbandonmentRenewedPostNotStarted"));
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_ABANDON_UI RENEWED_RETRY_SUBMITTED LiveConfirmation=1 SameIdentity=1"));
		return true;
	}
	if (Stage == 43 || Stage == 48)
	{
		if (Flow->IsRequestInFlight()) return true;
		auto* Create = Cast<UButton>(Widget->GetWidgetFromName(TEXT("CreateDungeonSessionButton")));
		if (Flow->HasUnresolvedDungeonAbandonment() || Flow->HasActiveDungeonSession()
			|| !Flow->GetCurrentDungeonSession().DungeonSessionId.IsEmpty()
			|| !Same(Flow->GetSelectedCharacterId(), CharacterId) || !Same(Web->GetAuthenticatedSteamId(), SteamId)
			|| !Web->HasAccessToken() || !Create || !Create->GetIsEnabled())
			return Fail(TEXT("AbandonmentRetryDidNotResolveLobby"));
		if (bResultAbandonmentResponseLoss)
		{
			if (!bResultAbandonmentReceipt || !bSawReturning || Flow->HasQueuedDungeonAbandonment()
				|| Flow->GetFlowState() != ERPGDungeonClientFlowState::Idle
				|| !Same(Flow->GetLastErrorCode(), TEXT("dungeon_abandoned"))
				|| World->URL.HasOption(TEXT("JoinTicket=")) || !World->URL.Host.IsEmpty())
				return Fail(TEXT("ResultAbandonmentRecoveredReceiptOrLobbyMismatch"));
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_RESULT_ABANDON LOBBY_COMPLETE Receipt=1 SessionCleared=1 AuthSelectionRetained=1 NoJoinTicket=1"));
		}
		if (bGameplayAbandonmentResponseLoss)
		{
			// Reuse the gameplay path's exact lobby/receipt checks, not a synthetic success.
			if (!bGameplayAbandonmentReceipt) return Fail(TEXT("GameplayAbandonmentRecoveryMissingReceipt"));
			Stage = 60;
			TickGameplayAbandonmentProbe(World, PC);
			if (Stage != 61) return false;
		}
		Stage = 44;
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_ABANDON_UI RECOVERED LiveButton=1 ReceiptApplied=1 SelectionRetained=1 NewDungeonEnabled=1"));
		return false;
	}
	return true;
}

URPGDungeonFlowE2EDamageEffect::URPGDungeonFlowE2EDamageEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayEffectExecutionDefinition Execution;
	Execution.CalculationClass = UGE_ExecCalc_TakeDamage::StaticClass();
	Executions.Add(Execution);
}

URPGDungeonFlowE2EServer::URPGDungeonFlowE2EServer()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = .1f;
}

void URPGDungeonFlowE2EServer::BeginPlay()
{
	Super::BeginPlay();
	if (!Enabled()) return;
	FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2EGate="), GatePath);
	FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2ENaturalGateFile="), NaturalAttackGateFile);
	FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2EPlayers="), ExpectedPlayers);
#if !UE_BUILD_SHIPPING
	bAbandonmentSettlement = FParse::Param(FCommandLine::Get(), TEXT("RPGDungeonAbandonmentSettlementE2E"));
	bUnexpectedDisconnectSettlement = FParse::Param(FCommandLine::Get(), TEXT("RPGDungeonUnexpectedDisconnectE2E"));
	bReconnectWithinGrace = FParse::Param(FCommandLine::Get(), TEXT("RPGDungeonReconnectE2E"));
	bPartyCombatVitals = FParse::Param(FCommandLine::Get(), TEXT("RPGPartyCombatVitalsE2E"));
	bReconnectAdmissionLoss = FParse::Param(FCommandLine::Get(), TEXT("RPGReconnectAdmissionLossE2E"));
	bGameplayAbandonmentSettlement = FParse::Param(FCommandLine::Get(), TEXT("RPGGameplayAbandonmentSettlementE2E"));
	bNaturalPlayerAttack = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2ENaturalPlayerAttack"));
	bLiveBossAI = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2ELiveBossAI"));
	bResultAbandonment = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2EResultAbandonment"));
	FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2EResultSettleGate="), ResultSettleGateFile);
	FParse::Value(FCommandLine::Get(), TEXT("RPGAbandonCharacter="), AbandonCharacter);
	if (bResultAbandonment)
		FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2EResultAbandonCharacter="), AbandonCharacter);
#endif
	auto* Mode = Cast<ARPGBossBattleGameMode>(GetOwner());
	if (!Mode || GetWorld()->GetNetMode() != NM_DedicatedServer || GatePath.IsEmpty()
		|| ExpectedPlayers < 1 || ExpectedPlayers > 4
		|| (bLiveBossAI && (!bNaturalPlayerAttack || NaturalAttackGateFile.IsEmpty()))
		|| FParse::Param(FCommandLine::Get(), TEXT("RPGNetTestMode"))) { Fail(TEXT("UnsafeServerFixture")); return; }
	FGuid Target;
	if (bGameplayAbandonmentSettlement && (!bAbandonmentSettlement || ExpectedPlayers != 2 || bUnexpectedDisconnectSettlement
		|| bResultAbandonment || bNaturalPlayerAttack || bLiveBossAI))
	{ Fail(TEXT("UnsafeGameplayAbandonmentFixture")); return; }
	if (bReconnectAdmissionLoss && !bReconnectWithinGrace)
	{ Fail(TEXT("AdmissionLossRequiresReconnectFixture")); return; }
	if (bReconnectWithinGrace && !bUnexpectedDisconnectSettlement)
	{ Fail(TEXT("ReconnectRequiresUnexpectedLossFixture")); return; }
	if (bPartyCombatVitals && (!bReconnectWithinGrace || ExpectedPlayers != 2 || bReconnectAdmissionLoss
		|| bGameplayAbandonmentSettlement || bResultAbandonment || bNaturalPlayerAttack || bLiveBossAI))
	{ Fail(TEXT("PartyVitalsRequiresTwoPlayerReconnectFixture")); return; }
	if (bUnexpectedDisconnectSettlement && !bAbandonmentSettlement)
	{ Fail(TEXT("DisconnectRequiresPartySettlementFixture")); return; }
	if (bResultAbandonment && (ExpectedPlayers != 2 || bAbandonmentSettlement || bNaturalPlayerAttack || bLiveBossAI
		|| ResultSettleGateFile.IsEmpty()
		|| !FGuid::ParseExact(AbandonCharacter, EGuidFormats::DigitsWithHyphens, Target) || !Target.IsValid()))
	{ Fail(TEXT("InvalidResultAbandonmentFixture")); return; }
	if (bAbandonmentSettlement && (ExpectedPlayers < 2
		|| !FParse::Param(FCommandLine::Get(), TEXT("RPGDungeonMembershipBackendE2E"))
		|| !FGuid::ParseExact(AbandonCharacter, EGuidFormats::DigitsWithHyphens, Target) || !Target.IsValid()))
	{ Fail(TEXT("InvalidAbandonmentFixture")); return; }
	// Runtime test instance only: allow cold client loading without changing the authored BP timer.
	FFloatProperty* Time = FindFProperty<FFloatProperty>(Mode->GetClass(), TEXT("BattleTimeLimit"));
	if (!Time) { Fail(TEXT("MissingBattleTimer")); return; }
	Time->SetPropertyValue_InContainer(Mode, static_cast<float>(FlowFixtureTimeout()));
	// Components begin before the owning GameMode sends its authenticated start.
	// Observe the real waiting boundary; never seed either local difficulty value.
	const auto* Battle = GetWorld()->GetGameState<ARPGBossBattleGameState>();
	const auto* Web = GetWorld()->GetGameInstance()->GetSubsystem<UHttpWebManager>();
	if (!Web || !Web->IsConfiguredForDungeonServer() || !Battle
		|| Mode->IsBackendDungeonDifficultyReady() || Battle->GetBattleSnapshot().Boss
		|| Battle->GetBattleSnapshot().BattleState != EBossBattleState::Waiting)
	{ Fail(TEXT("BossCreatedBeforeBackendConfiguration")); return; }
	UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_CONFIG_WAITING BackendReady=0 NoBoss=1 Waiting=1 LocalDifficultyWrite=0"));
	Deadline = FPlatformTime::Seconds() + FlowFixtureTimeout();
	SetComponentTickEnabled(true);
}

void URPGDungeonFlowE2EServer::Fail(const TCHAR* Reason)
{
	UE_LOG(LogTemp, Error, TEXT("DUNGEON_FLOW_E2E_FAIL Reason=%s ServerStage=%d"), Reason, Stage);
	SetComponentTickEnabled(false);
}

void URPGDungeonFlowE2EServer::HandleMemberRevoked(const FString&)
{
#if !UE_BUILD_SHIPPING
	if (!bAbandonmentSettlement || bRevocationObserved || Stage != 1 || InitialParty.IsEmpty()) return;
	const FPartyPeer& Target = InitialParty[0];
	if (!IsInGameThread() || !Target.State.IsValid() || !Target.State->IsAuthenticatedIdentityRevoked()
		|| !Target.Controller.IsValid() || !Target.Pawn.IsValid() || !Target.Connection.IsValid()
		|| Target.Controller->GetPawn() != Target.Pawn.Get() || Target.Connection->GetConnectionState() != USOCK_Open)
	{ Fail(TEXT("AbandonmentNotificationOrder")); return; }
	bRevocationObserved = true;
	RevocationTime = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Display, TEXT("DUNGEON_ABANDON_SETTLEMENT REVOCATION Character=%s Steam=%s PlayerId=%d Revoked=1 GameThread=1 ConnectionOpen=1 ActorsRetained=1"),
		*Target.CharacterId, *Target.SteamId, Target.State->GetPlayerId());
#endif
}

bool URPGDungeonFlowE2EServer::ObserveAbandonmentParty()
{
#if !UE_BUILD_SHIPPING
	const auto* Battle = GetWorld()->GetGameState<ARPGBossBattleGameState>();
	const auto* Driver = GetWorld()->GetNetDriver();
	const auto* Web = GetWorld()->GetGameInstance()->GetSubsystem<UHttpWebManager>();
	if (!Battle || !Driver || !Web) { Fail(TEXT("AbandonmentWorldMissing")); return false; }
	const auto Snapshot = Battle->GetBattleSnapshot();
	if (InitialParty.IsEmpty() && Snapshot.BattleState == EBossBattleState::Waiting) return false;
	if (Snapshot.BattleState != EBossBattleState::InProgress || !Snapshot.Boss)
	{ Fail(TEXT("RemainingPartyBattleInterrupted")); return false; }
	if (InitialParty.IsEmpty())
	{
		if (Driver->ClientConnections.Num() != ExpectedPlayers) return false;
		TArray<FPartyPeer> Ready;
		for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			auto* PC = It->Get();
			auto* State = PC ? PC->GetPlayerState<ARPGPlayerState>() : nullptr;
			const auto* Projection = PC ? PC->FindComponentByClass<URPGInventoryProjectionComponent>() : nullptr;
			if (!State || !State->HasAuthenticatedCharacter() || !PC->GetPawn() || !Projection
				|| Projection->GetLoadState() != ERPGInventoryProjectionLoadState::Ready) return false;
			if (State->GetBackendDungeonSessionId() != Web->GetConfiguredDungeonSessionId())
			{ Fail(TEXT("AbandonmentPartySessionMismatch")); return false; }
			FPartyPeer& Peer = Ready.AddDefaulted_GetRef();
			Peer.Controller=PC; Peer.State=State; Peer.Pawn=PC->GetPawn(); Peer.Connection=PC->GetNetConnection();
			Peer.CharacterId=State->GetBackendCharacterId(); Peer.SteamId=State->GetAuthenticatedSteamId();
			Peer.ConnectionGeneration=State->GetBackendConnectionGeneration();
			if (!Peer.Connection.IsValid() || Peer.Connection->GetConnectionState() != USOCK_Open) return false;
		}
		if (Ready.Num() != ExpectedPlayers) return false;
		const int32 Target = Ready.IndexOfByPredicate([this](const FPartyPeer& Peer) { return Peer.CharacterId == AbandonCharacter; });
		if (Target == INDEX_NONE) { Fail(TEXT("AbandonmentTargetNotAdmitted")); return false; }
		Ready.Swap(0, Target); InitialParty=MoveTemp(Ready);
		InitialBoss=Snapshot.Boss; InitialEncounterId=Snapshot.Boss->BossAIComponent->GetBossState().EncounterId;
		for (const FPartyPeer& Peer : InitialParty)
		{
			if (!ObserveAbandonmentSimulation(GetWorld(), TEXT("Server"), Peer.State->GetPlayerId()))
			{ Fail(TEXT("AbandonmentServerSimulation")); return false; }
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_ABANDON_SETTLEMENT PEER Character=%s Steam=%s PlayerId=%d Authenticated=1"),
				*Peer.CharacterId, *Peer.SteamId, Peer.State->GetPlayerId());
		}
		if (!bUnexpectedDisconnectSettlement)
			InitialParty[0].State->OnCharacterIdentityChanged.AddUniqueDynamic(this, &ThisClass::HandleMemberRevoked);
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_ABANDON_SETTLEMENT PARTY_READY Session=%s Server=%s Target=%s Players=%d Iris=1 NoIdentitySeed=1 NoReceiptSeed=1"),
			*Web->GetConfiguredDungeonSessionId(), *Web->GetGameServerId(), *AbandonCharacter, ExpectedPlayers);
		if (bReconnectWithinGrace)
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_RECONNECT_SETTLEMENT READY Target=%s OriginalGeneration=%lld OriginalPlayerId=%d Players=%d"),
				*AbandonCharacter, InitialParty[0].ConnectionGeneration, InitialParty[0].State->GetPlayerId(), ExpectedPlayers);
		return false;
	}
	if (Snapshot.Boss != InitialBoss.Get() || Snapshot.Boss->BossAIComponent->GetBossState().EncounterId != InitialEncounterId)
	{ Fail(TEXT("RemainingPartyEncounterReplaced")); return false; }
	for (int32 Index=1; Index<InitialParty.Num(); ++Index)
	{
		const FPartyPeer& Peer=InitialParty[Index];
		if (!Peer.Controller.IsValid() || !Peer.Pawn.IsValid() || !Peer.State.IsValid() || !Peer.Connection.IsValid()
			|| Peer.Controller->GetPawn() != Peer.Pawn.Get() || Peer.Controller->GetPlayerState<ARPGPlayerState>() != Peer.State.Get()
			|| Peer.Controller->GetNetConnection() != Peer.Connection.Get() || Peer.Connection->GetConnectionState() != USOCK_Open
			|| !Peer.State->HasAuthenticatedCharacter() || Peer.State->GetBackendCharacterId() != Peer.CharacterId
			|| Peer.State->GetAuthenticatedSteamId() != Peer.SteamId || Peer.State->GetBackendDungeonSessionId() != Web->GetConfiguredDungeonSessionId()
			|| Peer.State->GetBackendConnectionGeneration() != Peer.ConnectionGeneration)
		{ Fail(TEXT("RemainingPartyIdentityChanged")); return false; }
	}
	if (bUnexpectedDisconnectSettlement)
	{
		const FPartyPeer& Target = InitialParty[0];
		if (Target.Controller.IsValid() || Target.Pawn.IsValid()
			|| (Target.Connection.IsValid() && Driver->ClientConnections.Contains(Target.Connection.Get()))) return false;
		if (!bUnexpectedLossObserved)
		{
			if (Driver->ClientConnections.Num() != ExpectedPlayers-1) return false;
			bUnexpectedLossObserved = true;
			RevocationTime = FPlatformTime::Seconds();
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_DISCONNECT_SETTLEMENT SOCKET_LOST Target=%s Remaining=%d TargetActorsRemoved=1 SamePeers=1 SameEncounter=1 NoPlayerAbandonment=1"),
				*Target.CharacterId, ExpectedPlayers-1);
		}
		if (bReconnectWithinGrace)
		{
			if (bReconnectAdmissionLoss)
			{
				const auto* Mode = Cast<ARPGGameModeBase>(GetOwner());
				if (!Mode) { Fail(TEXT("ReconnectModeMissing")); return false; }
				for (const auto& Entry : Mode->AdmissionsAwaitingBackend)
				{
					const auto& Awaiting = Entry.Value;
					if (Awaiting.Admission.CharacterId != Target.CharacterId
						|| Awaiting.Admission.ConnectionGeneration != Target.ConnectionGeneration+1) continue;
					auto* PC = Entry.Key.Get();
					const auto* State = Awaiting.PlayerState.Get();
					if (!PC || !State || !Awaiting.Connection.IsValid()
						|| Awaiting.Admission.SteamId != Target.SteamId
						|| Awaiting.Admission.DungeonSessionId != Web->GetConfiguredDungeonSessionId()
						|| PC->GetPawn() || State->HasAuthenticatedCharacter()
						|| State->GetBackendConnectionGeneration() != 0)
					{ Fail(TEXT("UnconfirmedReconnectActivated")); return false; }
					if (PendingReconnectGeneration == 0)
					{
						PendingReconnectController=PC; PendingReconnectGeneration=Awaiting.Admission.ConnectionGeneration;
						UE_LOG(LogTemp, Display, TEXT("DUNGEON_RECONNECT_SETTLEMENT CONFIRMATION_PENDING Target=%s Generation=%lld PlayerId=%d NoIdentity=1 NoPawn=1 SamePeers=1 SameEncounter=1"),
							*Target.CharacterId, PendingReconnectGeneration, State->GetPlayerId());
					}
				}
				if (PendingReconnectGeneration > 0 && !bPendingReconnectCleaned && !PendingReconnectController.IsValid()
					&& Driver->ClientConnections.Num() == ExpectedPlayers-1)
				{
					bPendingReconnectCleaned=true;
					UE_LOG(LogTemp, Display, TEXT("DUNGEON_RECONNECT_SETTLEMENT PENDING_SOCKET_LOST Target=%s Generation=%lld NoActivation=1 SamePeers=1 SameEncounter=1"),
						*Target.CharacterId, PendingReconnectGeneration);
				}
			}
			APlayerController* Replacement = nullptr;
			for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				auto* PC = It->Get();
				const auto* State = PC ? PC->GetPlayerState<ARPGPlayerState>() : nullptr;
				if (!State || !State->HasAuthenticatedCharacter() || State->GetBackendCharacterId() != Target.CharacterId) continue;
				if (Replacement) { Fail(TEXT("DuplicateReconnectedCharacter")); return false; }
				Replacement = PC;
			}
			if (!Replacement || Driver->ClientConnections.Num() != ExpectedPlayers) return false;
			auto* State = Replacement->GetPlayerState<ARPGPlayerState>();
			auto* Connection = Replacement->GetNetConnection();
			const auto* Projection = Replacement->FindComponentByClass<URPGInventoryProjectionComponent>();
			if (!State || State->IsAuthenticatedIdentityRevoked() || State->GetAuthenticatedSteamId() != Target.SteamId
				|| State->GetBackendDungeonSessionId() != Web->GetConfiguredDungeonSessionId()
				|| State->GetBackendConnectionGeneration() <= Target.ConnectionGeneration
				|| !Connection || Connection->GetConnectionState() != USOCK_Open || !Driver->ClientConnections.Contains(Connection)
				|| !Replacement->GetPawn() || !Projection || Projection->GetLoadState() != ERPGInventoryProjectionLoadState::Ready) return false;
			if (ReconnectedPeer.ConnectionGeneration == 0)
			{
				ReconnectedPeer.Controller=Replacement; ReconnectedPeer.State=State; ReconnectedPeer.Pawn=Replacement->GetPawn();
				ReconnectedPeer.Connection=Connection; ReconnectedPeer.ConnectionGeneration=State->GetBackendConnectionGeneration();
				UE_LOG(LogTemp, Display, TEXT("DUNGEON_RECONNECT_SETTLEMENT RECONNECTED Target=%s OriginalGeneration=%lld Generation=%lld PlayerId=%d Players=%d NewConnection=1 SamePeers=1 SameEncounter=1 Authenticated=1"),
					*Target.CharacterId, Target.ConnectionGeneration, ReconnectedPeer.ConnectionGeneration, State->GetPlayerId(), ExpectedPlayers);
			}
			if (ReconnectedPeer.Controller.Get() != Replacement || ReconnectedPeer.Pawn.Get() != Replacement->GetPawn()
				|| ReconnectedPeer.State.Get() != State || ReconnectedPeer.Connection.Get() != Connection
				|| ReconnectedPeer.ConnectionGeneration != State->GetBackendConnectionGeneration())
			{ Fail(TEXT("ReconnectedPeerReplacedBeforeVictory")); return false; }
			return true;
		}
		if (Driver->ClientConnections.Num() != ExpectedPlayers-1) return false;
		const auto* Mode = Cast<ARPGGameModeBase>(GetOwner());
		// Read the production poller's immutable cache, never seed a receipt or issue an expiry POST here.
		if (!Mode || Mode->GetDungeonAdmissionRevocationReason(Web->GetConfiguredDungeonSessionId(),
			Target.CharacterId, Target.SteamId) != TEXT("dungeon_member_disconnected_expired")) return false;
		if (!bExpiryObserved)
		{
			bExpiryObserved = true;
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_DISCONNECT_SETTLEMENT EXPIRY_CACHED Target=%s Remaining=%d ImmutableReceipt=1 ProductionPolling=1 NoReceiptSeed=1"),
				*Target.CharacterId, ExpectedPlayers-1);
		}
	}
	if (bGameplayAbandonmentSettlement && !bRevocationObserved && !InitialParty[0].Controller.IsValid()
		&& !InitialParty[0].Pawn.IsValid() && Driver->ClientConnections.Num() == ExpectedPlayers-1)
	{
		const auto* Mode = Cast<ARPGGameModeBase>(GetOwner());
		if (Mode && Mode->GetDungeonAdmissionRevocationReason(Web->GetConfiguredDungeonSessionId(),
			InitialParty[0].CharacterId, InitialParty[0].SteamId) == TEXT("dungeon_member_abandoned"))
		{
			bRevocationObserved = true;
			RevocationTime = FPlatformTime::Seconds();
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_GAMEPLAY_ABANDON SERVER_CACHED Target=%s Generation=%lld Remaining=%d PlannedSocketClosed=1 ProductionPolling=1 SamePeers=1 SameEncounter=1"),
				*InitialParty[0].CharacterId, InitialParty[0].ConnectionGeneration, ExpectedPlayers-1);
		}
	}
	if (!(bUnexpectedDisconnectSettlement ? bExpiryObserved : bRevocationObserved)
		|| InitialParty[0].Controller.IsValid() || InitialParty[0].Pawn.IsValid()
		|| Driver->ClientConnections.Num() != ExpectedPlayers-1 || FPlatformTime::Seconds()-RevocationTime < 5.0) return false;
	if (!bRemainingPartyReady)
	{
		bRemainingPartyReady=true;
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_ABANDON_SETTLEMENT REMAINING_READY Target=%s Remaining=%d TargetActorsRemoved=1 SamePeers=1 SameEncounter=1 InProgress=1"),
			*AbandonCharacter, ExpectedPlayers-1);
	}
	return true;
#else
	return false;
#endif
}

void URPGDungeonFlowE2EServer::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (FPlatformTime::Seconds() > Deadline) { Fail(TEXT("ServerTimeout")); return; }
	auto* Battle = GetWorld()->GetGameState<ARPGBossBattleGameState>();
	if (!Battle) return;
	const auto Snapshot = Battle->GetBattleSnapshot();
	ARPGBossCharacter* Boss = Snapshot.Boss;
	if (Boss && (!bLiveBossAI || !bLiveBossAIStarted))
		if (auto* AI = Cast<AAIController>(Boss->GetController()))
			if (auto* Brain = AI->GetBrainComponent(); Brain && Brain->IsRunning())
				Brain->StopLogic(TEXT("Opt-in dungeon backend/return admission barrier"));
	if (Stage == 0)
	{
		const auto* Mode = Cast<ARPGBossBattleGameMode>(GetOwner());
		const auto* Web = GetWorld()->GetGameInstance()->GetSubsystem<UHttpWebManager>();
		const auto* Driver = GetWorld()->GetNetDriver();
		if (!Mode || !Web || !Web->IsConfiguredForDungeonServer()) { Fail(TEXT("MissingServerAssignment")); return; }
		if (!Mode->IsBackendDungeonDifficultyReady())
		{
			if (Boss || Snapshot.BattleState != EBossBattleState::Waiting)
				Fail(TEXT("BossCreatedBeforeBackendConfiguration"));
			return;
		}
		const auto* Instance = GetWorld()->GetGameInstance<URPGGameInstance>();
		if (FParse::Param(FCommandLine::Get(), TEXT("RPGVerifyServerDungeonContent")))
		{
			if (Mode->GetConfirmedDungeonContentId() != TEXT("Dungeon.RaidSwordMan")
				|| !Mode->ConfirmedDungeonDefinition || !Mode->ConfirmedDungeonRewardDefinition
				|| Mode->ConfirmedDungeonRewardVersion.IsEmpty()) { Fail(TEXT("ServerDungeonContentNotPinned")); return; }
			// Emitted only after the existing startup barrier advances below.
		}
		if (!Instance || Instance->GetPendingGameDifficulty() != Mode->GetCurrentGameDifficulty())
		{ Fail(TEXT("ConfirmedDifficultyNotSynchronized")); return; }
		if (!Boss || !Boss->IsNPCStartUpDataReady() || !Driver || !Driver->IsUsingIrisReplication()) return;
		const auto* ASC = Boss->GetAbilitySystemComponent();
		const float InitialHealth = ASC ? ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) : 0.f;
		if (!FMath::IsFinite(InitialHealth) || InitialHealth <= 0.f)
		{ Fail(TEXT("ConfiguredBossStartupHealthInvalid")); return; }
		const int32 ExpectedLevel = static_cast<int32>(Mode->GetCurrentGameDifficulty()) + 1;
		int32 StartupNPCAbilities = 0;
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			if (!Spec.Ability || !Spec.Ability->IsA<URPGNPCGameplayAbility>() || Spec.SourceObject.Get() != Boss) continue;
			if (Spec.Level != ExpectedLevel) { Fail(TEXT("BossAbilityLevelDoesNotMatchBackendDifficulty")); return; }
			++StartupNPCAbilities;
		}
		if (StartupNPCAbilities == 0) { Fail(TEXT("ConfiguredBossStartupAbilitiesMissing")); return; }
		const FString Difficulty = StaticEnum<ERPGGameDifficulty>()->GetNameStringByValue(
			static_cast<int64>(Mode->GetCurrentGameDifficulty()));
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_CONFIG_CONFIRMED BackendReady=1 Difficulty=%s LocalDifficultyWrite=0 BossStartupReady=1 StartupAbilityLevel=%d NPCAbilities=%d InitialBossHP=%g"),
			*Difficulty, ExpectedLevel, StartupNPCAbilities, InitialHealth);
		if (FParse::Param(FCommandLine::Get(), TEXT("RPGVerifyServerDungeonContent")))
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_SERVER_CONTENT CONFIRMED Dungeon=%s Map=%s Difficulty=%s RewardVersion=%s AuthenticatedConfiguration=1 PinnedPayload=1"),
				*Mode->GetConfirmedDungeonContentId(), *GetWorld()->GetOutermost()->GetName(), *Difficulty, *Mode->ConfirmedDungeonRewardVersion);
		Stage = 1;
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_SERVER_READY ActualBossMap=1 Iris=1 Assignment=1 AdmissionBypass=0"));
	}
	if (Stage == 1 && bAbandonmentSettlement)
	{
		const bool bPartyReady = ObserveAbandonmentParty();
		if (bPartyCombatVitals && IsComponentTickEnabled()) ObservePartyCombatVitals();
		if (!bPartyReady || !IsComponentTickEnabled()) return;
	}
#if !UE_BUILD_SHIPPING
	if (Stage == 1 && FParse::Param(FCommandLine::Get(), TEXT("RPGPendingAdmissionAbandonmentE2E")))
	{
		ObservePendingAdmissionAbandonment();
		return; // Never opens the normal victory/settlement gate in this scenario.
	}
	if (Stage == 1 && FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryConsumeRecoveryServerE2E")))
	{
		if (ExpectedPlayers != 1 || bAbandonmentSettlement || bResultAbandonment || bNaturalPlayerAttack || bLiveBossAI)
		{ Fail(TEXT("ConsumeRecoveryUnsafeServerMode")); return; }
		if (!ConsumeRecoveryProbe) ConsumeRecoveryProbe = MakeShared<FRPGConsumeRecoveryServerProbe>();
		if (ConsumeRecoveryProbe->Tick(*GetWorld()) < 0) { Fail(*ConsumeRecoveryProbe->Error); return; }
	}
#endif
	if (Stage == 1 && bResultAbandonment && !ObserveResultAbandonmentRetry()) return;
	if (Stage == 1 && bRemainingPartyReady
		&& FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryRecoveryRevocationE2E"))
		&& IFileManager::Get().FileExists(*(GatePath + TEXT(".inventory-revocation-complete"))))
	{
		UE_LOG(LogTemp, Display, TEXT("RPG_INVENTORY_REVOCATION_OBSERVATION_COMPLETE Target=%s Remaining=1 ActorsRemoved=1"), *AbandonCharacter);
		SetComponentTickEnabled(false); // Stop this opt-in observer before the harness intentionally closes the remaining clients.
		return;
	}
	if (Stage == 1 && IFileManager::Get().FileExists(*GatePath))
	{
		if (!Boss || Snapshot.BattleState != EBossBattleState::InProgress) { Fail(TEXT("BattleNotInProgress")); return; }
		APawn* Source = nullptr;
		TSet<FString> Characters;
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			const auto* State = PC ? PC->GetPlayerState<ARPGPlayerState>() : nullptr;
			const auto* Projection = PC ? PC->FindComponentByClass<URPGInventoryProjectionComponent>() : nullptr;
			if (!State || !State->HasAuthenticatedCharacter() || !PC->GetPawn() || !Projection
				|| Projection->GetLoadState() != ERPGInventoryProjectionLoadState::Ready) { Fail(TEXT("PartyNotAuthenticated")); return; }
			Characters.Add(State->GetBackendCharacterId());
			Source = PC->GetPawn();
		}
		const bool bExcludeTarget = (bAbandonmentSettlement && !bReconnectWithinGrace) || bResultAbandonment;
		if (Characters.Num() != ExpectedPlayers-(bExcludeTarget ? 1 : 0)
			|| (bExcludeTarget && Characters.Contains(AbandonCharacter))) { Fail(TEXT("PartyCountMismatch")); return; }
		if (bNaturalPlayerAttack)
		{
			LiveBossAIPlayerHealthBefore.Reset();
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				APlayerController* PC = It->Get();
				const auto* State = PC->GetPlayerState<ARPGPlayerState>();
				const auto* Projection = PC->FindComponentByClass<URPGInventoryProjectionComponent>();
				const auto* Player = Cast<ARPGPlayer>(PC->GetPawn());
				const auto* Equipment = Player ? Player->FindComponentByClass<URPGAuthoritativeEquipmentComponent>() : nullptr;
				const auto* Loadout = Player ? Player->FindComponentByClass<URPGPlayerWeaponLoadoutComponent>() : nullptr;
				ARPGPlayerWeapon* Weapon = Player ? Player->GetPlayerCombatComponent()->GetPlayerCurrentEquippedWeapon() : nullptr;
				FRPGItemOwnerRef Owner;
				Owner.Type = ERPGItemOwnerType::Character;
				Owner.OwnerId = State->GetBackendCharacterId();
				TArray<FRPGItemRecord> Records;
				const auto* RecordSource = Projection->GetAuthoritativeRecordSource();
				if (!RecordSource) { Fail(TEXT("NaturalAttackRecordSourceMissing")); return; }
				RecordSource->FindByOwner(Owner, Records);
				FRPGEquipmentSnapshot SnapshotItem;
				if (!Records.ContainsByPredicate([](const FRPGItemRecord& Record)
					{ return Record.IsActive() && (Record.GetDefinitionId().PrimaryAssetName == TEXT("GameItem.Equipment.Weapon.GreatSword")
						|| Record.GetDefinitionId().PrimaryAssetName == TEXT("GameItem.Equipment.Progression.Starter.GreatSword"))
						&& Record.GetLocation().ContainerType == ERPGItemContainerType::Equipment
						&& Record.GetLocation().SlotIndex == int32(EEquipmentSlotType::Weapon_Primary_R); }))
				{ Fail(TEXT("PersistedStarterWeaponMissing")); return; }
				if (!Equipment || !Equipment->FindSnapshotInSlot(EEquipmentSlotType::Weapon_Primary_R, SnapshotItem)
					|| !Weapon || SnapshotItem.EquippedActor != Weapon || !Loadout || !Loadout->IsNativeManaged()) return;
				const auto* ASC = Player->GetAbilitySystemComponent();
				if (!ASC || !ASC->GetActivatableAbilities().ContainsByPredicate([Weapon](const FGameplayAbilitySpec& Spec)
					{ return Spec.SourceObject.Get() == Weapon && Spec.GetDynamicSpecSourceTags().HasTagExact(RPGGameplayTags::InputTag_LightAttack_GreatSword); })) return;
				if (bLiveBossAI)
				{
					const float PlayerHealth = ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
					if (!FMath::IsFinite(PlayerHealth) || PlayerHealth <= 0.f)
					{ Fail(TEXT("LiveBossAIPartyNotAlive")); return; }
					LiveBossAIPlayerHealthBefore.Add(State->GetBackendCharacterId(), PlayerHealth);
				}
			}
			NaturalBossHealthBefore = Boss->GetAbilitySystemComponent()->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
			if (!FMath::IsFinite(NaturalBossHealthBefore) || NaturalBossHealthBefore <= 0.f)
			{ Fail(TEXT("NaturalAttackBossHealthInvalid")); return; }
			Stage = 2;
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_NATURAL_READY PersistedStarterWeapons=%d NativeAbilities=1 ManualDamage=0 BossHP=%g"),
				Characters.Num(), NaturalBossHealthBefore);
			return;
		}
		auto* Player = Cast<ARPGPlayer>(Source);
		auto* ASC = Player ? Player->GetAbilitySystemComponent() : nullptr;
		auto* Threat = Boss->FindComponentByClass<URPGBossThreatComponent>();
		if (!ASC || !Threat) { Fail(TEXT("DamageComponentsMissing")); return; }
		FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(URPGDungeonFlowE2EDamageEffect::StaticClass(), 1.f, ASC->MakeEffectContext());
		if (!Spec.IsValid()) { Fail(TEXT("DamageSpecInvalid")); return; }
		Spec.Data->SetSetByCallerMagnitude(RPGGameplayTags::Shared_SetByCaller_BaseDamage, 10000.f);
		Spec.Data->SetSetByCallerMagnitude(RPGGameplayTags::Shared_SetByCaller_IdentityGain, 0.f);
		const FHitResult Hit(Boss, Boss->GetCapsuleComponent(), Boss->GetActorLocation(), FVector::UpVector);
		float HealthDamage = 0.f;
		bool bThreatAdded = false;
		Stage = 2; // before synchronous GAS death/settlement callbacks
		if (!Threat->ApplyDamageFromServerHit(Source, Hit, Spec, Boss->BossAIComponent->GetBossState().EncounterId,
			FRPGSkillSecurityProfile(), HealthDamage, bThreatAdded) || HealthDamage <= 0.f)
		{ Fail(TEXT("ServerDamageRejected")); return; }
		if (Boss->BossAIComponent->GetBossState().EncounterState != ERPGBossEncounterState::Dead
			|| Battle->GetBattleSnapshot().BattleState != EBossBattleState::Victory)
		{ Fail(TEXT("ProductionDeathDidNotWin")); return; }
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_SERVER_VICTORY ServerGASHit=1 DeadToVictory=1 Party=%d"), Characters.Num());
		if (bReconnectWithinGrace)
		{
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_RECONNECT_SETTLEMENT VICTORY Players=%d ServerGASHit=1 DeadToVictory=1 NoDirectSettlement=1"), Characters.Num());
		}
		else if (bAbandonmentSettlement)
		{
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_ABANDON_SETTLEMENT VICTORY Source=%s Remaining=%d ServerGASHit=1 DeadToVictory=1 NoDirectSettlement=1"),
				*Player->GetPlayerState<ARPGPlayerState>()->GetBackendCharacterId(), Characters.Num());
		}
		SetComponentTickEnabled(false);
	}
	if (Stage == 2 && bNaturalPlayerAttack)
	{
		if (!Boss) { Fail(TEXT("NaturalAttackBossLost")); return; }
		if (bLiveBossAI && !bLiveBossAIStarted)
		{
			if (!IFileManager::Get().FileExists(*NaturalAttackGateFile)) return;
			auto* AI = Cast<AAIController>(Boss->GetController());
			auto* Brain = AI ? AI->GetBrainComponent() : nullptr;
			if (!Brain) { Fail(TEXT("LiveBossAIBrainMissing")); return; }
			Brain->RestartLogic();
			bLiveBossAIStarted = true;
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_LIVE_AI_STARTED BrainRestarted=1 ManualDamage=0"));
		}
		if (bLiveBossAI)
		{
			const FRPGBossTelegraphState Telegraph = Boss->BossAIComponent->GetTelegraphState();
			const FRPGBossState BossState = Boss->BossAIComponent->GetBossState();
			if (!bLiveBossAITelegraph && Telegraph.bActive)
			{
				bLiveBossAITelegraph = true;
				LiveBossAITelegraphPhase = BossState.PhaseIndex;
				UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_LIVE_AI_TELEGRAPH Sequence=%d"),
					Telegraph.Sequence);
			}
			if (bLiveBossAITelegraph && !bLiveBossAITelegraphFinished && !Telegraph.bActive
				&& BossState.EncounterState == ERPGBossEncounterState::InCombat
				&& BossState.PhaseIndex == LiveBossAITelegraphPhase)
			{
				bLiveBossAITelegraphFinished = true;
				UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_LIVE_AI_ATTACK_FINISHED SameCombatPhase=1"));
			}
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				const APlayerController* PC = It->Get();
				const ARPGPlayerState* State = PC ? PC->GetPlayerState<ARPGPlayerState>() : nullptr;
				const ARPGPlayer* Player = PC ? Cast<ARPGPlayer>(PC->GetPawn()) : nullptr;
				const float* Before = State ? LiveBossAIPlayerHealthBefore.Find(State->GetBackendCharacterId()) : nullptr;
				if (!Before || !Player) { Fail(TEXT("LiveBossAIPartyChanged")); return; }
				const float PlayerHealth = Player->GetAbilitySystemComponent()->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
				if (!bLiveBossAIDamage && FMath::IsFinite(PlayerHealth) && PlayerHealth < *Before)
				{
					bLiveBossAIDamage = true;
					UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_LIVE_AI_DAMAGE PlayerHealthBefore=%g PlayerHealthAfter=%g"),
						*Before, PlayerHealth);
				}
			}
		}
		const float Health = Boss->GetAbilitySystemComponent()->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
		bNaturalSawDamage |= FMath::IsFinite(Health) && Health < NaturalBossHealthBefore;
		if (Battle->GetBattleSnapshot().BattleState == EBossBattleState::Defeated)
		{ Fail(TEXT("NaturalAttackBattleDefeated")); return; }
		if (Battle->GetBattleSnapshot().BattleState == EBossBattleState::Victory)
		{
			if (!bNaturalSawDamage || Boss->BossAIComponent->GetBossState().EncounterState != ERPGBossEncounterState::Dead)
			{ Fail(TEXT("NaturalAttackVictoryWithoutDamage")); return; }
			if (bLiveBossAI && !bLiveBossAITelegraphFinished)
			{ Fail(TEXT("LiveBossAINoCompletedAttack")); return; }
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_NATURAL_VICTORY AuthoredPlayerInput=1 BossDead=1 Party=%d NoManualDamage=1"), ExpectedPlayers);
			if (bLiveBossAI)
				UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_E2E_LIVE_AI_VICTORY Telegraph=1 AttackFinished=1 PlayerDamaged=%d BrainRestarted=1"),
					int32(bLiveBossAIDamage));
			SetComponentTickEnabled(false);
		}
	}
}
