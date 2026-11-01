#include "GameFeatureAction_RPGOptionsUI.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "InputAction.h"
#if WITH_EDITORONLY_DATA
#include "AssetRegistry/AssetBundleData.h"
#endif
#include "RPGOptionsMenuComponent.h"
#include "UI/HUD/RPGHUD.h"
#include "UI/Option/RPGGraphicsOptionsRuntime.h"
#include "UI/Option/RPGSoundOptionsRuntime.h"
#include "UObject/StrongObjectPtr.h"

UGameFeatureAction_RPGOptionsUI::FContextData* UGameFeatureAction_RPGOptionsUI::FindActiveContext(
	const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation)
{
	FContextData* Data = ContextData.Find(ChangeContext);
	return Data && Data->Generation == Generation ? Data : nullptr;
}

void UGameFeatureAction_RPGOptionsUI::OnGameFeatureActivating(FGameFeatureActivatingContext& Context)
{
	Super::OnGameFeatureActivating(Context);
	if (!GEngine || ContextData.Contains(Context)) { return; }

	const FGameFeatureStateChangeContext ChangeContext(Context);
	const uint64 Generation = ++NextGeneration;
	FContextData& Data = ContextData.Add(ChangeContext);
	Data.Generation = Generation;
	Data.GameInstanceStarted = FWorldDelegates::OnStartGameInstance.AddWeakLambda(this,
		[this, ChangeContext, Generation](UGameInstance* Instance)
		{
			if (IsValid(Instance) && FindActiveContext(ChangeContext, Generation))
			{
				if (const FWorldContext* WorldContext = Instance->GetWorldContext())
				{
					AddToWorld(*WorldContext, ChangeContext, Generation);
				}
			}
		});
	Data.WorldInitialized = FWorldDelegates::OnPostWorldInitialization.AddWeakLambda(this,
		[this, ChangeContext, Generation](UWorld* World, const UWorld::InitializationValues)
		{
			if (GEngine && FindActiveContext(ChangeContext, Generation))
			{
				if (const FWorldContext* WorldContext = GEngine->GetWorldContextFromWorld(World))
				{
					AddToWorld(*WorldContext, ChangeContext, Generation);
				}
			}
		});
	Data.WorldCleanup = FWorldDelegates::OnWorldCleanup.AddWeakLambda(this,
		[this, ChangeContext, Generation](UWorld* World, bool, bool)
		{
			RemoveWorld(World, ChangeContext, Generation);
		});

	// Widget construction may switch worlds or reactivate this same context.
	// Iterate weak world snapshots and resolve the current engine context each time.
	TArray<TWeakObjectPtr<UWorld>> Worlds;
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		if (UWorld* World = WorldContext.World()) { Worlds.Add(World); }
	}
	for (const TWeakObjectPtr<UWorld>& WeakWorld : Worlds)
	{
		if (!GEngine || !FindActiveContext(ChangeContext, Generation)) { return; }
		UWorld* World = WeakWorld.Get();
		if (!World) { continue; }
		if (const FWorldContext* WorldContext = GEngine->GetWorldContextFromWorld(World))
		{
			AddToWorld(*WorldContext, ChangeContext, Generation);
		}
	}
}

void UGameFeatureAction_RPGOptionsUI::AddToWorld(const FWorldContext& WorldContext,
	const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation)
{
	UWorld* World = WorldContext.World();
	if (!IsValid(World) || !World->IsGameWorld() || World->bIsTearingDown
		|| World->GetNetMode() == NM_DedicatedServer
		|| !ChangeContext.ShouldApplyToWorldContext(WorldContext))
	{
		return;
	}
	FContextData* Active = FindActiveContext(ChangeContext, Generation);
	TStrongObjectPtr<UWorld> KeepWorldAlive(World);
	if (!Active) { return; }
	if (Active->Worlds.Contains(World))
	{
		// World initialization can precede its game instance; retry when that instance starts.
		InitializeOptionsForWorld(World, ChangeContext, Generation);
		return;
	}
	FWorldData& Data = Active->Worlds.Add(World);

	const TWeakObjectPtr<UWorld> WeakWorld(World);
	Data.ActorSpawnedHandle = World->AddOnActorSpawnedHandler(
		FOnActorSpawned::FDelegate::CreateWeakLambda(this,
			[this, WeakWorld, ChangeContext, Generation](AActor* Actor)
			{
				UWorld* OwnerWorld = WeakWorld.Get();
				ARPGHUD* HUD = Cast<ARPGHUD>(Actor);
				if (!HUD || !OwnerWorld || OwnerWorld->bIsTearingDown
					|| HUD->GetWorld() != OwnerWorld) { return; }
				FContextData* Current = FindActiveContext(ChangeContext, Generation);
				FWorldData* WorldData = Current ? Current->Worlds.Find(WeakWorld) : nullptr;
				if (WorldData) { AttachToHUD(HUD, *WorldData); }
			}));

	if (!World->HasBegunPlay())
	{
		Data.WorldBeginPlayHandle = World->OnWorldBeginPlay.AddWeakLambda(this,
			[this, WeakWorld, ChangeContext, Generation]()
			{
				InitializeOptionsForWorld(WeakWorld.Get(), ChangeContext, Generation);
			});
	}
	InitializeOptionsForWorld(World, ChangeContext, Generation);
	// Settings callbacks may deactivate this context or clean up the world.
	Active = FindActiveContext(ChangeContext, Generation);
	if (!Active || !Active->Worlds.Contains(WeakWorld) || !IsValid(World) || World->bIsTearingDown) { return; }

	TArray<TWeakObjectPtr<ARPGHUD>> HUDs;
	for (TActorIterator<ARPGHUD> It(World); It; ++It) { HUDs.Add(*It); }
	for (const TWeakObjectPtr<ARPGHUD>& WeakHUD : HUDs)
	{
		FContextData* Current = FindActiveContext(ChangeContext, Generation);
		FWorldData* WorldData = Current ? Current->Worlds.Find(WeakWorld) : nullptr;
		if (!WorldData || !IsValid(World) || World->bIsTearingDown) { return; }
		ARPGHUD* HUD = WeakHUD.Get();
		if (HUD && HUD->GetWorld() == World) { AttachToHUD(HUD, *WorldData); }
	}
}

