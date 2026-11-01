// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Option/GraphicOptionMenu.h"

#include "UI/Option/RPGGraphicsOptionsRuntime.h"
#include "Settings/RPGGameUserSettings.h"
#include "Components/ComboBoxString.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Styling/CoreStyle.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

UGraphicOptionMenu::UGraphicOptionMenu()
{
}

TSharedRef<SWidget> UGraphicOptionMenu::RebuildWidget()
{
    TSharedRef<SWidget> AuthoredContent = Super::RebuildWidget();
    if (IsDesignTime()) { return AuthoredContent; }

    const TWeakObjectPtr<UGraphicOptionMenu> WeakThis(this);
    TSharedPtr<SButton> ApplyButton;
    TSharedPtr<SButton> ConfirmButton;
    TSharedPtr<SHorizontalBox> ConfirmationActions;
    TSharedRef<SWidget> Result = SNew(SVerticalBox)
        + SVerticalBox::Slot().FillHeight(1.f)
        [
            SNew(SBox)
            .IsEnabled_Lambda([WeakThis]()
            {
                const UGraphicOptionMenu* Menu = WeakThis.Get();
                return Menu && Menu->CanInteractWithOptions();
            })
            [
                AuthoredContent
            ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))
        [
            SNew(SVerticalBox)
            .Visibility_Lambda([WeakThis]()
            {
                const UGraphicOptionMenu* Menu = WeakThis.Get();
                return Menu && (Menu->bShowNativeGraphicsActions || Menu->IsAwaitingDisplayConfirmation())
                    ? EVisibility::Visible : EVisibility::Collapsed;
            })
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SHorizontalBox)
                .Visibility_Lambda([WeakThis]()
                {
                    const UGraphicOptionMenu* Menu = WeakThis.Get();
                    return Menu && Menu->bShowNativeGraphicsActions && !Menu->IsAwaitingDisplayConfirmation()
                        ? EVisibility::Visible : EVisibility::Collapsed;
                })
                .IsEnabled_Lambda([WeakThis]()
                {
                    const UGraphicOptionMenu* Menu = WeakThis.Get();
                    return Menu && Menu->CanInteractWithOptions();
                })
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SAssignNew(ApplyButton, SButton)
                    .Text(NSLOCTEXT("RPGGraphicsOptions", "ApplyButton", "적용"))
                    .ContentPadding(FMargin(14.f, 6.f))
                    .OnClicked_UObject(this, &ThisClass::HandleApplyClicked)
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(FMargin(8.f, 0.f, 0.f, 0.f))
                [
                    SNew(SButton)
                    .Text(NSLOCTEXT("RPGGraphicsOptions", "DiscardButton", "변경 취소"))
                    .ToolTipText(NSLOCTEXT("RPGGraphicsOptions", "DiscardTooltip", "선택한 값을 현재 적용 중인 설정으로 되돌립니다."))
                    .ContentPadding(FMargin(14.f, 6.f))
                    .OnClicked_UObject(this, &ThisClass::HandleDiscardClicked)
                ]
            ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SAssignNew(ConfirmationActions, SHorizontalBox)
                .Visibility_Lambda([WeakThis]()
                {
                    const UGraphicOptionMenu* Menu = WeakThis.Get();
                    return Menu && Menu->IsAwaitingDisplayConfirmation() ? EVisibility::Visible : EVisibility::Collapsed;
                })
                .IsEnabled_Lambda([WeakThis]()
                {
                    const UGraphicOptionMenu* Menu = WeakThis.Get();
                    return Menu && Menu->CanResolveDisplayConfirmation();
                })
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SAssignNew(ConfirmButton, SButton)
                    .Text(NSLOCTEXT("RPGGraphicsOptions", "KeepDisplay", "이 설정 유지"))
                    .ContentPadding(FMargin(14.f, 6.f))
                    .OnClicked_UObject(this, &ThisClass::HandleConfirmClicked)
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(FMargin(8.f, 0.f, 0.f, 0.f))
                [
                    SNew(SButton)
                    .Text(NSLOCTEXT("RPGGraphicsOptions", "RevertDisplay", "이전 설정으로"))
                    .ContentPadding(FMargin(14.f, 6.f))
                    .OnClicked_UObject(this, &ThisClass::HandleRevertClicked)
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 0.f))
            [
                SNew(STextBlock)
                .Text_Lambda([WeakThis]()
                {
                    const UGraphicOptionMenu* Menu = WeakThis.Get();
                    return Menu ? Menu->GetApplyStatusMessage() : FText::GetEmpty();
                })
                .Visibility_Lambda([WeakThis]()
                {
                    const UGraphicOptionMenu* Menu = WeakThis.Get();
                    return Menu && !Menu->GetApplyStatusMessage().IsEmpty()
                        ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
                })
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
                .ColorAndOpacity_Lambda([WeakThis]()
                {
                    const UGraphicOptionMenu* Menu = WeakThis.Get();
                    return Menu && Menu->bLastApplyFailed
                        ? FSlateColor(FLinearColor(1.f, 0.35f, 0.3f)) : FSlateColor::UseForeground();
                })
                .AutoWrapText(true)
            ]
        ];
    NativeApplyFocusTarget = ApplyButton;
    NativeConfirmFocusTarget = ConfirmButton;
    NativeConfirmationActions = ConfirmationActions;
    return Result;
}

