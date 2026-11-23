#include "UI/Riding/RPGRidingPreferencesWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"

TSharedRef<SWidget> URPGRidingPreferencesWidget::RebuildWidget()
{
    if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"));
    if (!WidgetTree->RootWidget)
    {
        Group = WidgetTree->ConstructWidget<UVerticalBox>(); WidgetTree->RootWidget = Group;
        UHorizontalBox* Line = nullptr;
        auto NewLine = [&]
        {
            Line = WidgetTree->ConstructWidget<UHorizontalBox>();
            Group->AddChildToVerticalBox(Line)->SetPadding(FMargin(0,2));
        };
        auto Text = [&](const FText& Value)
        {
            auto* W = WidgetTree->ConstructWidget<UTextBlock>();
            W->SetAutoWrapText(true);
            W->SetFontSize(11);
            W->SetText(Value);
            Group->AddChild(W);
            return W;
        };
        auto Button = [&](const TCHAR* Name, const FText& Value)
        {
            auto* W = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
            auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
            Label->SetFontSize(12);
            Label->SetText(Value);
            W->SetContent(Label);
            W->SetBackgroundColor(FLinearColor(.055f,.060f,.068f));
            auto* Cell = Line->AddChildToHorizontalBox(W);
            Cell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            Cell->SetPadding(FMargin(1,0));
            return W;
        };
        auto Combo = [&]
        {
            auto* W = WidgetTree->ConstructWidget<UComboBoxString>();
            Line->AddChildToHorizontalBox(W)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            return W;
        };
        NewLine();
        DefaultButton = Button(TEXT("RidingPreferencesDefault"), NSLOCTEXT("RidingPreferences","Default","기본 탈것 지정"));
        DefaultButton->OnClicked.AddUniqueDynamic(this,&ThisClass::SetDefault);
        FavoriteButton = Button(TEXT("RidingPreferencesFavorite"), FText::GetEmpty());
        FavoriteButtonText = CastChecked<UTextBlock>(FavoriteButton->GetContent());
        Favorites = Text(FText::GetEmpty());
        Text(NSLOCTEXT("RidingPreferences","ShortcutLabel","빠른 호출 방식"));
        NewLine();
        ModeBox = Combo();
        ModeBox->AddOption(NSLOCTEXT("RidingPreferences", "SelectedMode", "선택 탈것 호출").ToString());
        ModeBox->AddOption(NSLOCTEXT("RidingPreferences", "SpecificLabel", "지정 탈것 호출").ToString());
        ModeBox->AddOption(NSLOCTEXT("RidingPreferences", "RandomMode", "즐겨찾기 무작위 호출").ToString()); SpecificBox = Combo();
        NewLine();
        ClearButton = Button(TEXT("RidingPreferencesClear"), NSLOCTEXT("RidingPreferences", "ClearShort", "전체 해제"));
        ResetButton = Button(TEXT("RidingPreferencesReset"), NSLOCTEXT("RidingPreferences", "Undo", "편집 되돌리기"));
        ReviewButton = Button(TEXT("RidingPreferencesReview"), NSLOCTEXT("RidingPreferences", "ReviewShort", "변경 내용 확인")); ReviewText = Text(FText::GetEmpty());
        NewLine();
        ConfirmButton = Button(TEXT("RidingPreferencesConfirm"), NSLOCTEXT("RidingPreferences", "Confirm", "위 설정으로 저장"));
        DismissButton = Button(TEXT("RidingPreferencesDismiss"), NSLOCTEXT("RidingPreferences", "Dismiss", "저장 확인 취소"));
        Status = Text(FText::GetEmpty());
        NewLine();
        ReloadButton = Button(TEXT("RidingPreferencesReload"), NSLOCTEXT("RidingPreferences","Refresh","설정 다시 불러오기"));
        RecoverButton = Button(TEXT("RidingPreferencesRecover"), NSLOCTEXT("RidingPreferences", "Recover", "원래 요청의 처리 결과 확인"));
        RetryButton = Button(TEXT("RidingPreferencesRetry"), NSLOCTEXT("RidingPreferences", "Retry", "같은 요청 다시 보내기"));
        StorageButton = Button(TEXT("RidingPreferencesStorage"), NSLOCTEXT("RidingPreferences", "StorageButton", "원래 요청의 저장 상태 재확인"));
        AssetsButton = Button(TEXT("RidingPreferencesAssets"), NSLOCTEXT("RidingPreferences", "Assets", "최신 보유 정보 다시 확인"));
        ReloadButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Reload); FavoriteButton->OnClicked.AddUniqueDynamic(this, &ThisClass::ToggleFavorite);
        ModeBox->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::ModeChanged); SpecificBox->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::SpecificChanged);
        ClearButton->OnClicked.AddUniqueDynamic(this, &ThisClass::ClearAll); ResetButton->OnClicked.AddUniqueDynamic(this, &ThisClass::ResetDraft);
        ReviewButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Review); ConfirmButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Confirm); DismissButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Dismiss);
        RecoverButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Recover); RetryButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Retry);
        StorageButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Storage); AssetsButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Assets);
    }
    RefreshView(); return Super::RebuildWidget();
}
void URPGRidingPreferencesWidget::SetOwner(TSharedRef<FRPGRidingPreferencesClient> InOwner)
{
    if (Owner.Pin() == InOwner && !bClosing) return;
    DetachOwner(); Owner = InOwner; bClosing = false; Change = InOwner->OnChanged.AddUObject(this, &ThisClass::RefreshView);
    if (!Tick.IsValid()) Tick = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::TickView), .25f); RefreshView();
}
void URPGRidingPreferencesWidget::DetachOwner()
{
    bClosing = true; if (const auto C = Owner.Pin()) { C->OnChanged.Remove(Change); const auto V = C->GetView(); C->ResetDraft(V.Snapshot); C->Dismiss(); }
    Change.Reset(); FTSTicker::RemoveTicker(Tick); Tick.Reset(); Owner.Reset(); Snapshot.Invalidate(); Consent.Invalidate(); FavoriteTarget.Reset();
}
void URPGRidingPreferencesWidget::NativeConstruct() { Super::NativeConstruct(); RefreshView(); }
void URPGRidingPreferencesWidget::NativeDestruct() { DetachOwner(); Super::NativeDestruct(); }
bool URPGRidingPreferencesWidget::TickView(float) { if (bClosing) return false; RefreshView(); return true; }
void URPGRidingPreferencesWidget::RefreshView()
{
    if (bClosing || bUpdating || !Group) return; TGuardValue<bool> Guard(bUpdating, true);
    const auto C = Owner.Pin(); const auto V = C ? C->GetView() : FRPGRidingPreferencesView{};
    Group->SetVisibility(V.bAvailable ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); if (!V.bAvailable) return;
    Status->SetText(V.Status); ReloadButton->SetIsEnabled(V.bCanReload);
    Snapshot = V.Snapshot;
    ReloadButton->SetVisibility(!V.bCanEdit && V.bCanReload ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    bool Rebuild = MountIds.Num() != V.Choices.Num();
    if (!Rebuild) for (int32 I = 0; I < MountIds.Num(); ++I) if (MountIds[I] != V.Choices[I].Id || SpecificBox->GetOptionAtIndex(I + 1) != V.Choices[I].Label.ToString()) { Rebuild = true; break; }
    if (Rebuild)
    {
        MountIds.Reset(); SpecificBox->ClearOptions();
        SpecificBox->AddOption(NSLOCTEXT("RidingPreferences", "PickSpecific", "호출할 탈것 선택").ToString());
        for (const auto& Choice : V.Choices) { MountIds.Add(Choice.Id); const auto N = Choice.Label.ToString(); SpecificBox->AddOption(N); }
    }
    if (SpecificBox->GetOptionCount() == 0) SpecificBox->AddOption(NSLOCTEXT("RidingPreferences", "PickSpecific", "호출할 탈것 선택").ToString());
    const bool bDefault = !FavoriteTarget.IsEmpty() && V.Draft.SelectedMountId == FavoriteTarget;
    DefaultButton->SetIsEnabled(V.bCanEdit && MountIds.Contains(FavoriteTarget));
    CastChecked<UTextBlock>(DefaultButton->GetContent())->SetText(bDefault
        ? NSLOCTEXT("RidingPreferences","UnsetDefault","기본 탈것 해제")
        : NSLOCTEXT("RidingPreferences","Default","기본 탈것 지정"));
    const int32 Mode = V.Draft.Shortcut.Kind == ERPGRidingSelectorKind::Specific ? 1 : V.Draft.Shortcut.Kind == ERPGRidingSelectorKind::RandomFavorites ? 2 : 0;
    ModeBox->SetSelectedIndex(Mode); SpecificBox->SetSelectedIndex(MountIds.IndexOfByKey(V.Draft.Shortcut.MountId) + 1);
    ModeBox->SetIsEnabled(V.bCanEdit); SpecificBox->SetIsEnabled(V.bCanEdit);
    SpecificBox->SetVisibility(Mode == 1 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    FavoriteButton->SetIsEnabled(V.bCanEdit && MountIds.Contains(FavoriteTarget));
    FavoriteButtonText->SetText(V.Draft.Favorites.Contains(FavoriteTarget) ? NSLOCTEXT("RidingPreferences", "RemoveStar", "즐겨찾기 해제") : NSLOCTEXT("RidingPreferences", "AddStar", "즐겨찾기 추가"));
    Favorites->SetText(V.FavoritesText); ClearButton->SetIsEnabled(V.bCanEdit); ResetButton->SetIsEnabled(V.bCanEdit && V.bDirty); ReviewButton->SetIsEnabled(V.bCanReview);
    Consent = V.Review; ReviewText->SetText(V.ReviewText); ConfirmButton->SetIsEnabled(V.bCanConfirm);
    ConfirmButton->SetVisibility(Consent.IsValid() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); DismissButton->SetVisibility(Consent.IsValid() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    RecoverButton->SetVisibility(V.bCanRecover ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); RetryButton->SetVisibility(V.bCanRetry ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    StorageButton->SetVisibility(V.bCanStorage ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); AssetsButton->SetVisibility(V.bCanRefreshAssets ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}
void URPGRidingPreferencesWidget::Reload() { if (const auto C = Owner.Pin(); C && !bClosing) C->Reload(); RefreshView(); }
void URPGRidingPreferencesWidget::BrowseMount(const FString& Id)
{
    FavoriteTarget = Id;
    RefreshView();
}
void URPGRidingPreferencesWidget::SetDefault()
{
    if (const auto C = Owner.Pin(); C && !bClosing && MountIds.Contains(FavoriteTarget))
    {
        C->Select(Snapshot, C->GetView().Draft.SelectedMountId == FavoriteTarget ? FString() : FavoriteTarget);
    }
    RefreshView();
}
void URPGRidingPreferencesWidget::ModeChanged(FString, ESelectInfo::Type)
{ if (bClosing || bUpdating) return; const int32 I = ModeBox->GetSelectedIndex(); if (I < 0 || I > 2) return; if (const auto C = Owner.Pin()) C->SetMode(Snapshot, I == 1 ? ERPGRidingSelectorKind::Specific : I == 2 ? ERPGRidingSelectorKind::RandomFavorites : ERPGRidingSelectorKind::Selected); RefreshView(); }
void URPGRidingPreferencesWidget::SpecificChanged(FString, ESelectInfo::Type)
{ if (bClosing || bUpdating) return; const int32 I = SpecificBox->GetSelectedIndex() - 1; if (const auto C = Owner.Pin()) C->SetSpecific(Snapshot, MountIds.IsValidIndex(I) ? MountIds[I] : FString()); RefreshView(); }
void URPGRidingPreferencesWidget::ToggleFavorite() { if (const auto C = Owner.Pin(); C && !bClosing) C->ToggleFavorite(Snapshot, FavoriteTarget); RefreshView(); }
void URPGRidingPreferencesWidget::ClearAll() { if (const auto C = Owner.Pin(); C && !bClosing) C->ClearAll(Snapshot); RefreshView(); }
void URPGRidingPreferencesWidget::ResetDraft() { if (const auto C = Owner.Pin(); C && !bClosing) C->ResetDraft(Snapshot); RefreshView(); }
void URPGRidingPreferencesWidget::Review() { if (const auto C = Owner.Pin(); C && !bClosing) C->Review(Snapshot); RefreshView(); }
void URPGRidingPreferencesWidget::Confirm() { if (const auto C = Owner.Pin(); C && !bClosing) C->Confirm(Consent); RefreshView(); }
void URPGRidingPreferencesWidget::Dismiss() { if (const auto C = Owner.Pin()) C->Dismiss(); RefreshView(); }
void URPGRidingPreferencesWidget::Recover() { if (const auto C = Owner.Pin(); C && !bClosing) C->Recover(); RefreshView(); }
void URPGRidingPreferencesWidget::Retry() { if (const auto C = Owner.Pin(); C && !bClosing) C->Retry(); RefreshView(); }
void URPGRidingPreferencesWidget::Storage() { if (const auto C = Owner.Pin(); C && !bClosing) C->RetryStorage(); RefreshView(); }
void URPGRidingPreferencesWidget::Assets() { if (const auto C = Owner.Pin(); C && !bClosing) C->RefreshAssets(); RefreshView(); }
