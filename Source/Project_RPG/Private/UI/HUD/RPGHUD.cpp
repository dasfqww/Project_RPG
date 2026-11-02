// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/HUD/RPGHUD.h"
#include "UI/RPGMainMenuWidget.h"
#include "UI/RPGInteractionWidget.h"
#include "UI/Option/RPGOptionMenu.h"
#include "UI/Content/RPGCombatContributionWidget.h"
#include "UI/Content/RPGTrainingGuideWidget.h"
#include "UI/Content/RPGIntroApproachGuideWidget.h"
#include "UI/Progress/RPGActionProgressWidget.h"
#include "Component/UI/PlayerUIComponent.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "UObject/StrongObjectPtr.h"
#include "Blueprint/UserWidget.h"

ARPGHUD::ARPGHUD()
{
    ActionProgressWidgetClass = URPGActionProgressWidget::StaticClass();
}

void ARPGHUD::BeginPlay()
{
	bEndingOptionsUI = false;
	Super::BeginPlay();

	if (MainMenuWidgetClass)
	{
		//�巡�׵���� �ȵȴٸ� AddToViewport(zorder)<<�����Ұ�
		MainMenuWidget = CreateWidget<URPGMainMenuWidget>(GetWorld(), MainMenuWidgetClass);
		MainMenuWidget->AddToViewport(-1);
		MainMenuWidget->SetVisibility(ESlateVisibility::Collapsed);
	}

	CreateDefaultOptionMenuWidget();
	InitializePlayerHUD();
	if (IsActorBeingDestroyed() || bEndingPlayerHUD) return;
	CreateActionProgressWidget();
	if (IsActorBeingDestroyed() || bEndingOptionsUI) return;
	if (auto* Controller = GetOwningPlayerController(); IsValid(Controller)
		&& Controller->IsLocalController() && Controller->GetLocalPlayer())
	{
		CombatContributionWidget = CreateWidget<URPGCombatContributionWidget>(Controller);
		if (CombatContributionWidget && !CombatContributionWidget->AddToPlayerScreen(5))
		{
			CombatContributionWidget->RemoveFromParent();
			CombatContributionWidget = nullptr;
		}
		TrainingGuideWidget = CreateWidget<URPGTrainingGuideWidget>(Controller);
		if (TrainingGuideWidget && !TrainingGuideWidget->AddToPlayerScreen(5))
		{
			TrainingGuideWidget->RemoveFromParent();
			TrainingGuideWidget = nullptr;
		}
		if (IsActorBeingDestroyed() || bEndingPlayerHUD) return;
		TStrongObjectPtr<URPGIntroApproachGuideWidget> IntroGuide(CreateWidget<URPGIntroApproachGuideWidget>(Controller));
		if (IntroGuide.IsValid())
		{
			IntroApproachGuideWidget = IntroGuide.Get();
			const bool bAdded = IntroGuide->AddToPlayerScreen(5);
			if (!bAdded || IsActorBeingDestroyed() || bEndingPlayerHUD || IntroApproachGuideWidget != IntroGuide.Get())
			{
				IntroGuide->RemoveFromParent();
				if (IntroApproachGuideWidget == IntroGuide.Get()) IntroApproachGuideWidget = nullptr;
			}
		}
	}

	if (InteractionWidgetClass)
	{
		InteractionWidget = CreateWidget<URPGInteractionWidget>(GetWorld(), InteractionWidgetClass);
		InteractionWidget->AddToViewport(-1);
		InteractionWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void ARPGHUD::EndPlay(const EEndPlayReason::Type Reason)
{
    // Block creation before any authored Destruct callback can change possession.
    bEndingPlayerHUD = true;
    if (APlayerController* Controller = PlayerHUDController.Get())
        Controller->OnPossessedPawnChanged.RemoveDynamic(this, &ARPGHUD::OnPlayerHUDPawnChanged);
    PlayerHUDController.Reset();
    RemovePlayerHUD();
    RemoveActionProgressWidget();
    if (IntroApproachGuideWidget)
    {
        IntroApproachGuideWidget->RemoveFromParent();
        IntroApproachGuideWidget = nullptr;
    }
    if (TrainingGuideWidget)
    {
        TrainingGuideWidget->RemoveFromParent();
        TrainingGuideWidget = nullptr;
    }
    if (CombatContributionWidget)
    {
        CombatContributionWidget->RemoveFromParent();
        CombatContributionWidget = nullptr;
    }
    bEndingOptionsUI = true;
    ++OptionMenuProviderRevision;
    // A seamless controller replacement can destroy this HUD after it has moved worlds.
    // Release its options widget before components end, so they cannot restore a fallback.
    OptionMenuProvider.Reset();
    URPGOptionMenu* Widget = OptionMenuWidget;
    OptionMenuWidget = nullptr;
    bIsOptionMenuVisible = false;
    if (IsValid(Widget))
    {
        TStrongObjectPtr<URPGOptionMenu> KeepAlive(Widget);
        Widget->OnMenuHidden();
        Widget->RemoveFromParent();
    }
    Super::EndPlay(Reason);
}

void ARPGHUD::InitializePlayerHUD()
{
    APlayerController* Controller = GetOwningPlayerController();
    if (bEndingPlayerHUD || !IsValid(Controller) || !Controller->IsLocalController()
        || !Controller->GetLocalPlayer() || Controller->GetWorld() != GetWorld()) return;
    PlayerHUDController = Controller;
    Controller->OnPossessedPawnChanged.AddUniqueDynamic(this, &ARPGHUD::OnPlayerHUDPawnChanged);
    RefreshPlayerHUD();
}

void ARPGHUD::OnPlayerHUDPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
    // Another possession listener may have already changed Pawn again.
    RefreshPlayerHUD();
}

void ARPGHUD::RemovePlayerHUD()
{
    ++PlayerHUDRevision;
    TStrongObjectPtr<UUserWidget> Previous(PlayerHUDWidget);
    PlayerHUDWidget = nullptr;
    PlayerHUDPawn.Reset();
    if (Previous.IsValid()) Previous->RemoveFromParent();
}

void ARPGHUD::RefreshPlayerHUD()
{
    TStrongObjectPtr<ARPGHUD> KeepAlive(this);
    APlayerController* Controller = PlayerHUDController.Get();
    APawn* Pawn = IsValid(Controller) ? Controller->GetPawn() : nullptr;
    const auto ValidOwner = [this, Controller, Pawn]()
    {
        UWorld* World = GetWorld();
        return !bEndingPlayerHUD && !IsActorBeingDestroyed() && World && !World->bIsTearingDown
            && IsValid(Controller) && !Controller->IsActorBeingDestroyed()
            && GetOwningPlayerController() == Controller && Controller->IsLocalController()
            && Controller->GetWorld() == World && Controller->GetLocalPlayer()
            && Controller->GetLocalPlayer()->GetPlayerController(World) == Controller
            && IsValid(Pawn) && !Pawn->IsActorBeingDestroyed() && Pawn->GetWorld() == World
            && Controller->GetPawn() == Pawn;
    };
    if (PlayerHUDWidget && PlayerHUDPawn == Pawn && ValidOwner()) return;

    TStrongObjectPtr<APlayerController> KeepControllerAlive(Controller);
    TStrongObjectPtr<APawn> KeepPawnAlive(Pawn);
    TStrongObjectPtr<UWorld> KeepWorldAlive(GetWorld());

    // Legacy visual modules capture their Pawn during OnInitialized. Recreate the
    // visual tree on possession, rather than leaving their old bindings on screen.
    const uint64 Revision = ++PlayerHUDRevision;
    TStrongObjectPtr<UUserWidget> Previous(PlayerHUDWidget);
    PlayerHUDWidget = nullptr;
    PlayerHUDPawn.Reset();
    if (Previous.IsValid()) Previous->RemoveFromParent();
    if (Revision != PlayerHUDRevision || !ValidOwner() || !PlayerHUDWidgetClass) return;

    TStrongObjectPtr<UUserWidget> Widget(CreateWidget<UUserWidget>(Controller, PlayerHUDWidgetClass));
    if (!Widget.IsValid()) return;
    if (Revision != PlayerHUDRevision || !ValidOwner())
    {
        Widget->RemoveFromParent();
        return;
    }
    PlayerHUDWidget = Widget.Get();
    PlayerHUDPawn = Pawn;
    // Construct can end the HUD or change possession. Run it before registration,
    // then verify the revision so a superseded tree never enters the player layer.
    const TSharedRef<SWidget> PreparedWidget = Widget->TakeWidget();
    const auto StillCurrent = [&]()
    {
        return Revision == PlayerHUDRevision && PlayerHUDWidget == Widget.Get() && ValidOwner();
    };
    if (!StillCurrent() || !Widget->AddToPlayerScreen(PlayerHUDZOrder) || !StillCurrent())
    {
        if (PlayerHUDWidget == Widget.Get()) RemovePlayerHUD();
        else Widget->RemoveFromParent();
    }
}

void ARPGHUD::CreateActionProgressWidget()
{
    APlayerController* Controller = GetOwningPlayerController();
    if (ActionProgressWidget || !ActionProgressWidgetClass || IsActorBeingDestroyed() || !GetWorld() || GetWorld()->bIsTearingDown
        || !IsValid(Controller) || Controller->IsActorBeingDestroyed()
        || !Controller->IsLocalController() || !Controller->GetLocalPlayer()
        || Controller->GetWorld() != GetWorld()
        || Controller->GetLocalPlayer()->GetPlayerController(GetWorld()) != Controller) return;

    const auto IsFinite = [](FVector2D Value) { return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y); };
    if (!IsFinite(ActionProgressSize) || ActionProgressSize.X <= 0 || ActionProgressSize.Y <= 0
        || !IsFinite(ActionProgressAnchor) || !IsFinite(ActionProgressAlignment) || !IsFinite(ActionProgressOffset)) return;
    // Authored initialization/Construct callbacks can end this HUD before the call returns.
    TStrongObjectPtr<ARPGHUD> KeepAlive(this);
    TStrongObjectPtr<APlayerController> KeepControllerAlive(Controller);
    TStrongObjectPtr<UWorld> KeepWorldAlive(GetWorld());
    TStrongObjectPtr<URPGActionProgressWidget> Widget(CreateWidget<URPGActionProgressWidget>(Controller, ActionProgressWidgetClass));
    if (!Widget.IsValid()) return;
    if (IsActorBeingDestroyed() || bEndingOptionsUI || GetOwningPlayerController() != Controller
        || !IsValid(Controller) || Controller->IsActorBeingDestroyed() || !GetWorld() || GetWorld()->bIsTearingDown) return;
    ActionProgressWidget = Widget.Get();
    // The HUD owns screen placement; the module owns all internal presentation.
    Widget->SetDesiredSizeInViewport(ActionProgressSize);
    // Position/size setters reset anchors in the viewport subsystem.
    Widget->SetPositionInViewport(ActionProgressOffset, false);
    Widget->SetAnchorsInViewport(FAnchors(ActionProgressAnchor.X, ActionProgressAnchor.Y));
    Widget->SetAlignmentInViewport(ActionProgressAlignment);
    // Run authored Construct before viewport registration. A callback that ends
    // this HUD must not leave a canvas inserted after EndPlay already removed it.
    const TSharedRef<SWidget> PreparedWidget = Widget->TakeWidget();
    if (IsActorBeingDestroyed() || bEndingOptionsUI || ActionProgressWidget != Widget.Get()
        || GetOwningPlayerController() != Controller || Controller->IsActorBeingDestroyed()
        || !GetWorld() || GetWorld()->bIsTearingDown || Controller->GetWorld() != GetWorld())
    {
        if (ActionProgressWidget == Widget.Get()) RemoveActionProgressWidget();
        return;
    }
    if (!Widget->AddToPlayerScreen(ActionProgressZOrder))
    {
        RemoveActionProgressWidget();
        return;
    }
    if (IsActorBeingDestroyed() || bEndingOptionsUI || ActionProgressWidget != Widget.Get()
        || GetOwningPlayerController() != Controller || Controller->IsActorBeingDestroyed()
        || !GetWorld() || GetWorld()->bIsTearingDown)
    {
        if (ActionProgressWidget == Widget.Get()) RemoveActionProgressWidget();
        Widget->SetProgressSource(nullptr);
        Widget->RemoveFromParent();
        return;
    }
    ProgressController = Controller;
    // This delegate runs on clients as well as authority (unlike OnNewPawn).
    Controller->OnPossessedPawnChanged.AddUniqueDynamic(this, &ThisClass::OnProgressPawnChanged);
    OnProgressPawnChanged(nullptr, Controller->GetPawn());
}