bool UGraphicOptionMenu::CanUseOptions() const
{
    const APlayerController* Controller = GetOwningPlayer();
    const UWorld* World = GetWorld();
    return bMenuConstructed && !bSynchronizingOptions && !bApplyingOptions
        && !IsRemovingFromParent() && !IsDesignTime()
        && IsValid(Controller) && Controller->IsLocalController() && Controller->GetLocalPlayer()
        && IsValid(World) && !World->bIsTearingDown;
}

bool UGraphicOptionMenu::CanInteractWithOptions() const
{
    return CanUseOptions() && !IsAwaitingDisplayConfirmation();
}

bool UGraphicOptionMenu::CanResolveDisplayConfirmation() const
{
    return CanUseOptions() && IsAwaitingDisplayConfirmation();
}

bool UGraphicOptionMenu::IsAwaitingDisplayConfirmation() const
{
    const URPGGameUserSettings* Settings = PreviewSettings.Get();
    return Settings && Settings->IsGraphicsPreviewOwner(this);
}

int32 UGraphicOptionMenu::GetDisplayConfirmationSecondsRemaining() const
{
    const URPGGameUserSettings* Settings = PreviewSettings.Get();
    return Settings ? Settings->GetGraphicsPreviewSecondsRemaining(this) : 0;
}

FText UGraphicOptionMenu::GetApplyStatusMessage() const
{
    return IsAwaitingDisplayConfirmation()
        ? FText::Format(NSLOCTEXT("RPGGraphicsOptions", "DisplayCountdown",
            "이 화면 설정을 유지할까요? {0}초 후 이전 설정으로 돌아갑니다."),
            FText::AsNumber(GetDisplayConfirmationSecondsRemaining()))
        : LastApplyMessage;
}

void UGraphicOptionMenu::NativeConstruct()
{
    bMenuConstructed = true;
    if (VisibilityChangedHandle.IsValid()) { OnNativeVisibilityChanged.Remove(VisibilityChangedHandle); }
    VisibilityChangedHandle = OnNativeVisibilityChanged.AddUObject(this, &ThisClass::HandleOptionsVisibilityChanged);
    Super::NativeConstruct();
    InitializeOptions();
}

void UGraphicOptionMenu::NativeDestruct()
{
    bMenuConstructed = false;
    CancelPendingGraphicsChange();
    OnNativeVisibilityChanged.Remove(VisibilityChangedHandle);
    VisibilityChangedHandle.Reset();
    Super::NativeDestruct();
}

void UGraphicOptionMenu::NativeOnDeactivated()
{
    CancelPendingGraphicsChange();
    Super::NativeOnDeactivated();
}

void UGraphicOptionMenu::RemoveFromParent()
{
    CancelPendingGraphicsChange();
    Super::RemoveFromParent();
}

