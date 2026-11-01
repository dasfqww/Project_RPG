#include "RPGClientUIWidgetComponent.h"

#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "UObject/StrongObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogRPGClientUI, Log, All);

URPGClientUIWidgetComponent::URPGClientUIWidgetComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

TArray<FName> URPGClientUIWidgetComponent::AddWidgetRequests(
	const TArray<FRPGClientWidgetEntry>& Entries, bool bCreatedByFeature)
{
	TArray<FName> Accepted;
	if (!IsInGameThread() || bEndingPlay || IsBeingDestroyed() || IsUnreachable()
		|| HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)) { return Accepted; }
	bDestroyWhenUnused |= bCreatedByFeature;
	for (const FRPGClientWidgetEntry& Entry : Entries)
	{
		UClass* WidgetClass = Entry.WidgetClass.Get();
		if (Entry.WidgetId.IsNone() || !WidgetClass || !WidgetClass->IsChildOf(UUserWidget::StaticClass())
			|| WidgetClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated)
			|| Accepted.Contains(Entry.WidgetId))
		{
			UE_LOG(LogRPGClientUI, Warning, TEXT("RPG_CLIENT_UI SKIPPED ID=%s Reason=InvalidWidgetRequest"),
				*Entry.WidgetId.ToString());
			continue;
		}
		FRPGClientWidgetRequest* Existing = Requests.Find(Entry.WidgetId);
		if (Existing && (Existing->WidgetClass.Get() != WidgetClass || Existing->ZOrder != Entry.ZOrder
			|| Existing->bInputTransparent != Entry.bInputTransparent))
		{
			UE_LOG(LogRPGClientUI, Warning, TEXT("RPG_CLIENT_UI SKIPPED ID=%s Reason=ConfigurationConflict"),
				*Entry.WidgetId.ToString());
			continue;
		}
		FRPGClientWidgetRequest& Request = Existing ? *Existing : Requests.Add(Entry.WidgetId);
		if (!Existing)
		{
			Request.WidgetClass = WidgetClass;
			Request.ZOrder = Entry.ZOrder;
			Request.bInputTransparent = Entry.bInputTransparent;
			Request.Generation = ++NextGeneration;
		}
		++Request.References;
		Accepted.Add(Entry.WidgetId);
	}
	return Accepted;
}

void URPGClientUIWidgetComponent::OnRegister()
{
	TStrongObjectPtr<URPGClientUIWidgetComponent> KeepAlive(this);
	CancelPendingRefresh();
	++WidgetLifecycleGeneration;
	Super::OnRegister();
	// Wait until engine registration completes before widget callbacks can deactivate this feature.
	if (HasBegunPlay() && !bEndingPlay) { QueueRefresh(); }
}

void URPGClientUIWidgetComponent::OnUnregister()
{
	TStrongObjectPtr<URPGClientUIWidgetComponent> KeepAlive(
		!IsUnreachable() && !HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ? this : nullptr);
	TGuardValue<bool> UnregisteringGuard(bUnregisteringWidgets, true);
	CancelPendingRefresh();
	++WidgetLifecycleGeneration;
	// Mark unregistered before NativeDestruct can release the last feature reference and destroy this component.
	Super::OnUnregister();
	ReleaseWidgetInstances(false);
}

void URPGClientUIWidgetComponent::BeginPlay()
{
	TStrongObjectPtr<URPGClientUIWidgetComponent> KeepAlive(this);
	bEndingPlay = false;
	Super::BeginPlay();
	RefreshWidgets();
}

bool URPGClientUIWidgetComponent::CanManageWidgets(const UWorld* World) const
{
	if (!IsInGameThread() || bEndingPlay || bUnregisteringWidgets || !IsRegistered() || !HasBegunPlay()
		|| IsBeingDestroyed() || IsUnreachable() || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)) { return false; }
	const AHUD* HUD = Cast<AHUD>(GetOwner());
	return IsValid(HUD) && !HUD->IsActorBeingDestroyed() && IsValid(World) && !World->IsUnreachable()
		&& !World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) && World == GetWorld() && HUD->GetWorld() == World
		&& World->IsGameWorld() && !World->bIsTearingDown && World->GetNetMode() != NM_DedicatedServer;
}

