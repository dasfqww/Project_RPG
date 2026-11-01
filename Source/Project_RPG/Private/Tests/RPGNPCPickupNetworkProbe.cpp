#include "Tests/RPGNPCPickupNetworkProbe.h"

#if !UE_BUILD_SHIPPING
#include "Tests/RPGSkillDamageSecurityTestFixture.h"
#include "AIController.h"
#include "Ability/NPC/RPGNPCDropItemAbility.h"
#include "Attribute/RPGAttributeSet.h"
#include "BrainComponent.h"
#include "Character/RPGNonPlayerCharacter.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/RPGInventoryProjectionComponent.h"
#include "Component/RPGItemCommandComponent.h"
#include "Component/RPGPickupGrantComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DataTable/DropItemData.h"
#include "Engine/NetDriver.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FunctionLibrary/RPGSecurityBlueprintLibrary.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Item/Definition/RPGItemDefinition.h"
#include "Item/Backend/RPGItemBackendSubsystem.h"
#include "Item/PickUp/RPGPickUpBase.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/RPGPlayerState.h"
#include "UObject/UnrealType.h"

namespace
{
const FVector FixturePosition(20000, 20000, 500);
TArray<FRPGInventoryProjectionEntry> InventoryEntries(const URPGInventoryProjectionComponent* Projection)
{
	TArray<FRPGInventoryProjectionEntry> Entries = Projection->GetProjectedItems();
	// Character creation now restores equipment too. Preserve it; only usable
	// bag entries belong to this pickup fixture's empty/count/owner assertions.
	Entries.RemoveAll([](const FRPGInventoryProjectionEntry& Entry)
	{
		return Entry.GetContainerType() != ERPGItemContainerType::Inventory;
	});
	return Entries;
}
uint64 NetId(const UObject* Object)
{
	const UNetDriver* Driver = Object ? Object->GetWorld()->GetNetDriver() : nullptr;
	const UReplicationSystem* System = Driver ? Driver->GetReplicationSystem() : nullptr;
	const UObjectReplicationBridge* Bridge = System ? System->GetReplicationBridge() : nullptr;
	return Bridge ? Bridge->GetReplicatedRefHandle(Object).GetId() : 0;
}
void PauseNPC(ARPGNonPlayerCharacter& Target)
{
	Target.GetCharacterMovement()->DisableMovement();
	Target.GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	if (AAIController* AI = Cast<AAIController>(Target.GetController()); AI && AI->BrainComponent)
		AI->BrainComponent->StopLogic(TEXT("Explicit NPC pickup integration fixture"));
}
}