void UGraphicOptionMenu::HandleOptionsVisibilityChanged(ESlateVisibility NewVisibility)
{
    if (NewVisibility == ESlateVisibility::Hidden || NewVisibility == ESlateVisibility::Collapsed)
    {
        CancelPendingGraphicsChange();
    }
}

void UGraphicOptionMenu::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    TStrongObjectPtr<UGraphicOptionMenu> KeepAlive(this);
    Super::NativeTick(MyGeometry, InDeltaTime);
    if (bAwaitingConfirmation && !IsAwaitingDisplayConfirmation() && CanUseOptions())
    {
        bAwaitingConfirmation = false;
        PreviewSettings.Reset();
        InitializeOptions();
        LastApplyMessage = NSLOCTEXT("RPGGraphicsOptions", "DisplayConfirmationEnded",
            "화면 변경 확인이 종료되었습니다. 현재 설정을 다시 불러왔습니다.");
        FocusNativeAction(false);
    }
}

void UGraphicOptionMenu::InitializeOptions()
{
    if (!CanInteractWithOptions()) { return; }
    TStrongObjectPtr<UGraphicOptionMenu> KeepAlive(this);
    TGuardValue<bool> Guard(bSynchronizingOptions, true);
    if (IRPGGraphicsOptionsRuntime* Runtime = IRPGGraphicsOptionsRuntime::Get())
    {
        FRPGGraphicsOptionsModel Model;
        Runtime->BuildOptions(Model);
        if (!bMenuConstructed || IsRemovingFromParent()) { return; }
        IRPGGraphicsOptionsRuntime::SynchronizeMenuControls(Model, ResolutionComboBox, WindowModeComboBox, VSyncCheckBox);
        if (!bMenuConstructed || IsRemovingFromParent()) { return; }
        LastApplyMessage = FText::GetEmpty();
        bLastApplyFailed = false;
    }
    else
    {
        LastApplyMessage = NSLOCTEXT("RPGGraphicsOptions", "RuntimeUnavailable", "그래픽 설정이 아직 준비되지 않았습니다.");
        bLastApplyFailed = true;
    }
}

void UGraphicOptionMenu::OnApplyButtonClicked()
{
    if (!CanInteractWithOptions()) { return; }
    TStrongObjectPtr<UGraphicOptionMenu> KeepAlive(this);
    {
        TGuardValue<bool> Guard(bApplyingOptions, true);
        PreviewSettings = URPGGameUserSettings::GetRPGGameUserSettings();
        IRPGGraphicsOptionsRuntime* Runtime = IRPGGraphicsOptionsRuntime::Get();
        FText ApplyError;
        bool bNeedsConfirmation = false;
        FRPGGraphicsSettings Requested;
        const bool bApplied = Runtime
            && Runtime->ReadMenuSelection(ResolutionComboBox, WindowModeComboBox, VSyncCheckBox, Requested, ApplyError)
            && Runtime->TryPreviewSettings(Requested, this, bNeedsConfirmation, ApplyError);
        if (!bMenuConstructed || IsRemovingFromParent())
        {
            CancelPendingGraphicsChange();
            return;
        }
        bAwaitingConfirmation = bApplied && bNeedsConfirmation && IsAwaitingDisplayConfirmation();
        if (!IsAwaitingDisplayConfirmation()) { PreviewSettings.Reset(); }
        LastApplyMessage = bApplied
            ? (bAwaitingConfirmation ? FText::GetEmpty()
                : NSLOCTEXT("RPGGraphicsOptions", "AppliedMessage", "그래픽 설정을 적용하고 저장했습니다."))
            : (ApplyError.IsEmpty()
                ? NSLOCTEXT("RPGGraphicsOptions", "RuntimeUnavailable", "그래픽 설정이 아직 준비되지 않았습니다.") : ApplyError);
        bLastApplyFailed = !bApplied;
    }
    if (IsAwaitingDisplayConfirmation()) { FocusNativeAction(true); }
    else
    {
        const FText ResultMessage = LastApplyMessage;
        const bool bApplyFailed = bLastApplyFailed;
        InitializeOptions();
        LastApplyMessage = ResultMessage;
        bLastApplyFailed = bApplyFailed;
    }
}