void ARPGHUD::OnProgressPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
    if (!ActionProgressWidget) return;
    APlayerController* Controller = ProgressController.Get();
    // Reread current ownership: an earlier listener may have changed possession again.
    APawn* Pawn = IsValid(Controller) && Controller == GetOwningPlayerController()
        && Controller->IsLocalController() && !Controller->IsActorBeingDestroyed()
        && Controller->GetWorld() == GetWorld() ? Controller->GetPawn() : nullptr;
    // Controller::Pawn can replicate before Pawn::Controller. Bind from this local
    // controller's current Pawn; the component handles its own controller lifetime.
    UPlayerUIComponent* Source = IsValid(Pawn) && !Pawn->IsActorBeingDestroyed()
        && Pawn->GetWorld() == GetWorld()
        ? Pawn->FindComponentByClass<UPlayerUIComponent>() : nullptr;
    ActionProgressWidget->SetProgressSource(Source);
}

void ARPGHUD::RemoveActionProgressWidget()
{
    if (APlayerController* Controller = ProgressController.Get())
        Controller->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::OnProgressPawnChanged);
    ProgressController.Reset();
    URPGActionProgressWidget* Widget = ActionProgressWidget;
    ActionProgressWidget = nullptr;
    if (IsValid(Widget))
    {
        Widget->SetProgressSource(nullptr);
        Widget->RemoveFromParent();
    }
}

