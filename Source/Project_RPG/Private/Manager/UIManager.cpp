#include "Manager/UIManager.h"

#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Manager/RPGManagerContext.h"
#include "UI/RPGWidgetBase.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

UUIManager* UUIManager::Get()
{
    return Get(nullptr);
}

UUIManager* UUIManager::Get(const UObject* Context)
{
    check(IsInGameThread());
    if (UUIManager* Manager = Find(Context)) { return Manager; }
    return GetMutableDefault<UUIManager>();
}

UUIManager* UUIManager::Find(const UObject* Context)
{
    if (!IsInGameThread() || (Context && (!IsValid(Context)
        || Context->HasAnyFlags(RF_ClassDefaultObject)))) { return nullptr; }
    ULocalPlayer* Player = nullptr;
    if (const UUserWidget* Widget = Cast<UUserWidget>(Context))
    {
        Player = Widget->GetOwningLocalPlayer();
    }
    else if (const APlayerController* Controller = Cast<APlayerController>(Context))
    {
        Player = Controller->GetLocalPlayer();
    }
    else if (const ULocalPlayer* LocalPlayer = Cast<ULocalPlayer>(Context))
    {
        Player = const_cast<ULocalPlayer*>(LocalPlayer);
    }
    else if (UGameInstance* Instance = RPGManagerContext::ResolveGameInstance(Context))
    {
        Player = Instance->GetFirstGamePlayer();
    }
    UUIManager* Manager = IsValid(Player) ? Player->GetSubsystem<UUIManager>() : nullptr;
    return IsValid(Manager) && !Manager->bDeinitializing ? Manager : nullptr;
}

void UUIManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    bDeinitializing = false;
    FWorldDelegates::OnWorldCleanup.AddUObject(this, &ThisClass::OnWorldCleanup);
}

void UUIManager::Deinitialize()
{
    bDeinitializing = true;
    FWorldDelegates::OnWorldCleanup.RemoveAll(this);
    ClearUI();
    Super::Deinitialize();
}

void UUIManager::PlayerControllerChanged(APlayerController* NewPlayerController)
{
    // A replacement controller owns a new HUD and new popup instances.
    ClearUI();
    Super::PlayerControllerChanged(NewPlayerController);
}

void UUIManager::PushUI(URPGWidgetBase* NewUI)
{
    if (!IsValid(NewUI) || NewUI->IsRemovingFromParent()) { return; }
    UUIManager* Owner = Find(NewUI);
    if (Owner != this)
    {
        if (Owner) { Owner->PushUI(NewUI); }
        return;
    }
    UWorld* World = NewUI->GetWorld();
    if (bClearingUI || bDeinitializing || !World || World->bIsTearingDown
        || PopUpUIStack.Contains(NewUI)) { return; }

    TStrongObjectPtr<URPGWidgetBase> KeepAlive(NewUI);
    if (URPGWidgetBase* Top = GetTopUI()) { Top->SetIsEnabled(false); }
    if (bClearingUI || bDeinitializing || World->bIsTearingDown
        || NewUI->IsRemovingFromParent() || PopUpUIStack.Contains(NewUI))
    {
        ActivateTopUI();
        return;
    }
    PopUpUIStack.Add(NewUI);
    WidgetWorlds.Add(NewUI, World);
    if (!NewUI->IsInViewport() && !NewUI->AddToPlayerScreen())
    {
        ForgetUI(NewUI);
        return;
    }
    // NativeConstruct may remove the widget or tear down its owner.
    if (bDeinitializing || World->bIsTearingDown || !PopUpUIStack.Contains(NewUI))
    {
        ForgetUI(NewUI);
        NewUI->RemoveFromParent();
        return;
    }
    NewUI->SetIsEnabled(GetTopUI() == NewUI);
    NewUI->SetVisibility(ESlateVisibility::Visible);
}

void UUIManager::PopUI()
{
    if (URPGWidgetBase* Top = GetTopUI()) { RemoveUI(Top); }
}

void UUIManager::RemoveUI(URPGWidgetBase* TargetUI)
{
    if (!IsValid(TargetUI)) { return; }
    UUIManager* Owner = Find(TargetUI);
    if (Owner != this)
    {
        if (Owner) { Owner->RemoveUI(TargetUI); }
        return;
    }
    if (!PopUpUIStack.Contains(TargetUI)) { return; }
    const bool bWasTop = GetTopUI() == TargetUI;
    TStrongObjectPtr<URPGWidgetBase> KeepAlive(TargetUI);
    PopUpUIStack.Remove(TargetUI);
    WidgetWorlds.Remove(TargetUI);
    TargetUI->RemoveFromParent();
    if (bWasTop) { ActivateTopUI(); }
}

void UUIManager::ForgetUI(URPGWidgetBase* TargetUI)
{
    if (HasAnyFlags(RF_ClassDefaultObject))
    {
        if (UUIManager* Owner = Find(TargetUI)) { Owner->ForgetUI(TargetUI); }
        return;
    }
    WidgetWorlds.Remove(TargetUI);
    const bool bWasTop = !PopUpUIStack.IsEmpty() && PopUpUIStack.Last() == TargetUI;
    if (PopUpUIStack.Remove(TargetUI) > 0 && bWasTop) { ActivateTopUI(); }
}

void UUIManager::ActivateTopUI()
{
    if (bClearingUI || bDeinitializing) { return; }
    if (URPGWidgetBase* Top = GetTopUI())
    {
        UWorld* World = Top->GetWorld();
        if (!World || World->bIsTearingDown) { return; }
        Top->SetIsEnabled(true);
        Top->SetVisibility(ESlateVisibility::Visible);
    }
}

URPGWidgetBase* UUIManager::GetTopUI() const
{
    if (HasAnyFlags(RF_ClassDefaultObject) || bDeinitializing) { return nullptr; }
    for (int32 Index = PopUpUIStack.Num() - 1; Index >= 0; --Index)
    {
        URPGWidgetBase* Widget = PopUpUIStack[Index];
        if (IsValid(Widget)) { return Widget; }
    }
    return nullptr;
}

void UUIManager::ClearUI()
{
    if (HasAnyFlags(RF_ClassDefaultObject)) { return; }
    ClearUIForWorld(nullptr);
}

void UUIManager::ClearUIForWorld(UWorld* World)
{
    if (bClearingUI) { return; }
    TGuardValue<bool> ClearingGuard(bClearingUI, true);
    TArray<TStrongObjectPtr<URPGWidgetBase>> Removed;
    for (int32 Index = PopUpUIStack.Num() - 1; Index >= 0; --Index)
    {
        URPGWidgetBase* Widget = PopUpUIStack[Index];
        if (!IsValid(Widget) || !World || WidgetWorlds.FindRef(Widget).Get() == World)
        {
            if (IsValid(Widget)) { Removed.Emplace(Widget); }
            WidgetWorlds.Remove(Widget);
            PopUpUIStack.RemoveAt(Index);
        }
    }
    if (!World) { WidgetWorlds.Reset(); }
    for (const TStrongObjectPtr<URPGWidgetBase>& Widget : Removed)
    {
        Widget->RemoveFromParent();
    }
}

void UUIManager::OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
    const TWeakObjectPtr<URPGWidgetBase> PreviousTop(GetTopUI());
    const bool bHadTop = PreviousTop.IsValid();
    ClearUIForWorld(World);
    if (bHadTop && PreviousTop != TWeakObjectPtr<URPGWidgetBase>(GetTopUI()))
    {
        ActivateTopUI();
    }
}