void UGraphicOptionMenu::ConfirmDisplayChanges()
{
    if (!CanResolveDisplayConfirmation()) { return; }
    TStrongObjectPtr<UGraphicOptionMenu> KeepAlive(this);
    FText ApplyError;
    bool bConfirmed = false;
    {
        TGuardValue<bool> Guard(bApplyingOptions, true);
        IRPGGraphicsOptionsRuntime* Runtime = IRPGGraphicsOptionsRuntime::Get();
        bConfirmed = Runtime && Runtime->TryConfirmPreview(this, ApplyError);
    }
    bAwaitingConfirmation = IsAwaitingDisplayConfirmation();
    if (!bAwaitingConfirmation)
    {
        PreviewSettings.Reset();
        InitializeOptions();
        FocusNativeAction(false);
    }
    LastApplyMessage = bConfirmed
        ? NSLOCTEXT("RPGGraphicsOptions", "ConfirmedMessage", "화면 설정을 유지하고 저장했습니다.")
        : (ApplyError.IsEmpty()
            ? NSLOCTEXT("RPGGraphicsOptions", "RuntimeUnavailable", "그래픽 설정이 아직 준비되지 않았습니다.") : ApplyError);
    bLastApplyFailed = !bConfirmed;
}

void UGraphicOptionMenu::CancelPendingGraphicsChange()
{
    TStrongObjectPtr<UGraphicOptionMenu> KeepAlive(
        !HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) && !IsUnreachable() ? this : nullptr);
    if (URPGGameUserSettings* Settings = PreviewSettings.Get())
    {
        Settings->CancelGraphicsPreview(this);
    }
    if (!IsAwaitingDisplayConfirmation())
    {
        bAwaitingConfirmation = false;
        PreviewSettings.Reset();
    }
}

void UGraphicOptionMenu::RevertDisplayChanges()
{
    if (!CanResolveDisplayConfirmation()) { return; }
    TStrongObjectPtr<UGraphicOptionMenu> KeepAlive(this);
    {
        TGuardValue<bool> Guard(bApplyingOptions, true);
        CancelPendingGraphicsChange();
    }
    InitializeOptions();
    LastApplyMessage = NSLOCTEXT("RPGGraphicsOptions", "DisplayReverted", "이전 화면 설정으로 되돌렸습니다.");
    bLastApplyFailed = false;
    FocusNativeAction(false);
}

void UGraphicOptionMenu::FocusNativeAction(bool bConfirmation)
{
    if (!CanUseOptions() || !FSlateApplication::IsInitialized()) { return; }
    ULocalPlayer* Player = GetOwningLocalPlayer();
    const TSharedPtr<FSlateUser> User = Player ? Player->GetSlateUser() : nullptr;
    const TSharedPtr<SWidget> Root = GetCachedWidget();
    const TSharedPtr<SWidget> Target = bConfirmation ? NativeConfirmFocusTarget.Pin()
        : (bShowNativeGraphicsActions ? NativeApplyFocusTarget.Pin()
            : (IsValid(ResolutionComboBox) ? ResolutionComboBox->GetCachedWidget() : nullptr));
    const TSharedPtr<SWidget> ConfirmationActions = NativeConfirmationActions.Pin();
    if (!User || !Root || !Target || !Target->IsEnabled()
        || (!bConfirmation && (!ConfirmationActions || !User->IsWidgetInFocusPath(ConfirmationActions)))
        || (User->GetFocusedWidget() && !User->IsWidgetInFocusPath(Root))) { return; }
    FSlateApplication::Get().SetUserFocus(User->GetUserIndex(), Target, EFocusCause::SetDirectly);
}

FReply UGraphicOptionMenu::HandleApplyClicked()
{
    OnApplyButtonClicked();
    return FReply::Handled();
}

FReply UGraphicOptionMenu::HandleDiscardClicked()
{
    InitializeOptions();
    return FReply::Handled();
}

FReply UGraphicOptionMenu::HandleConfirmClicked()
{
    ConfirmDisplayChanges();
    return FReply::Handled();
}

FReply UGraphicOptionMenu::HandleRevertClicked()
{
    RevertDisplayChanges();
    return FReply::Handled();
}