void FRPGNPCPickupNetworkProbe::Fail(const TCHAR* Reason)
{
	if (bFinished) return;
	bFinished = true;
	UE_LOG(LogTemp, Error, TEXT("RPG_NPC_PICKUP FAIL Reason=%s Stage=%d"), Reason, Stage);
	Stop();
}
bool FRPGNPCPickupNetworkProbe::ConfigureParty()
{
	if (bConfigured) return true;
	FString Characters;
	if (FParse::Value(FCommandLine::Get(), TEXT("RPGNPCPickupCharacters="), Characters, false))
	{
		Characters.ParseIntoArray(ExpectedCharacters, TEXT(","), false);
		if (ExpectedCharacters.Num() < 2 || ExpectedCharacters.Num() > 4)
		{ Fail(TEXT("InvalidFixtureParty")); return false; }
		TSet<FGuid> Unique;
		for (FString& Character : ExpectedCharacters)
		{
			FGuid Id;
			if (!FGuid::Parse(Character, Id) || !Id.IsValid() || Unique.Contains(Id))
			{ Fail(TEXT("InvalidFixturePartyIdentity")); return false; }
			Unique.Add(Id);
			Character = Id.ToString(EGuidFormats::DigitsWithHyphensLower);
		}
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGNPCPickupLostAckE2E")) && ExpectedCharacters.Num() != 2)
	{ Fail(TEXT("InvalidRecoveryFixtureParty")); return false; }
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGNPCPickupRestartRecoveryE2E")))
	{
		FString Owner, Item, Request;
		if (ExpectedCharacters.Num() != 2 ||
			!FParse::Value(FCommandLine::Get(), TEXT("RPGNPCPickupRestartOwner="), Owner) ||
			!FParse::Value(FCommandLine::Get(), TEXT("RPGNPCPickupRestartItem="), Item) ||
			!FParse::Value(FCommandLine::Get(), TEXT("RPGNPCPickupRestartRequest="), Request) ||
			!FGuid::Parse(Owner, RestartOwnerId) || !RestartOwnerId.IsValid() ||
			!FGuid::Parse(Item, RestartItemId) || !RestartItemId.IsValid() ||
			!FGuid::Parse(Request, RestartRequestId) || !RestartRequestId.IsValid() ||
			!ExpectedCharacters.Contains(RestartOwnerId.ToString(EGuidFormats::DigitsWithHyphensLower)))
		{ Fail(TEXT("InvalidRestartRecoveryFixture")); return false; }
	}
	bConfigured = true;
	return true;
}
bool FRPGNPCPickupNetworkProbe::GatherParty(APlayerController& Controller, TArray<APlayerController*>& Party)
{
	if (ExpectedCharacters.IsEmpty()) { Party.Add(&Controller); return true; }
	const auto* LeaderState = Controller.GetPlayerState<ARPGPlayerState>();
	TSet<FString> Seen;
	for (FConstPlayerControllerIterator It = Controller.GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		auto* Member = It->Get();
		const auto* State = Member ? Member->GetPlayerState<ARPGPlayerState>() : nullptr;
		if (!State || !State->HasAuthenticatedCharacter()) continue;
		const FString Character = State->GetBackendCharacterId().ToLower();
		if (!ExpectedCharacters.Contains(Character) || Seen.Contains(Character) ||
			State->GetBackendDungeonSessionId() != LeaderState->GetBackendDungeonSessionId())
		{ Fail(TEXT("UnexpectedFixturePartyIdentity")); return false; }
		Seen.Add(Character);
		auto* Projection = Member->FindComponentByClass<URPGInventoryProjectionComponent>();
		if (!Member->GetPawn() || !Projection || Projection->GetLoadState() != ERPGInventoryProjectionLoadState::Ready) return false;
		Party.Add(Member);
	}
	return Party.Num() == ExpectedCharacters.Num();
}
void FRPGNPCPickupNetworkProbe::Tick(APlayerController& Controller)
{
	if (bFinished || !FParse::Param(FCommandLine::Get(), TEXT("RPGNPCDropPickupE2E"))) return;
	if (!FParse::Param(FCommandLine::Get(), TEXT("nosteam"))) { Fail(TEXT("UnsafeFixture")); return; }
	if (!ConfigureParty()) return;
	ObservedController = &Controller;
	const auto* State = Controller.GetPlayerState<ARPGPlayerState>();
	// Only the first authenticated fixture member owns the NPC/drop observer.
	// Other controllers must not spawn an independent NPC or time out a passive probe.
	if (Controller.GetNetMode() == NM_DedicatedServer && !ExpectedCharacters.IsEmpty() &&
		(!State || State->GetBackendCharacterId().ToLower() != ExpectedCharacters[0])) return;
	if (Deadline == 0.0)
	{
		int32 Timeout = 180;
		FParse::Value(FCommandLine::Get(), TEXT("RPGNPCPickupTimeoutSeconds="), Timeout);
		if (Timeout < 60 || Timeout > 600) { Fail(TEXT("InvalidFixtureTimeout")); return; }
		Deadline = FPlatformTime::Seconds() + Timeout;
	}
	if (FPlatformTime::Seconds() > Deadline) { Fail(TEXT("Timeout")); return; }
	const auto* Driver = Controller.GetWorld()->GetNetDriver();
	if (!Driver || !Driver->IsUsingIrisReplication()) return;
	const auto* Projection = Controller.FindComponentByClass<URPGInventoryProjectionComponent>();
	if (!State || !State->HasAuthenticatedCharacter() || !Controller.GetPawn() || !Projection ||
		Projection->GetLoadState() != ERPGInventoryProjectionLoadState::Ready) return;
	if (!ExpectedCharacters.IsEmpty() && !ExpectedCharacters.Contains(State->GetBackendCharacterId().ToLower()))
	{ Fail(TEXT("UnexpectedLocalFixtureIdentity")); return; }
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGNPCPickupRestartRecoveryE2E"))) TickRestartRecovery(Controller);
	else if (Controller.GetNetMode() == NM_DedicatedServer) TickServer(Controller);
	else if (Controller.GetNetMode() == NM_Client && Controller.IsLocalController()) TickClient(Controller);
}
void FRPGNPCPickupNetworkProbe::TickRestartRecovery(APlayerController& Controller)
{
	// Observation only. No spawn, interaction, identity injection or grant replay.
	const auto* Definition = LoadObject<URPGItemDefinition>(nullptr,
		TEXT("/Game/Blueprints/GameData/Item/Weapons/DA_Item_GreatSword.DA_Item_GreatSword"));
	if (!Definition) { Fail(TEXT("MissingRestartDefinition")); return; }
	TArray<APlayerController*> Party;
	if (Controller.GetNetMode() == NM_DedicatedServer)
	{
		if (!GatherParty(Controller, Party)) return;
	}
	else if (Controller.GetNetMode() == NM_Client && Controller.IsLocalController()) Party.Add(&Controller);
	else return;
	int32 Total = 0;
	for (const auto* Member : Party)
	{
		const auto* State = Member->GetPlayerState<ARPGPlayerState>();
		const auto Entries = InventoryEntries(Member->FindComponentByClass<URPGInventoryProjectionComponent>());
		const bool bWinner = State->GetBackendCharacterId() == RestartOwnerId.ToString(EGuidFormats::DigitsWithHyphensLower);
		if (bWinner && Entries.IsEmpty() && Controller.GetNetMode() == NM_Client) return;
		if ((!bWinner && !Entries.IsEmpty()) || (bWinner && Entries.Num() != 1))
		{ Fail(TEXT("RestartOwnerProjectionMismatch")); return; }
		if (bWinner && (Entries[0].GetItemId() != RestartItemId ||
			Entries[0].GetDefinitionId() != Definition->GetPrimaryAssetId() ||
			Entries[0].GetQuantity() != 1 || Entries[0].GetRevision() != 1 ||
			Entries[0].GetContainerType() != ERPGItemContainerType::Inventory || Entries[0].GetSlotIndex() != 0))
		{ Fail(TEXT("RestartItemProjectionMismatch")); return; }
		Total += Entries.Num();
	}
	// Actor restoration is deliberately out of scope. This recovery fixture must
	// not spawn a second encounter/drop at the original fixture position.
	for (TActorIterator<ARPGPickUpBase> It(Controller.GetWorld()); It; ++It)
		if (It->GetActorLocation().Equals(FixturePosition, 250.f)) { Fail(TEXT("UnexpectedRestartFixturePickup")); return; }
	const FString Owner = RestartOwnerId.ToString(EGuidFormats::DigitsWithHyphensLower);
	const FString Item = RestartItemId.ToString(EGuidFormats::DigitsWithHyphensLower);
	const FString Request = RestartRequestId.ToString(EGuidFormats::DigitsWithHyphensLower);
	if (Controller.GetNetMode() == NM_DedicatedServer)
	{
		if (Total != 1) { Fail(TEXT("RestartMultipleGrants")); return; }
		UE_LOG(LogTemp, Display, TEXT("RPG_NPC_PICKUP SERVER_RESTART_RECOVERED Owner=%s Request=%s Item=%s Quantity=1 Revision=1 Party=2 TotalItems=1 NewPickup=0"),
			*Owner, *Request, *Item);
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("RPG_NPC_PICKUP CLIENT_RESTART_RECOVERED Character=%s Owner=%s Request=%s Item=%s TotalItems=%d Quantity=%d Revision=%d NewPickup=0"),
			*Controller.GetPlayerState<ARPGPlayerState>()->GetBackendCharacterId(), *Owner, *Request, *Item, Total, Total, Total);
	}
	bFinished = true;
}
void FRPGNPCPickupNetworkProbe::TickServer(APlayerController& Controller)
{
	UWorld& World = *Controller.GetWorld();
	if (Stage == 0)
	{
		TArray<APlayerController*> Party;
		if (!GatherParty(Controller, Party)) return;
		for (auto* Member : Party)
			if (!InventoryEntries(Member->FindComponentByClass<URPGInventoryProjectionComponent>()).IsEmpty())
			{ Fail(TEXT("ExpectedEmptyInventory")); return; }
		if (FParse::Param(FCommandLine::Get(), TEXT("RPGNPCPickupRevisionConflictE2E")))
			UE_LOG(LogTemp, Display, TEXT("RPG_NPC_PICKUP_FIXTURE_INVENTORY_ONLY Inventory=1 EquipmentUntouched=1"));
		UClass* NPCClass = LoadClass<ARPGNonPlayerCharacter>(nullptr,
			TEXT("/Game/Blueprints/Character/NPC/DummyNPC/BP_Dummy_Gdn.BP_Dummy_Gdn_C"));
		const auto* Definition = LoadObject<URPGItemDefinition>(nullptr,
			TEXT("/Game/Blueprints/GameData/Item/Weapons/DA_Item_GreatSword.DA_Item_GreatSword"));
		if (!NPCClass || !Definition) { Fail(TEXT("MissingSavedContent")); return; }
		DefinitionId = Definition->GetPrimaryAssetId();
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		NPC = World.SpawnActor<ARPGNonPlayerCharacter>(NPCClass, FixturePosition, FRotator::ZeroRotator, Params);
		if (!NPC.IsValid()) { Fail(TEXT("NPCSpawn")); return; }
		NPC->bAlwaysRelevant = true;
		NPC->SetNetCullDistanceSquared(1.e10f);
		PauseNPC(*NPC);
		// Server-side positioning is a fixture, not evidence of player movement/input.
		for (int32 Index = 0; Index < Party.Num(); ++Index)
		{
			Party[Index]->GetPawn()->SetActorLocation(FixturePosition - FVector(150, Index * 30, 0));
			if (auto* Character = Cast<ACharacter>(Party[Index]->GetPawn())) Character->GetCharacterMovement()->DisableMovement();
		}
		Stage = 1;
		return;
	}
	if (Stage == 1)
	{
		if (!NPC.IsValid() || !NPC->IsNPCStartUpDataReady() || !(NPCNetId = NetId(NPC.Get()))) return;
		PauseNPC(*NPC);
		auto* ASC = NPC->GetRPGAbilitySystemComponent();
		UClass* DropClass = LoadClass<URPGNPCDropItemAbility>(nullptr,
			TEXT("/Game/Blueprints/Character/NPC/Gruntling/Guardian/BP_Guardian_DropItemAbility.BP_Guardian_DropItemAbility_C"));
		UClass* DeathClass = LoadClass<UGameplayAbility>(nullptr,
			TEXT("/Game/Blueprints/Character/NPC/Gruntling/GameplayAbility/GA_Guardian_Death.GA_Guardian_Death_C"));
		UClass* PickupClass = LoadClass<ARPGPickUpBase>(nullptr,
			TEXT("/Game/Blueprints/Item/Equipment/BP_Pickup_GreatSword.BP_Pickup_GreatSword_C"));
		UClass* DamageClass = LoadClass<UGameplayEffect>(nullptr,
			TEXT("/Game/Blueprints/Character/Shared/GameplayEffect/GE_Shared_ApplyDamage.GE_Shared_ApplyDamage_C"));
		if (!DropClass || !DeathClass || !PickupClass || !DamageClass ||
			!ASC->FindAbilitySpecFromClass(DropClass) || !ASC->FindAbilitySpecFromClass(DeathClass))
		{ Fail(TEXT("MissingGrantedSavedAbilities")); return; }
		ObservedASC = ASC;
		ObservedWorld = &World;
		const auto Counter = Observation;
		const TWeakObjectPtr<ARPGNonPlayerCharacter> Target = NPC;
		ActivatedHandle = ASC->AbilityActivatedCallbacks.AddLambda([Counter, DropClass, DeathClass, PickupClass](UGameplayAbility* Ability)
		{
			if (Ability->IsA(DeathClass)) ++Counter->Deaths;
			if (!Ability->IsA(DropClass)) return;
			++Counter->Drops;
			// GAS notifies activation before ActivateAbility. Configure only this
			// InstancedPerExecution instance; never the saved table or class CDO.
			if (Ability->HasAnyFlags(RF_ClassDefaultObject)) return;
			auto* Table = NewObject<UDataTable>(Ability);
			Table->RowStruct = FItemDropTable::StaticStruct();
			FItemDropTable Row;
			FDropItem& Drop = Row.DropItemList.AddDefaulted_GetRef();
			Drop.ItemClass = PickupClass; Drop.DropChance = 1.f; Drop.DropQuantity = 1;
			Table->AddRow(TEXT("NPCPickupFixture"), Row);
			FindFProperty<FObjectPropertyBase>(Ability->GetClass(), TEXT("ItemDropTable"))->SetObjectPropertyValue_InContainer(Ability, Table);
			FindFProperty<FNameProperty>(Ability->GetClass(), TEXT("DropRowName"))->SetPropertyValue_InContainer(Ability, TEXT("NPCPickupFixture"));
		});
		EndedHandle = ASC->OnAbilityEnded.AddLambda([Counter, DropClass, Target](const FAbilityEndedData& Data)
		{
			if (!Data.AbilityThatEnded || !Data.AbilityThatEnded->IsA(DropClass)) return;
			++Counter->Ends;
			Counter->Claimed = !Data.bWasCancelled && Target.IsValid() && Target->IsNPCStartUpDataReady() &&
				Target->GetRPGAttributeSet()->GetCurrentHealth() == 0.f && !Target->CanProcessDeathItemDrop();
		});
		SpawnHandle = World.AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([this, PickupClass](AActor* Actor)
		{
			if (Actor->GetClass() != PickupClass || !Actor->GetActorLocation().Equals(FixturePosition, 250.f)) return;
			++SpawnCount;
			Pickup = CastChecked<ARPGPickUpBase>(Actor);
			Pickup->bAlwaysRelevant = true;
			if (auto* Mesh = Cast<UStaticMeshComponent>(Pickup->GetRootComponent())) Mesh->SetSimulatePhysics(false);
			Pickup->SetActorLocation(FixturePosition);
		}));
		auto* Source = World.SpawnActor<ARPGSkillDamageSecurityTestPawn>();
		auto* AI = World.SpawnActor<AAIController>();
		if (!Source || !AI) { Fail(TEXT("SourceSpawn")); return; }
		AI->SetGenericTeamId(FGenericTeamId(0)); AI->Possess(Source);
		Source->SetActorLocation(FixturePosition - FVector(300, 0, 0));
		const float Health = NPC->GetRPGAttributeSet()->GetCurrentHealth();
		const float Defense = NPC->GetRPGAttributeSet()->GetDefense();
		if (!FMath::IsFinite(Health) || Health <= 0.f || !FMath::IsFinite(Defense) || Defense <= 0.f)
		{ Fail(TEXT("InvalidStartup")); return; }
		const float Base = FMath::CeilToFloat(Health * Defense) + Defense;
		FRPGSkillSecurityProfile Profile;
		Profile.MaximumDamagePerHit = FMath::Max(100.f, Base);
		Profile.MaximumFinalDamagePerHit = FMath::Max(100.f, Health + 2.f);
		Profile.MaximumServerHitDistance = 1000.f;
		FActiveGameplayEffectHandle Handle; FText Error;
		Stage = 2;
		if (!URPGSecurityBlueprintLibrary::ApplyAuthorizedServerDamage(Source,
			FHitResult(NPC.Get(), nullptr, FixturePosition, FVector::ForwardVector), DamageClass, Base,
			FGameplayTag(), Profile, Handle, Error) || Observation->Deaths != 1 || NPC->GetRPGAttributeSet()->GetCurrentHealth() != 0.f)
		{ Fail(TEXT("ActualDeathFailed")); return; }
		UE_LOG(LogTemp, Display, TEXT("RPG_NPC_PICKUP SERVER_DEATH NPC=%llu Deaths=1 Authenticated=1 NativeSource=1"), NPCNetId);
		return;
	}
	if (Stage == 2)
	{
		if (Observation->Drops == 0) return;
		if (Observation->Drops != 1 || Observation->Ends != 1 || !Observation->Claimed || SpawnCount != 1 || !Pickup.IsValid())
		{ Fail(TEXT("ActualDropFailed")); return; }
		if (!(PickupNetId = NetId(Pickup.Get()))) return;
		if (FParse::Param(FCommandLine::Get(), TEXT("RPGNPCPickupPreparedE2E")))
		{
			if (!Pickup->GetPickupDropId().IsValid()) { Fail(TEXT("MissingServerDropIdentity")); return; }
			UE_LOG(LogTemp, Display, TEXT("RPG_NPC_PICKUP SERVER_DROP NPC=%llu Pickup=%llu Spawned=1 Activated=1 Ended=1 ClaimConsumed=1 TransientRow=1 DropId=%s"),
				NPCNetId, PickupNetId, *Pickup->GetPickupDropId().ToString(EGuidFormats::DigitsWithHyphensLower));
		}
		else UE_LOG(LogTemp, Display, TEXT("RPG_NPC_PICKUP SERVER_DROP NPC=%llu Pickup=%llu Spawned=1 Activated=1 Ended=1 ClaimConsumed=1 TransientRow=1"), NPCNetId, PickupNetId);
		Stage = 3;
		return;
	}
	if (Stage == 3)
	{
		TArray<APlayerController*> Party;
		if (!GatherParty(Controller, Party)) return;
		int32 Total = 0;
		FRPGInventoryProjectionEntry Granted;
		FString Owner;
		for (auto* Member : Party)
		{
			const auto Entries = InventoryEntries(Member->FindComponentByClass<URPGInventoryProjectionComponent>());
			Total += Entries.Num();
			if (Entries.Num() == 1)
			{
				Granted = Entries[0];
				Owner = Member->GetPlayerState<ARPGPlayerState>()->GetBackendCharacterId();
			}
		}
		if (Total > 1) { Fail(TEXT("MultiplePartyGrants")); return; }
		if (!bObservedUnknownAck && FParse::Param(FCommandLine::Get(), TEXT("RPGNPCPickupLostAckE2E")))
		{
			FString CommittedFile;
			FParse::Value(FCommandLine::Get(), TEXT("RPGNPCPickupCommittedFile="), CommittedFile);
			if (!CommittedFile.IsEmpty() && IFileManager::Get().FileExists(*CommittedFile))
			{
				const auto* Grant = Pickup.IsValid() ? Pickup->FindComponentByClass<URPGPickupGrantComponent>() : nullptr;
				const auto* Backend = World.GetGameInstance()->GetSubsystem<URPGItemBackendSubsystem>();
				const auto* Attempts = Backend ? FindFProperty<FIntProperty>(Backend->GetClass(), TEXT("MaximumAttempts")) : nullptr;
				if (Total != 0 || !Grant || !Grant->IsPending() || Pickup->IsAvailableForPickup() || !Attempts)
				{ Fail(TEXT("UnknownAckReleasedPickup")); return; }
				bObservedUnknownAck = true;
				UE_LOG(LogTemp, Display, TEXT("RPG_NPC_PICKUP SERVER_UNCERTAIN Pickup=%llu Pending=1 Available=0 Retired=0 TotalItems=0 GatewayAttempts=%d"),
					PickupNetId, Attempts->GetPropertyValue_InContainer(Backend));
			}
		}
		if (Total != 1 || Granted.GetDefinitionId() != DefinitionId || Granted.GetQuantity() != 1 ||
			Granted.GetRevision() != 1 || Pickup.IsValid()) return;
		if (ExpectedCharacters.IsEmpty())
		{
			UE_LOG(LogTemp, Display, TEXT("RPG_NPC_PICKUP SERVER_COMMIT Pickup=%llu Item=%s Definition=%s Quantity=1 Revision=1 Retired=1"),
				PickupNetId, *Granted.GetItemId().ToString(EGuidFormats::DigitsWithHyphensLower), *DefinitionId.PrimaryAssetName.ToString());
		}
		else
		{
			UE_LOG(LogTemp, Display, TEXT("RPG_NPC_PICKUP SERVER_COMMIT Pickup=%llu Item=%s Definition=%s Quantity=1 Revision=1 Retired=1 Owner=%s Party=%d TotalItems=1"),
				PickupNetId, *Granted.GetItemId().ToString(EGuidFormats::DigitsWithHyphensLower), *DefinitionId.PrimaryAssetName.ToString(), *Owner, Party.Num());
		}
		bFinished = true;
		Stop();
	}
}
void FRPGNPCPickupNetworkProbe::TickClient(APlayerController& Controller)
{
	auto* Projection = Controller.FindComponentByClass<URPGInventoryProjectionComponent>();
	if (Stage == 0)
	{
		// The harness opens this barrier only after the actual server drop row
		// was observed, avoiding receipt/retirement racing its observation tick.
		FString ReadyFile;
		FParse::Value(FCommandLine::Get(), TEXT("RPGNPCPickupReadyFile="), ReadyFile);
		if (ReadyFile.IsEmpty() || !IFileManager::Get().FileExists(*ReadyFile)) return;
		if (!InventoryEntries(Projection).IsEmpty()) { Fail(TEXT("ExpectedEmptyClientInventory")); return; }
		ARPGPickUpBase* Found = nullptr;
		for (TActorIterator<ARPGPickUpBase> It(Controller.GetWorld()); It; ++It)
		{
			if (It->GetClass()->GetName() != TEXT("BP_Pickup_GreatSword_C") || !It->GetActorLocation().Equals(FixturePosition, 1.f)) continue;
			if (Found) { Fail(TEXT("AmbiguousReplicatedPickup")); return; }
			Found = *It;
		}
		if (!Found || !Found->IsAvailableForPickup() || !NetId(Found) ||
			FVector::DistSquared(Controller.GetPawn()->GetActorLocation(), Found->GetActorLocation()) > FMath::Square(200.f)) return;
		const auto* Definition = LoadObject<URPGItemDefinition>(nullptr,
			TEXT("/Game/Blueprints/GameData/Item/Weapons/DA_Item_GreatSword.DA_Item_GreatSword"));
		if (!Definition) { Fail(TEXT("MissingClientDefinition")); return; }
		DefinitionId = Definition->GetPrimaryAssetId();
		Pickup = Found; PickupNetId = NetId(Found);
		Stage = ExpectedCharacters.IsEmpty() ? 1 : 4;
		if (Stage == 4)
		{
			UE_LOG(LogTemp, Display, TEXT("RPG_NPC_PICKUP CLIENT_READY Pickup=%llu Character=%s Available=1 Empty=1 InRange=1"),
				PickupNetId, *Controller.GetPlayerState<ARPGPlayerState>()->GetBackendCharacterId());
			return;
		}
		UE_LOG(LogTemp, Display, TEXT("RPG_NPC_PICKUP CLIENT_SEND Pickup=%llu Requests=2 ExistingRPC=1"), PickupNetId);
		// Two queued real requests. Identity/quantity/item ID are server-derived.
		static_cast<IInteractionInterface*>(Found)->Interact(&Controller);
		static_cast<IInteractionInterface*>(Found)->Interact(&Controller);
		return;
	}
	if (Stage == 4)
	{
		FString SendFile;
		FParse::Value(FCommandLine::Get(), TEXT("RPGNPCPickupSendFile="), SendFile);
		if (SendFile.IsEmpty() || !IFileManager::Get().FileExists(*SendFile)) return;
		if (!Pickup.IsValid() || !Pickup->IsAvailableForPickup() || !InventoryEntries(Projection).IsEmpty())
		{ Fail(TEXT("PickupRetiredBeforeContenderSend")); return; }
		Stage = 1;
		UE_LOG(LogTemp, Display, TEXT("RPG_NPC_PICKUP CLIENT_SEND Pickup=%llu Requests=2 ExistingRPC=1 Character=%s"),
			PickupNetId, *Controller.GetPlayerState<ARPGPlayerState>()->GetBackendCharacterId());
		static_cast<IInteractionInterface*>(Pickup.Get())->Interact(&Controller);
		static_cast<IInteractionInterface*>(Pickup.Get())->Interact(&Controller);
		return;
	}
	if (Stage == 1 && !ExpectedCharacters.IsEmpty() && Results == 2 && !Pickup.IsValid())
	{
		const auto Entries = InventoryEntries(Projection);
		const bool Won = SuccessRequestId.IsValid();
		if (Won && (Entries.Num() != 1 || Entries[0].GetDefinitionId() != DefinitionId ||
			Entries[0].GetQuantity() != 1 || Entries[0].GetRevision() != 1)) return;
		if (!Won && !Entries.IsEmpty()) { Fail(TEXT("LoserProjectionLeaked")); return; }
		UE_LOG(LogTemp, Display, TEXT("RPG_NPC_PICKUP CLIENT_PARTY_COMPLETE Pickup=%llu Character=%s Success=%d Rejected=%d Request=%s Reject1=%s Reject2=%s Item=%s Quantity=%d Revision=%d Retired=1"),
			PickupNetId, *Controller.GetPlayerState<ARPGPlayerState>()->GetBackendCharacterId(), Won ? 1 : 0, RejectedRequests.Num(),
			*SuccessRequestId.ToString(EGuidFormats::DigitsWithHyphensLower),
			*RejectedRequests[0].ToString(EGuidFormats::DigitsWithHyphensLower),
			*(Won ? FGuid() : RejectedRequests[1]).ToString(EGuidFormats::DigitsWithHyphensLower),
			*(Won ? Entries[0].GetItemId() : FGuid()).ToString(EGuidFormats::DigitsWithHyphensLower), Won ? 1 : 0, Won ? 1 : 0);
		bFinished = true;
		return;
	}
	if (Stage == 1 && Results == 2 && SuccessRequestId.IsValid() && RejectedRequestId.IsValid() && !Pickup.IsValid())
	{
		const auto Entries = InventoryEntries(Projection);
		if (Entries.Num() != 1 || Entries[0].GetDefinitionId() != DefinitionId || Entries[0].GetQuantity() != 1 || Entries[0].GetRevision() != 1) return;
		UE_LOG(LogTemp, Display, TEXT("RPG_NPC_PICKUP CLIENT_COMPLETE Pickup=%llu Request=%s Rejected=%s Item=%s Definition=%s Quantity=1 Revision=1 Retired=1"),
			PickupNetId, *SuccessRequestId.ToString(EGuidFormats::DigitsWithHyphensLower),
			*RejectedRequestId.ToString(EGuidFormats::DigitsWithHyphensLower), *Entries[0].GetItemId().ToString(EGuidFormats::DigitsWithHyphensLower), *DefinitionId.PrimaryAssetName.ToString());
		bFinished = true;
	}
}
void FRPGNPCPickupNetworkProbe::HandleResult(const FRPGItemCommandClientResult& Result)
{
	if (bFinished || Stage != 1 || Result.Operation != TEXT("Item.Pickup")) return;
	if (!Result.RequestId.IsValid() || ++Results > 2) { Fail(TEXT("UnexpectedCommandCount")); return; }
	const bool LostAckFixture = FParse::Param(FCommandLine::Get(), TEXT("RPGNPCPickupLostAckE2E"));
	if ((Result.Result == ERPGItemCommandResultCode::Succeeded ||
		(LostAckFixture && Result.Result == ERPGItemCommandResultCode::AlreadyApplied)) && !SuccessRequestId.IsValid())
	{
		SuccessRequestId = Result.RequestId;
		if (LostAckFixture)
		{
			if (Result.Result != ERPGItemCommandResultCode::AlreadyApplied) { Fail(TEXT("RecoveryWasNotIdempotent")); return; }
			UE_LOG(LogTemp, Display, TEXT("RPG_NPC_PICKUP CLIENT_RECOVERED Pickup=%llu Request=%s Code=AlreadyApplied"),
				PickupNetId, *Result.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower));
		}
	}
	else if ((Result.Result == ERPGItemCommandResultCode::InvalidRequest || Result.Result == ERPGItemCommandResultCode::Busy) &&
		(!ExpectedCharacters.IsEmpty() || !RejectedRequestId.IsValid()))
	{
		if (RejectedRequests.Contains(Result.RequestId)) { Fail(TEXT("DuplicateRejectedIdentity")); return; }
		RejectedRequests.Add(Result.RequestId);
		if (!RejectedRequestId.IsValid()) RejectedRequestId = Result.RequestId;
		if (LostAckFixture)
		{
			const auto* Controller = ObservedController.Get();
			const auto* Projection = Controller ? Controller->FindComponentByClass<URPGInventoryProjectionComponent>() : nullptr;
			const auto* State = Controller ? Controller->GetPlayerState<ARPGPlayerState>() : nullptr;
			if (!Pickup.IsValid() || !Projection || !InventoryEntries(Projection).IsEmpty() || !State)
			{ Fail(TEXT("RejectionNotObservedWhilePickupAlive")); return; }
			UE_LOG(LogTemp, Display, TEXT("RPG_NPC_PICKUP CLIENT_REJECTED_PENDING Pickup=%llu Character=%s Request=%s Retired=0 TotalItems=0"),
				PickupNetId, *State->GetBackendCharacterId(), *Result.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower));
		}
	}
	else { Fail(TEXT("UnexpectedCommandResult")); return; }
	if (SuccessRequestId.IsValid() && RejectedRequests.Contains(SuccessRequestId)) Fail(TEXT("BorrowedRequestIdentity"));
}
#else
void FRPGNPCPickupNetworkProbe::Tick(APlayerController&) {}
void FRPGNPCPickupNetworkProbe::HandleResult(const FRPGItemCommandClientResult&) {}
#endif

FRPGNPCPickupNetworkProbe::~FRPGNPCPickupNetworkProbe() { Stop(); }
void FRPGNPCPickupNetworkProbe::Stop()
{
#if !UE_BUILD_SHIPPING
	if (ObservedASC.IsValid())
	{
		ObservedASC->AbilityActivatedCallbacks.Remove(ActivatedHandle);
		ObservedASC->OnAbilityEnded.Remove(EndedHandle);
	}
	if (ObservedWorld.IsValid()) ObservedWorld->RemoveOnActorSpawnedHandler(SpawnHandle);
	ObservedASC.Reset(); ObservedWorld.Reset();
#endif
}