void URPGClientUIWidgetComponent::QueueRefresh()
{
	UWorld* World = GetWorld();
	if (Requests.IsEmpty() || !CanManageWidgets(World)) { return; }
	if (PendingRefreshWorld.Get() == World && World->GetTimerManager().TimerExists(PendingRefreshTimer)) { return; }
	CancelPendingRefresh();
	PendingRefreshWorld = World;
	const TWeakObjectPtr<UWorld> ExpectedWorld(World);
	const uint64 Generation = WidgetLifecycleGeneration;
	World->GetTimerManager().SetTimer(PendingRefreshTimer,
		FTimerDelegate::CreateWeakLambda(this, [this, ExpectedWorld, Generation]()
		{
			if (WidgetLifecycleGeneration != Generation || PendingRefreshWorld != ExpectedWorld) { return; }
			PendingRefreshTimer.Invalidate();
			PendingRefreshWorld.Reset();
			RefreshWidgets();
		}), 0.25f, false);
}

void URPGClientUIWidgetComponent::CancelPendingRefresh()
{
	if (UWorld* World = PendingRefreshWorld.Get();
		IsValid(World) && !World->IsUnreachable() && !World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
	{
		World->GetTimerManager().ClearTimer(PendingRefreshTimer);
	}
	PendingRefreshTimer.Invalidate();
	PendingRefreshWorld.Reset();
}

void URPGClientUIWidgetComponent::RefreshWidgets()
{
	if (bRefreshingWidgets) { QueueRefresh(); return; }
	CancelPendingRefresh();
	UWorld* World = GetWorld();
	if (!CanManageWidgets(World) || Requests.IsEmpty()) { return; }
	TStrongObjectPtr<URPGClientUIWidgetComponent> KeepAlive(this);
	TGuardValue<bool> RefreshingGuard(bRefreshingWidgets, true);
	TStrongObjectPtr<UWorld> KeepWorld(World);
	TStrongObjectPtr<AHUD> HUD(Cast<AHUD>(GetOwner()));
	APlayerController* Candidate = HUD->GetOwningPlayerController();
	TStrongObjectPtr<APlayerController> Controller(IsValid(Candidate) && !Candidate->IsUnreachable() ? Candidate : nullptr);
	ULocalPlayer* CandidatePlayer = Controller.IsValid() ? Controller->GetLocalPlayer() : nullptr;
	TStrongObjectPtr<ULocalPlayer> Player(IsValid(CandidatePlayer) && !CandidatePlayer->IsUnreachable() ? CandidatePlayer : nullptr);
	const uint64 Lifecycle = WidgetLifecycleGeneration;
	const auto HasCurrentOwner = [&]()
	{
		return WidgetLifecycleGeneration == Lifecycle && CanManageWidgets(World) && GetOwner() == HUD.Get()
			&& HUD->GetOwningPlayerController() == Controller.Get() && Controller.IsValid()
			&& !Controller->IsActorBeingDestroyed() && Controller->IsLocalController() && Controller->GetWorld() == World
			&& Controller->GetHUD() == HUD.Get() && Player.IsValid() && Controller->GetLocalPlayer() == Player.Get()
			&& Player->GetPlayerController(World) == Controller.Get();
	};
	if (!HasCurrentOwner())
	{
		ReleaseWidgetInstances(false);
		// Local players may not be registered in the destination world yet; known remote HUDs need no retry.
		if (!Controller.IsValid() || Controller->IsActorBeingDestroyed() || Controller->IsLocalController()) { QueueRefresh(); }
		return;
	}

	TArray<FName> Ids;
	Requests.GetKeys(Ids);
	for (FName Id : Ids)
	{
		if (!HasCurrentOwner()) { QueueRefresh(); return; }
		FRPGClientWidgetRequest* Request = Requests.Find(Id);
		if (!Request || Request->bConstructing || Request->References <= 0) { continue; }
		if (UUserWidget* Existing = Request->Widget.Get())
		{
			if (!IsValid(Existing) || Existing->IsUnreachable() || Existing->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
			{
				Request->Widget = nullptr;
				Request->Generation = ++NextGeneration;
			}
			else
			{
				TStrongObjectPtr<UUserWidget> KeepExisting(Existing);
				const bool bMatchesOwner = Existing->GetOwningPlayer() == Controller.Get()
					&& Existing->GetOwningLocalPlayer() == Player.Get() && Existing->GetWorld() == World;
				if (!HasCurrentOwner()) { QueueRefresh(); return; }
				if (bMatchesOwner) { continue; }
				Request = Requests.Find(Id);
				if (!Request || Request->Widget != Existing) { continue; }
				Request->Widget = nullptr;
				Request->Generation = ++NextGeneration;
				Existing->RemoveFromParent();
				if (!HasCurrentOwner()) { QueueRefresh(); return; }
			}
		}
		Request = Requests.Find(Id);
		if (!Request || Request->Widget || Request->bConstructing || Request->References <= 0) { continue; }
		const uint64 Generation = Request->Generation;
		TStrongObjectPtr<UClass> WidgetClass(Request->WidgetClass.Get());
		const int32 ZOrder = Request->ZOrder;
		const bool bInputTransparent = Request->bInputTransparent;
		Request->bConstructing = true;
		TStrongObjectPtr<UUserWidget> NewWidget(CreateWidget<UUserWidget>(Controller.Get(), WidgetClass.Get()));
		Request = Requests.Find(Id);
		if (Request && Request->Generation == Generation) { Request->bConstructing = false; }
		UUserWidget* Widget = NewWidget.Get();
		if (!IsValid(Widget) || Widget->IsUnreachable()) { continue; }
		const auto IsCurrentRequest = [&]()
		{
			if (!IsValid(Widget) || Widget->IsUnreachable()
				|| Widget->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
				|| Widget->GetOwningPlayer() != Controller.Get() || Widget->GetOwningLocalPlayer() != Player.Get()
				|| Widget->GetWorld() != World || !HasCurrentOwner()) { return false; }
			const FRPGClientWidgetRequest* Current = Requests.Find(Id);
			return Current && Current->Generation == Generation && Current->References > 0;
		};
		const auto DiscardWidget = [&]()
		{
			FRPGClientWidgetRequest* Current = Requests.Find(Id);
			if (Current && Current->Generation == Generation && Current->Widget == Widget) { Current->Widget = nullptr; }
			if (IsValid(Widget) && !Widget->IsUnreachable()) { Widget->RemoveFromParent(); }
		};
		if (!IsCurrentRequest()) { DiscardWidget(); continue; }
		Request = Requests.Find(Id);
		if (!Request || Request->Widget) { DiscardWidget(); continue; }
		Request->Widget = Widget;
		if (bInputTransparent) { Widget->SetVisibility(ESlateVisibility::HitTestInvisible); }
		if (!IsCurrentRequest()) { DiscardWidget(); continue; }
		Request = Requests.Find(Id);
		if (!Request || Request->Widget != Widget) { DiscardWidget(); continue; }
		const bool bAdded = Widget->AddToPlayerScreen(ZOrder);
		const bool bCurrentRequest = bAdded && IsCurrentRequest();
		Request = Requests.Find(Id);
		if (!bCurrentRequest || !Request || Request->Widget != Widget)
		{
			DiscardWidget();
			continue;
		}
		UE_LOG(LogRPGClientUI, Display,
			TEXT("RPG_CLIENT_UI APPLIED World=%s HUD=%s ID=%s Widget=%s References=%d"),
			*World->GetName(), *HUD->GetName(), *Id.ToString(), *Widget->GetName(), Request->References);
	}
	if (!HasCurrentOwner()) { QueueRefresh(); return; }
	for (const auto& Pair : Requests)
	{
		if (Pair.Value.References > 0 && !Pair.Value.Widget) { QueueRefresh(); return; }
	}
	CancelPendingRefresh();
}

void URPGClientUIWidgetComponent::ReleaseWidgetInstances(bool bClearRequests)
{
	const FString OwnerName = GetNameSafe(GetOwner());
	TArray<TStrongObjectPtr<UUserWidget>> RemovedWidgets;
	TArray<FName> RemovedIds;
	// Detach every instance before the first NativeDestruct can mutate requests or collect a sibling widget.
	for (auto& Pair : Requests)
	{
		UUserWidget* Widget = Pair.Value.Widget.Get();
		if (IsValid(Widget) && !Widget->IsUnreachable() && !Widget->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
		{
			RemovedWidgets.Emplace(Widget);
			RemovedIds.Add(Pair.Key);
		}
		if (Pair.Value.Widget || Pair.Value.bConstructing) { Pair.Value.Generation = ++NextGeneration; }
		Pair.Value.Widget = nullptr;
		Pair.Value.bConstructing = false;
	}
	if (bClearRequests) { Requests.Reset(); }
	for (int32 Index = 0; Index < RemovedWidgets.Num(); ++Index)
	{
		RemovedWidgets[Index]->RemoveFromParent();
		UE_LOG(LogRPGClientUI, Display, TEXT("RPG_CLIENT_UI RELEASED HUD=%s ID=%s Reason=OwnerCleanup"),
			*OwnerName, *RemovedIds[Index].ToString());
	}
}

void URPGClientUIWidgetComponent::RemoveWidgetRequests(const TArray<FName>& WidgetIds)
{
	if (!IsInGameThread() || bEndingPlay || IsUnreachable()
		|| HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)) { return; }
	TStrongObjectPtr<URPGClientUIWidgetComponent> KeepAlive(this);
	const FString OwnerName = GetNameSafe(GetOwner());
	TArray<TStrongObjectPtr<UUserWidget>> RemovedWidgets;
	TArray<FName> ReleasedIds;
	TSet<FName> Seen;
	// Finish all reference bookkeeping before NativeDestruct can re-enter this component.
	for (FName Id : WidgetIds)
	{
		if (Seen.Contains(Id)) { continue; }
		Seen.Add(Id);
		FRPGClientWidgetRequest* Request = Requests.Find(Id);
		if (!Request || --Request->References > 0) { continue; }
		RemovedWidgets.Emplace(Request->Widget.Get());
		ReleasedIds.Add(Id);
		Requests.Remove(Id);
	}
	if (Requests.IsEmpty()) { CancelPendingRefresh(); }
	for (int32 Index = 0; Index < RemovedWidgets.Num(); ++Index)
	{
		if (RemovedWidgets[Index].IsValid()) { RemovedWidgets[Index]->RemoveFromParent(); }
		UE_LOG(LogRPGClientUI, Display, TEXT("RPG_CLIENT_UI RELEASED HUD=%s ID=%s"),
			*OwnerName, *ReleasedIds[Index].ToString());
	}
	if (!bEndingPlay && !IsBeingDestroyed() && bDestroyWhenUnused && Requests.IsEmpty())
	{
		DestroyComponent();
	}
}

void URPGClientUIWidgetComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	TStrongObjectPtr<URPGClientUIWidgetComponent> KeepAlive(
		!IsUnreachable() && !HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ? this : nullptr);
	bEndingPlay = true;
	CancelPendingRefresh();
	++WidgetLifecycleGeneration;
	ReleaseWidgetInstances(true);
	Super::EndPlay(Reason);
}