void UGameFeatureAction_RPGOptionsUI::InitializeOptionsForWorld(UWorld* World,
	const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation)
{
	FContextData* Active = FindActiveContext(ChangeContext, Generation);
	if (!Active || !IsValid(World) || World->bIsTearingDown || !Active->Worlds.Contains(World)) { return; }
	TStrongObjectPtr<UWorld> KeepWorldAlive(World);
	if (IRPGGraphicsOptionsRuntime* Runtime = IRPGGraphicsOptionsRuntime::Get())
	{
		Runtime->InitializeForWorld(World);
	}
	Active = FindActiveContext(ChangeContext, Generation);
	if (!Active || !IsValid(World) || World->bIsTearingDown || !Active->Worlds.Contains(World)) { return; }
	if (IRPGSoundOptionsRuntime* Runtime = IRPGSoundOptionsRuntime::Get())
	{
		Runtime->InitializeForWorld(World);
	}
}

void UGameFeatureAction_RPGOptionsUI::AttachToHUD(ARPGHUD* HUD, FWorldData& Data)
{
	if (!IsValid(HUD) || HUD->IsActorBeingDestroyed() || Data.HUDs.Contains(HUD)) { return; }
	TStrongObjectPtr<ARPGHUD> KeepHUDAlive(HUD);
	URPGOptionsMenuComponent* Component = HUD->FindComponentByClass<URPGOptionsMenuComponent>();
	if (!IsValid(Component) || Component->IsBeingDestroyed()) { Component = nullptr; }
	const bool bCreated = Component == nullptr;
	if (bCreated)
	{
		Component = NewObject<URPGOptionsMenuComponent>(HUD);
		HUD->AddInstanceComponent(Component);
	}
	if (!IsValid(Component) || Component->IsBeingDestroyed()) { return; }

	TStrongObjectPtr<URPGOptionsMenuComponent> KeepComponentAlive(Component);
	// Capture both the component and the exact shortcut before any lifecycle callback.
	// Each accepted HUD request owns one reference, including shared components.
	UInputAction* ToggleAction = ToggleInputAction.Get();
	FHUDRequest& Request = Data.HUDs.Add(HUD);
	Request.Component = Component;
	Request.ToggleAction = ToggleAction;
	Component->AddFeatureReference(bCreated, ToggleAction);
	if (bCreated && IsValid(Component) && !Component->IsBeingDestroyed() && !Component->IsRegistered())
	{
		Component->RegisterComponent();
	}
}

void UGameFeatureAction_RPGOptionsUI::ReleaseWorld(UWorld* World, FWorldData& Data)
{
	if (World)
	{
		World->RemoveOnActorSpawnedHandler(Data.ActorSpawnedHandle);
		World->OnWorldBeginPlay.Remove(Data.WorldBeginPlayHandle);
	}
	TMap<TWeakObjectPtr<ARPGHUD>, FHUDRequest> Removed = MoveTemp(Data.HUDs);
	for (const auto& Pair : Removed)
	{
		if (URPGOptionsMenuComponent* Component = Pair.Value.Component.Get())
		{
			Component->RemoveFeatureReference(Pair.Value.ToggleAction.Get());
		}
	}
}

void UGameFeatureAction_RPGOptionsUI::RemoveWorld(UWorld* World,
	const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation)
{
	FContextData* Active = FindActiveContext(ChangeContext, Generation);
	FWorldData Removed;
	if (Active && Active->Worlds.RemoveAndCopyValue(World, Removed))
	{
		ReleaseWorld(World, Removed);
	}
}

void UGameFeatureAction_RPGOptionsUI::OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context)
{
	FContextData Removed;
	// Detach this activation before releasing widgets, which may activate another context.
	if (ContextData.RemoveAndCopyValue(Context, Removed))
	{
		FWorldDelegates::OnStartGameInstance.Remove(Removed.GameInstanceStarted);
		FWorldDelegates::OnPostWorldInitialization.Remove(Removed.WorldInitialized);
		FWorldDelegates::OnWorldCleanup.Remove(Removed.WorldCleanup);
		for (auto& Pair : Removed.Worlds) { ReleaseWorld(Pair.Key.Get(), Pair.Value); }
	}
	Super::OnGameFeatureDeactivating(Context);
}

#if WITH_EDITORONLY_DATA
void UGameFeatureAction_RPGOptionsUI::AddAdditionalAssetBundleData(FAssetBundleData& AssetBundleData)
{
	Super::AddAdditionalAssetBundleData(AssetBundleData);
	if (!ToggleInputAction.IsNull())
	{
		AssetBundleData.AddBundleAsset(TEXT("Client"), ToggleInputAction.ToSoftObjectPath().GetAssetPath());
	}
}
#endif