void ARPGHUD::DisplayMenu()
{
	if (MainMenuWidget)
	{
		bIsMenuVisible = true;
		MainMenuWidget->SetVisibility(ESlateVisibility::Visible);
	}
}

void ARPGHUD::HideMenu()
{
	if (MainMenuWidget)
	{
		bIsMenuVisible = false;
		MainMenuWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void ARPGHUD::ToggleMenu()
{
	if (bIsOptionMenuVisible)
	{
		HideOptionMenu();
	}

	if (bIsMenuVisible)
	{
		HideMenu();

		const FInputModeGameOnly InputMode;
		GetOwningPlayerController()->SetInputMode(InputMode);
		GetOwningPlayerController()->SetShowMouseCursor(false);
	}

	else
	{
		DisplayMenu();

		const FInputModeGameAndUI InputMode;
		GetOwningPlayerController()->SetInputMode(InputMode);
		GetOwningPlayerController()->SetShowMouseCursor(true);
	}
}


void ARPGHUD::CreateDefaultOptionMenuWidget()
{
    APlayerController* Controller = GetOwningPlayerController();
    UWorld* World = GetWorld();
    if (bEndingOptionsUI || OptionMenuProvider.IsValid() || OptionMenuWidget || !OptionMenuWidgetClass
        || !IsValid(Controller) || !Controller->IsLocalController() || !Controller->GetLocalPlayer()
        || !World || World->bIsTearingDown || Controller->GetWorld() != World
        || IsActorBeingDestroyed() || Controller->IsActorBeingDestroyed())
    {
        return;
    }
    if (Controller->GetLocalPlayer()->GetPlayerController(World) != Controller) { return; }
    TStrongObjectPtr<ARPGHUD> KeepHUDAlive(this);
    TStrongObjectPtr<APlayerController> KeepControllerAlive(Controller);
    TStrongObjectPtr<UWorld> KeepWorldAlive(World);
    const uint64 Revision = ++OptionMenuProviderRevision;
    TStrongObjectPtr<URPGOptionMenu> Widget(CreateWidget<URPGOptionMenu>(Controller, OptionMenuWidgetClass));
    if (!Widget.IsValid()) { return; }
    const auto OwnsRequest = [&]()
    {
        return Revision == OptionMenuProviderRevision && !bEndingOptionsUI && !IsActorBeingDestroyed()
            && !World->bIsTearingDown && GetWorld() == World && GetOwningPlayerController() == Controller
            && !Controller->IsActorBeingDestroyed() && Controller->GetWorld() == World
            && Controller->GetLocalPlayer() && Controller->GetLocalPlayer()->GetPlayerController(World) == Controller
            && Widget->GetOwningPlayer() == Controller
            && !Widget->IsRemovingFromParent() && !OptionMenuProvider.IsValid();
    };
    const auto RemoveUnclaimedWidget = [&]()
    {
        if (OptionMenuWidget != Widget.Get())
        {
            Widget->OnMenuHidden();
            if (OptionMenuWidget != Widget.Get()) { Widget->RemoveFromParent(); }
        }
    };
    if (!OwnsRequest() || OptionMenuWidget)
    {
        RemoveUnclaimedWidget();
        return;
    }
    Widget->SetVisibility(ESlateVisibility::Collapsed);
    if (!OwnsRequest() || OptionMenuWidget)
    {
        RemoveUnclaimedWidget();
        return;
    }
    OptionMenuWidget = Widget.Get();
    const bool bAdded = Widget->IsInViewport() || Widget->AddToPlayerScreen(10);
    if (!OwnsRequest() || OptionMenuWidget != Widget.Get())
    {
        RemoveUnclaimedWidget();
        return;
    }
    if (!bAdded)
    {
        OptionMenuWidget = nullptr;
        bIsOptionMenuVisible = false;
        ++OptionMenuProviderRevision;
        RemoveUnclaimedWidget();
    }
}

bool ARPGHUD::IsOptionMenuProvider(const UObject* Provider, const URPGOptionMenu* Widget) const
{
    return !bEndingOptionsUI && IsValid(Provider) && IsValid(Widget)
        && OptionMenuProvider.Get() == Provider && OptionMenuWidget == Widget;
}

bool ARPGHUD::SetOptionMenuProvider(UObject* Provider, URPGOptionMenu* Widget)
{
    APlayerController* Controller = GetOwningPlayerController();
    UWorld* World = GetWorld();
    if (bEndingOptionsUI || IsActorBeingDestroyed() || !World || World->bIsTearingDown
        || !IsValid(Provider) || !IsValid(Widget) || Widget->IsRemovingFromParent()
        || !IsValid(Controller) || Controller->IsActorBeingDestroyed() || !Controller->IsLocalController()
        || !Controller->GetLocalPlayer() || Controller->GetWorld() != World
        || Widget->GetOwningPlayer() != Controller || Widget->GetWorld() != World
        || (OptionMenuProvider.IsValid() && OptionMenuProvider.Get() != Provider))
    {
        return false;
    }
    if (IsOptionMenuProvider(Provider, Widget)) { return true; }
    TStrongObjectPtr<ARPGHUD> KeepHUDAlive(this);
    TStrongObjectPtr<UObject> KeepProviderAlive(Provider);
    TStrongObjectPtr<APlayerController> KeepControllerAlive(Controller);
    TStrongObjectPtr<UWorld> KeepWorldAlive(World);
    TStrongObjectPtr<URPGOptionMenu> KeepWidgetAlive(Widget);
    TStrongObjectPtr<URPGOptionMenu> PreviousWidget(IsValid(OptionMenuWidget) ? OptionMenuWidget : nullptr);
    const uint64 Revision = ++OptionMenuProviderRevision;
    // Publish ownership before old-widget callbacks so a reentrant release can identify this request.
    OptionMenuWidget = Widget;
    OptionMenuProvider = Provider;
    if (PreviousWidget.Get() != Widget)
    {
        bIsOptionMenuVisible = false;
        if (PreviousWidget.IsValid())
        {
            PreviousWidget->OnMenuHidden();
            if (OptionMenuWidget != PreviousWidget.Get()) { PreviousWidget->RemoveFromParent(); }
        }
    }
    return Revision == OptionMenuProviderRevision && IsOptionMenuProvider(Provider, Widget)
        && GetWorld() == World && !World->bIsTearingDown && GetOwningPlayerController() == Controller
        && !IsActorBeingDestroyed() && !Controller->IsActorBeingDestroyed()
        && Controller->GetWorld() == World && Widget->GetOwningPlayer() == Controller && Widget->GetWorld() == World;
}

void ARPGHUD::ClearOptionMenuProvider(UObject* Provider, bool bRestoreDefault)
{
    if (OptionMenuProvider.Get() != Provider || !Provider) { return; }
    TStrongObjectPtr<ARPGHUD> KeepHUDAlive(this);
    TStrongObjectPtr<URPGOptionMenu> Widget(IsValid(OptionMenuWidget) ? OptionMenuWidget : nullptr);
    TStrongObjectPtr<UWorld> World(GetWorld());
    TWeakObjectPtr<APlayerController> Controller(GetOwningPlayerController());
    const bool bWasVisible = bIsOptionMenuVisible && Widget.IsValid()
        && Widget->IsInViewport() && Widget->IsVisible();
    const uint64 Revision = ++OptionMenuProviderRevision;
    // Detach first. Destruction/focus callbacks can install a different provider.
    OptionMenuProvider.Reset();
    OptionMenuWidget = nullptr;
    bIsOptionMenuVisible = false;
    if (Widget.IsValid())
    {
        Widget->OnMenuHidden();
        if (OptionMenuWidget != Widget.Get()) { Widget->RemoveFromParent(); }
    }
    if (Revision != OptionMenuProviderRevision || OptionMenuProvider.IsValid() || OptionMenuWidget
        || bEndingOptionsUI || !bRestoreDefault || !World.IsValid() || World->bIsTearingDown
        || GetWorld() != World.Get() || GetOwningPlayerController() != Controller.Get()
        || !HasActorBegunPlay() || IsActorBeingDestroyed()) { return; }
    CreateDefaultOptionMenuWidget();
    if (bWasVisible && OptionMenuProviderRevision == Revision + 1 && !OptionMenuProvider.IsValid()
        && OptionMenuWidget && !bEndingOptionsUI && GetWorld() == World.Get() && !World->bIsTearingDown)
    {
        DisplayOptionMenu();
    }
}

void ARPGHUD::DisplayOptionMenu()
{
    APlayerController* Controller = GetOwningPlayerController();
    UWorld* World = GetWorld();
    URPGOptionMenu* Widget = OptionMenuWidget;
    if (bEndingOptionsUI || IsActorBeingDestroyed() || !IsValid(Widget) || Widget->IsRemovingFromParent()
        || !IsValid(Controller) || !Controller->IsLocalController() || !Controller->GetLocalPlayer()
        || Widget->GetOwningPlayer() != Controller || !World || World->bIsTearingDown
        || Controller->IsActorBeingDestroyed()) { return; }
    TStrongObjectPtr<ARPGHUD> KeepHUDAlive(this);
    TStrongObjectPtr<APlayerController> KeepControllerAlive(Controller);
    TStrongObjectPtr<URPGOptionMenu> KeepAlive(Widget);
    TStrongObjectPtr<UWorld> KeepWorldAlive(World);
    const uint64 Revision = OptionMenuProviderRevision;
    if (!Widget->IsInViewport() && !Widget->AddToPlayerScreen(10)) { return; }
    if (Revision != OptionMenuProviderRevision || OptionMenuWidget != Widget || bEndingOptionsUI
        || Widget->IsRemovingFromParent() || GetWorld() != World || World->bIsTearingDown
        || GetOwningPlayerController() != Controller || Widget->GetOwningPlayer() != Controller) { return; }
    bIsOptionMenuVisible = true;
    Widget->SetVisibility(ESlateVisibility::Visible);
    if (Revision == OptionMenuProviderRevision && OptionMenuWidget == Widget
        && Widget->IsInViewport() && Widget->IsVisible() && !bEndingOptionsUI
        && GetOwningPlayerController() == Controller && Widget->GetOwningPlayer() == Controller)
    {
        Widget->OnMenuShown();
    }
    if (Revision == OptionMenuProviderRevision && OptionMenuWidget == Widget)
    {
        bIsOptionMenuVisible = Widget->IsInViewport() && Widget->IsVisible();
    }
}

void ARPGHUD::HideOptionMenu()
{
    bIsOptionMenuVisible = false;
    if (IsValid(OptionMenuWidget))
    {
        TStrongObjectPtr<ARPGHUD> KeepHUDAlive(this);
        URPGOptionMenu* Widget = OptionMenuWidget;
        TStrongObjectPtr<URPGOptionMenu> KeepAlive(Widget);
        const uint64 Revision = OptionMenuProviderRevision;
        const TWeakObjectPtr<APlayerController> Controller(GetOwningPlayerController());
        const TWeakObjectPtr<UWorld> World(GetWorld());
        Widget->OnMenuHidden();
        if (Revision == OptionMenuProviderRevision && OptionMenuWidget == Widget
            && GetOwningPlayerController() == Controller.Get() && GetWorld() == World.Get())
        {
            Widget->SetVisibility(ESlateVisibility::Collapsed);
        }
    }
}

void ARPGHUD::ToggleOptionMenu()
{
    if (bIsOptionMenuVisible && IsValid(OptionMenuWidget)
        && OptionMenuWidget->IsInViewport() && OptionMenuWidget->IsVisible())
    {
        HideOptionMenu();
    }
    else
    {
        DisplayOptionMenu();
    }
}

void ARPGHUD::ShowInteractionWidget()
{
	if (InteractionWidget)
	{
		InteractionWidget->SetVisibility(ESlateVisibility::Visible);
	}
}

void ARPGHUD::HideInteractionWidget()
{
	if (InteractionWidget)
	{
		InteractionWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
}

//void ARPGHUD::UpdateInteractionWidget(const FInteractableData* InteractableData)
//{
//	if (InteractionWidget)
//	{
//		if (InteractionWidget->GetVisibility()==ESlateVisibility::Collapsed)
//		{
//			InteractionWidget->SetVisibility(ESlateVisibility::Visible);
//		}
//
//		InteractionWidget->UpdateWidget(InteractableData);
//	}
//}
