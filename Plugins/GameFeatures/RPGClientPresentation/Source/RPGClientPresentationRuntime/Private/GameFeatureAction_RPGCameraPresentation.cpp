#include "GameFeatureAction_RPGCameraPresentation.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "RPGClientPresentationComponent.h"
#include "UObject/StrongObjectPtr.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogRPGCameraPresentationAction, Log, All);

UGameFeatureAction_RPGCameraPresentation::FContextData* UGameFeatureAction_RPGCameraPresentation::FindActiveContext(
	const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation)
{
	FContextData* Data = ContextData.Find(ChangeContext);
	return Data && Data->Generation == Generation ? Data : nullptr;
}

UGameFeatureAction_RPGCameraPresentation::FWorldData* UGameFeatureAction_RPGCameraPresentation::FindWorldData(
	UWorld* World, const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation)
{
	FContextData* Data = FindActiveContext(ChangeContext, Generation);
	return Data ? Data->Worlds.Find(World) : nullptr;
}

void UGameFeatureAction_RPGCameraPresentation::OnGameFeatureRegistering()
{
	bUnregistering = false;
	Super::OnGameFeatureRegistering();
}

void UGameFeatureAction_RPGCameraPresentation::OnGameFeatureActivating(FGameFeatureActivatingContext& Context)
{
	Super::OnGameFeatureActivating(Context);
	if (bUnregistering || !GEngine || ContextData.Contains(Context)) { return; }
	if (CameraTargetActorTag.IsNone())
	{
		UE_LOG(LogRPGCameraPresentationAction, Warning,
			TEXT("RPG_CAMERA_PRESENTATION SKIPPED Reason=MissingCameraActorTag Action=%s"), *GetPathName());
		return;
	}
	TStrongObjectPtr<UGameFeatureAction_RPGCameraPresentation> KeepAlive(this);
	const FGameFeatureStateChangeContext ChangeContext(Context);
	const uint64 Generation = ++NextGeneration;
	FContextData& Data = ContextData.Add(ChangeContext);
	Data.Generation = Generation;
	Data.TargetTag = CameraTargetActorTag;
	Data.Priority = CameraPriority;
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
			if (GEngine && IsValid(World) && FindActiveContext(ChangeContext, Generation))
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

	TArray<TWeakObjectPtr<UWorld>> Worlds;
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		if (UWorld* World = WorldContext.World()) { Worlds.Add(World); }
	}
	for (const TWeakObjectPtr<UWorld>& WeakWorld : Worlds)
	{
		if (!GEngine || !FindActiveContext(ChangeContext, Generation)) { return; }
		if (UWorld* World = WeakWorld.Get())
		{
			if (const FWorldContext* WorldContext = GEngine->GetWorldContextFromWorld(World))
			{
				AddToWorld(*WorldContext, ChangeContext, Generation);
			}
		}
	}
}

void UGameFeatureAction_RPGCameraPresentation::AddToWorld(const FWorldContext& WorldContext,
	const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation)
{
	UWorld* World = WorldContext.World();
	if (!IsValid(World) || !World->IsGameWorld() || World->bIsTearingDown
		|| World->GetNetMode() == NM_DedicatedServer || !ChangeContext.ShouldApplyToWorldContext(WorldContext)) { return; }
	FContextData* Active = FindActiveContext(ChangeContext, Generation);
	if (!Active) { return; }
	TStrongObjectPtr<UWorld> KeepWorldAlive(World);
	if (!Active->Worlds.Contains(World))
	{
		FWorldData& Data = Active->Worlds.Add(World);
		const TWeakObjectPtr<UWorld> WeakWorld(World);
		Data.GameStateSetHandle = World->GameStateSetEvent.AddWeakLambda(this,
			[this, WeakWorld, ChangeContext, Generation](AGameStateBase*)
			{
				AttachToGameState(WeakWorld.Get(), ChangeContext, Generation);
			});
		if (!World->HasBegunPlay())
		{
			Data.WorldBeginPlayHandle = World->OnWorldBeginPlay.AddWeakLambda(this,
				[this, WeakWorld, ChangeContext, Generation]()
				{
					AttachToGameState(WeakWorld.Get(), ChangeContext, Generation);
				});
		}
	}
	AttachToGameState(World, ChangeContext, Generation);
}

