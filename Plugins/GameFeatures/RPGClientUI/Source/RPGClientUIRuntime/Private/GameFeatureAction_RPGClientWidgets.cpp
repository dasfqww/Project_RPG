#include "GameFeatureAction_RPGClientWidgets.h"

#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "RPGClientUIWidgetComponent.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif
#if WITH_EDITORONLY_DATA
#include "AssetRegistry/AssetBundleData.h"
#endif

UGameFeatureAction_RPGClientWidgets::FContextData* UGameFeatureAction_RPGClientWidgets::FindActiveContext(
	const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation)
{
	FContextData* Data = ContextData.Find(ChangeContext);
	return Data && Data->Generation == Generation ? Data : nullptr;
}

void UGameFeatureAction_RPGClientWidgets::OnGameFeatureActivating(FGameFeatureActivatingContext& Context)
{
	TStrongObjectPtr<UGameFeatureAction_RPGClientWidgets> KeepAlive(this);
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

void UGameFeatureAction_RPGClientWidgets::AddToWorld(const FWorldContext& WorldContext,
	const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation)
{
	UWorld* World = WorldContext.World();
	if (!IsValid(World) || !World->IsGameWorld() || World->bIsTearingDown
		|| World->GetNetMode() == NM_DedicatedServer || !ChangeContext.ShouldApplyToWorldContext(WorldContext))
	{
		return;
	}
	FContextData* Active = FindActiveContext(ChangeContext, Generation);
	if (!Active || Active->Worlds.Contains(World)) { return; }
	FWorldData& Data = Active->Worlds.Add(World);
	const TWeakObjectPtr<UWorld> WeakWorld(World);
	Data.ActorSpawnedHandle = World->AddOnActorSpawnedHandler(
		FOnActorSpawned::FDelegate::CreateWeakLambda(this,
			[this, WeakWorld, ChangeContext, Generation](AActor* Actor)
			{
				UWorld* OwnerWorld = WeakWorld.Get();
				AHUD* HUD = Cast<AHUD>(Actor);
				if (!HUD || !OwnerWorld || OwnerWorld->bIsTearingDown
					|| HUD->GetWorld() != OwnerWorld) { return; }
				FContextData* Current = FindActiveContext(ChangeContext, Generation);
				FWorldData* WorldData = Current ? Current->Worlds.Find(WeakWorld) : nullptr;
				if (WorldData) { AttachToHUD(HUD, *WorldData); }
			}));
	TArray<TWeakObjectPtr<AHUD>> HUDs;
	for (TActorIterator<AHUD> It(World); It; ++It) { HUDs.Add(*It); }
	for (const TWeakObjectPtr<AHUD>& WeakHUD : HUDs)
	{
		FContextData* Current = FindActiveContext(ChangeContext, Generation);
		FWorldData* WorldData = Current ? Current->Worlds.Find(WeakWorld) : nullptr;
		if (!WorldData || !IsValid(World) || World->bIsTearingDown) { return; }
		AHUD* HUD = WeakHUD.Get();
		if (HUD && HUD->GetWorld() == World) { AttachToHUD(HUD, *WorldData); }
	}
}

void UGameFeatureAction_RPGClientWidgets::AttachToHUD(AHUD* HUD, FWorldData& Data)
{
	if (!IsValid(HUD) || HUD->IsActorBeingDestroyed() || Data.HUDs.Contains(HUD)) { return; }
	TStrongObjectPtr<AHUD> KeepHUDAlive(HUD);
	URPGClientUIWidgetComponent* Component = HUD->FindComponentByClass<URPGClientUIWidgetComponent>();
	if (!IsValid(Component) || Component->IsBeingDestroyed()) { Component = nullptr; }
	const bool bCreated = Component == nullptr;
	if (bCreated)
	{
		Component = NewObject<URPGClientUIWidgetComponent>(HUD);
		HUD->AddInstanceComponent(Component);
	}
	if (!IsValid(Component) || Component->IsBeingDestroyed()) { return; }
	TStrongObjectPtr<URPGClientUIWidgetComponent> KeepComponentAlive(Component);
	TArray<FName> Ids = Component->AddWidgetRequests(Widgets, bCreated);
	if (Ids.IsEmpty())
	{
		if (bCreated) { Component->DestroyComponent(); }
		return;
	}
	FHUDRequest& Request = Data.HUDs.Add(HUD);
	Request.Component = Component;
	Request.WidgetIds = MoveTemp(Ids);
	// Record cleanup ownership before widget construction can call back into the feature system.
	if (bCreated) { Component->RegisterComponent(); }
	else { Component->RefreshWidgets(); }
}

void UGameFeatureAction_RPGClientWidgets::ReleaseWorld(UWorld* World, FWorldData& Data)
{
	if (World) { World->RemoveOnActorSpawnedHandler(Data.ActorSpawnedHandle); }
	TMap<TWeakObjectPtr<AHUD>, FHUDRequest> Removed = MoveTemp(Data.HUDs);
	for (const auto& Pair : Removed)
	{
		if (URPGClientUIWidgetComponent* Component = Pair.Value.Component.Get())
		{
			Component->RemoveWidgetRequests(Pair.Value.WidgetIds);
		}
	}
}

void UGameFeatureAction_RPGClientWidgets::RemoveWorld(UWorld* World,
	const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation)
{
	FContextData* Active = FindActiveContext(ChangeContext, Generation);
	FWorldData Removed;
	if (Active && Active->Worlds.RemoveAndCopyValue(World, Removed))
	{
		ReleaseWorld(World, Removed);
	}
}

void UGameFeatureAction_RPGClientWidgets::OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context)
{
	FContextData Removed;
	if (ContextData.RemoveAndCopyValue(Context, Removed))
	{
		FWorldDelegates::OnStartGameInstance.Remove(Removed.GameInstanceStarted);
		FWorldDelegates::OnPostWorldInitialization.Remove(Removed.WorldInitialized);
		FWorldDelegates::OnWorldCleanup.Remove(Removed.WorldCleanup);
		for (auto& Pair : Removed.Worlds) { ReleaseWorld(Pair.Key.Get(), Pair.Value); }
	}
	Super::OnGameFeatureDeactivating(Context);
}

#if WITH_EDITOR
EDataValidationResult UGameFeatureAction_RPGClientWidgets::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = CombineDataValidationResults(Super::IsDataValid(Context), EDataValidationResult::Valid);
	TSet<FName> Ids;
	if (Widgets.IsEmpty())
	{
		Context.AddError(FText::FromString(TEXT("At least one client widget is required.")));
		Result = EDataValidationResult::Invalid;
	}
	for (const FRPGClientWidgetEntry& Entry : Widgets)
	{
		UClass* WidgetClass = Entry.WidgetClass.LoadSynchronous();
		if (Entry.WidgetId.IsNone() || Ids.Contains(Entry.WidgetId) || !WidgetClass
			|| !WidgetClass->IsChildOf(UUserWidget::StaticClass()) || WidgetClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("Invalid widget ID or class: %s (%s)"), *Entry.WidgetId.ToString(),
				*Entry.WidgetClass.ToSoftObjectPath().ToString())));
			Result = EDataValidationResult::Invalid;
		}
		Ids.Add(Entry.WidgetId);
	}
	return Result;
}
#endif

#if WITH_EDITORONLY_DATA
void UGameFeatureAction_RPGClientWidgets::AddAdditionalAssetBundleData(FAssetBundleData& AssetBundleData)
{
	Super::AddAdditionalAssetBundleData(AssetBundleData);
	for (const FRPGClientWidgetEntry& Entry : Widgets)
	{
		if (!Entry.WidgetClass.IsNull())
		{
			AssetBundleData.AddBundleAsset(TEXT("Client"), Entry.WidgetClass.ToSoftObjectPath().GetAssetPath());
		}
	}
}
#endif