void UGameFeatureAction_RPGCameraPresentation::AttachToGameState(
	UWorld* World, const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation)
{
	if (!IsValid(World) || World->bIsTearingDown) { return; }
	FWorldData* Data = FindWorldData(World, ChangeContext, Generation);
	if (!Data) { return; }
	AGameStateBase* GameState = World->GetGameState();
	URPGClientPresentationComponent* Existing = Data->Component.Get();
	if (IsValid(Existing) && !Existing->IsBeingDestroyed() && Data->GameState.Get() == GameState) { return; }
	TStrongObjectPtr<UGameFeatureAction_RPGCameraPresentation> KeepAlive(this);
	TStrongObjectPtr<UWorld> KeepWorldAlive(World);
	// Clear ownership before destruction can reenter activation or the GameState-set callback.
	Data->Component.Reset();
	Data->GameState.Reset();
	ReleaseComponent(Existing);
	Data = FindWorldData(World, ChangeContext, Generation);
	if (!Data || Data->Component.IsValid() || World->bIsTearingDown) { return; }
	GameState = World->GetGameState();
	if (!IsValid(GameState) || GameState->IsUnreachable() || GameState->IsActorBeingDestroyed()
		|| GameState->GetWorld() != World) { return; }
	TStrongObjectPtr<AGameStateBase> KeepGameStateAlive(GameState);
	FContextData* Active = FindActiveContext(ChangeContext, Generation);
	if (!Active) { return; }
	const FName TargetTag = Active->TargetTag;
	const int32 Priority = Active->Priority;
	TStrongObjectPtr<URPGClientPresentationComponent> Component(
		NewObject<URPGClientPresentationComponent>(GameState, NAME_None, RF_Transient));
	// Object creation can also notify external listeners; resolve the activation again before publishing ownership.
	Data = FindWorldData(World, ChangeContext, Generation);
	if (!Data || Data->Component.IsValid() || World->bIsTearingDown || World->GetGameState() != GameState
		|| GameState->IsActorBeingDestroyed())
	{
		ReleaseComponent(Component.Get());
		return;
	}
	// Every action/context owns a separate native component; no existing component is borrowed or destroyed.
	Component->bDisableSplitscreen = false;
	Component->CameraTargetActorTag = TargetTag;
	Component->CameraPriority = Priority;
	Data->GameState = GameState;
	Data->Component = Component.Get();
	GameState->AddInstanceComponent(Component.Get());
	Component->RegisterComponent();

	// Registration/BeginPlay may replace the world owner or deactivate this activation.
	Data = FindWorldData(World, ChangeContext, Generation);
	if (!Data || Data->Component.Get() != Component.Get())
	{
		ReleaseComponent(Component.Get());
		return;
	}
	if (World->bIsTearingDown || World->GetGameState() != GameState
		|| GameState->IsActorBeingDestroyed() || !IsValid(Component.Get())
		|| Component->IsBeingDestroyed() || !Component->IsRegistered())
	{
		Data->Component.Reset();
		Data->GameState.Reset();
		ReleaseComponent(Component.Get());
		return;
	}
	UE_LOG(LogRPGCameraPresentationAction, Display,
		TEXT("RPG_CAMERA_PRESENTATION ATTACHED World=%s GameState=%s Tag=%s Priority=%d"),
		*World->GetName(), *GameState->GetName(), *TargetTag.ToString(), Priority);
}

void UGameFeatureAction_RPGCameraPresentation::ReleaseComponent(URPGClientPresentationComponent* Component)
{
	if (!IsValid(Component) || Component->IsUnreachable() || Component->IsBeingDestroyed()
		|| Component->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)) { return; }
	TStrongObjectPtr<URPGClientPresentationComponent> KeepAlive(Component);
	Component->DestroyComponent();
}

void UGameFeatureAction_RPGCameraPresentation::ReleaseWorld(UWorld* World, FWorldData& Data)
{
	if (IsValid(World) && !World->IsUnreachable() && !World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
	{
		World->GameStateSetEvent.Remove(Data.GameStateSetHandle);
		World->OnWorldBeginPlay.Remove(Data.WorldBeginPlayHandle);
	}
	URPGClientPresentationComponent* Component = Data.Component.Get();
	Data.Component.Reset();
	Data.GameState.Reset();
	ReleaseComponent(Component);
}

void UGameFeatureAction_RPGCameraPresentation::ReleaseContext(FContextData& Data)
{
	FWorldDelegates::OnStartGameInstance.Remove(Data.GameInstanceStarted);
	FWorldDelegates::OnPostWorldInitialization.Remove(Data.WorldInitialized);
	FWorldDelegates::OnWorldCleanup.Remove(Data.WorldCleanup);
	auto Worlds = MoveTemp(Data.Worlds);
	for (auto& Pair : Worlds) { ReleaseWorld(Pair.Key.Get(), Pair.Value); }
}

void UGameFeatureAction_RPGCameraPresentation::RemoveWorld(UWorld* World,
	const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation)
{
	if (!World) { return; }
	FContextData* Active = FindActiveContext(ChangeContext, Generation);
	FWorldData Removed;
	if (Active && Active->Worlds.RemoveAndCopyValue(World, Removed)) { ReleaseWorld(World, Removed); }
}

void UGameFeatureAction_RPGCameraPresentation::OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context)
{
	FContextData Removed;
	if (ContextData.RemoveAndCopyValue(Context, Removed)) { ReleaseContext(Removed); }
	Super::OnGameFeatureDeactivating(Context);
}

void UGameFeatureAction_RPGCameraPresentation::OnGameFeatureUnregistering()
{
	bUnregistering = true;
	auto Removed = MoveTemp(ContextData);
	ContextData.Reset();
	for (auto& Pair : Removed) { ReleaseContext(Pair.Value); }
	Super::OnGameFeatureUnregistering();
}

#if WITH_EDITOR
EDataValidationResult UGameFeatureAction_RPGCameraPresentation::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = CombineDataValidationResults(Super::IsDataValid(Context), EDataValidationResult::Valid);
	if (CameraTargetActorTag.IsNone())
	{
		Context.AddError(NSLOCTEXT("RPGCameraPresentation", "MissingCameraTag", "A CameraActor tag is required."));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif
