#include "UI/Riding/RPGRidingPreferencesWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"

TSharedRef<SWidget> URPGRidingPreferencesWidget::RebuildWidget()
{
    if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"));
    if (!WidgetTree->RootWidget)
    {
        Group = WidgetTree->ConstructWidget<UVerticalBox>(); WidgetTree->RootWidget = Group;
        auto Text = [&](const FText& T) { auto* W = WidgetTree->ConstructWidget<UTextBlock>(); W->SetAutoWrapText(true); W->SetText(T); Group->AddChild(W); return W; };
        auto Button = [&](const TCHAR* Name, const FText& T) { auto* W = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name)); auto* Label = WidgetTree->ConstructWidget<UTextBlock>(); Label->SetText(T); W->SetContent(Label); Group->AddChild(W); return W; };
        auto Combo = [&] { auto* W = WidgetTree->ConstructWidget<UComboBoxString>(); Group->AddChild(W); return W; };
        Text(NSLOCTEXT("RidingPreferences", "Title", "탈것 선택·즐겨찾기·빠른 호출 설정")); Status = Text(FText::GetEmpty());
        ReloadButton = Button(TEXT("RidingPreferencesReload"), NSLOCTEXT("RidingPreferences", "Reload", "최신 보유 탈것과 설정 불러오기"));
        Text(NSLOCTEXT("RidingPreferences", "SelectedLabel", "선택 탈것")); SelectedBox = Combo();
        Text(NSLOCTEXT("RidingPreferences", "FavoritesLabel", "즐겨찾기 편집")); FavoriteBox = Combo();
        FavoriteButton = Button(TEXT("RidingPreferencesFavorite"), FText::GetEmpty()); FavoriteButtonText = CastChecked<UTextBlock>(FavoriteButton->GetContent());
        Favorites = Text(FText::GetEmpty()); Text(NSLOCTEXT("RidingPreferences", "ShortcutLabel", "빠른 호출 방식")); ModeBox = Combo();
        ModeBox->AddOption(NSLOCTEXT("RidingPreferences", "SelectedMode", "선택 탈것 호출").ToString());
        ModeBox->AddOption(NSLOCTEXT("RidingPreferences", "SpecificLabel", "지정 탈것 호출").ToString());
        ModeBox->AddOption(NSLOCTEXT("RidingPreferences", "RandomMode", "즐겨찾기 무작위 호출").ToString()); SpecificBox = Combo();
        ClearButton = Button(TEXT("RidingPreferencesClear"), NSLOCTEXT("RidingPreferences", "Clear", "선택·즐겨찾기 전체 해제"));
        ResetButton = Button(TEXT("RidingPreferencesReset"), NSLOCTEXT("RidingPreferences", "Reset", "불러온 설정으로 편집 되돌리기"));
        ReviewButton = Button(TEXT("RidingPreferencesReview"), NSLOCTEXT("RidingPreferences", "ReviewButton", "설정 변경 내용 확인")); ReviewText = Text(FText::GetEmpty());
        ConfirmButton = Button(TEXT("RidingPreferencesConfirm"), NSLOCTEXT("RidingPreferences", "Confirm", "위 설정으로 저장"));
        DismissButton = Button(TEXT("RidingPreferencesDismiss"), NSLOCTEXT("RidingPreferences", "Dismiss", "저장 확인 취소"));
        RecoverButton = Button(TEXT("RidingPreferencesRecover"), NSLOCTEXT("RidingPreferences", "Recover", "원래 요청의 처리 결과 확인"));
        RetryButton = Button(TEXT("RidingPreferencesRetry"), NSLOCTEXT("RidingPreferences", "Retry", "같은 요청 다시 보내기"));
        StorageButton = Button(TEXT("RidingPreferencesStorage"), NSLOCTEXT("RidingPreferences", "StorageButton", "원래 요청의 저장 상태 재확인"));
        AssetsButton = Button(TEXT("RidingPreferencesAssets"), NSLOCTEXT("RidingPreferences", "Assets", "최신 보유 정보 다시 확인"));
        ReloadButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Reload); FavoriteButton->OnClicked.AddUniqueDynamic(this, &ThisClass::ToggleFavorite);
        SelectedBox->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::SelectChanged); FavoriteBox->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::FavoriteChanged);
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
    if (Snapshot != V.Snapshot) { Snapshot = V.Snapshot; FavoriteTarget.Reset(); }
    bool Rebuild = MountIds.Num() != V.Choices.Num();
    if (!Rebuild) for (int32 I = 0; I < MountIds.Num(); ++I) if (MountIds[I] != V.Choices[I].Id || FavoriteBox->GetOptionAtIndex(I) != V.Choices[I].Label.ToString()) { Rebuild = true; break; }
    if (Rebuild)
    {
        MountIds.Reset(); SelectedBox->ClearOptions(); FavoriteBox->ClearOptions(); SpecificBox->ClearOptions();
        SelectedBox->AddOption(NSLOCTEXT("RidingPreferences", "None", "없음").ToString()); SpecificBox->AddOption(NSLOCTEXT("RidingPreferences", "PickSpecific", "호출할 탈것 선택").ToString());
        for (const auto& Choice : V.Choices) { MountIds.Add(Choice.Id); const auto N = Choice.Label.ToString(); SelectedBox->AddOption(N); FavoriteBox->AddOption(N); SpecificBox->AddOption(N); }
    }
    if (SelectedBox->GetOptionCount() == 0) SelectedBox->AddOption(NSLOCTEXT("RidingPreferences", "None", "없음").ToString());
    if (SpecificBox->GetOptionCount() == 0) SpecificBox->AddOption(NSLOCTEXT("RidingPreferences", "PickSpecific", "호출할 탈것 선택").ToString());
    SelectedBox->SetSelectedIndex(MountIds.IndexOfByKey(V.Draft.SelectedMountId) + 1);
    const int32 FavoriteIndex = MountIds.IndexOfByKey(FavoriteTarget);
    if (FavoriteIndex == INDEX_NONE) FavoriteBox->ClearSelection(); else FavoriteBox->SetSelectedIndex(FavoriteIndex);
    const int32 Mode = V.Draft.Shortcut.Kind == ERPGRidingSelectorKind::Specific ? 1 : V.Draft.Shortcut.Kind == ERPGRidingSelectorKind::RandomFavorites ? 2 : 0;
    ModeBox->SetSelectedIndex(Mode); SpecificBox->SetSelectedIndex(MountIds.IndexOfByKey(V.Draft.Shortcut.MountId) + 1);
    SelectedBox->SetIsEnabled(V.bCanEdit); FavoriteBox->SetIsEnabled(V.bCanEdit); ModeBox->SetIsEnabled(V.bCanEdit); SpecificBox->SetIsEnabled(V.bCanEdit);
    SpecificBox->SetVisibility(Mode == 1 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    FavoriteButton->SetIsEnabled(V.bCanEdit && MountIds.Contains(FavoriteTarget));
    FavoriteButtonText->SetText(V.Draft.Favorites.Contains(FavoriteTarget) ? NSLOCTEXT("RidingPreferences", "RemoveFavorite", "선택한 탈것을 즐겨찾기에서 제거") : NSLOCTEXT("RidingPreferences", "AddFavorite", "선택한 탈것을 즐겨찾기에 추가"));
    Favorites->SetText(V.FavoritesText); ClearButton->SetIsEnabled(V.bCanEdit); ResetButton->SetIsEnabled(V.bCanEdit && V.bDirty); ReviewButton->SetIsEnabled(V.bCanReview);
    Consent = V.Review; ReviewText->SetText(V.ReviewText); ConfirmButton->SetIsEnabled(V.bCanConfirm);
    ConfirmButton->SetVisibility(Consent.IsValid() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); DismissButton->SetVisibility(Consent.IsValid() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    RecoverButton->SetVisibility(V.bCanRecover ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); RetryButton->SetVisibility(V.bCanRetry ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    StorageButton->SetVisibility(V.bCanStorage ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); AssetsButton->SetVisibility(V.bCanRefreshAssets ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}
void URPGRidingPreferencesWidget::Reload() { if (const auto C = Owner.Pin(); C && !bClosing) C->Reload(); RefreshView(); }
void URPGRidingPreferencesWidget::SelectChanged(FString, ESelectInfo::Type)
{ if (bClosing || bUpdating) return; const int32 I = SelectedBox->GetSelectedIndex() - 1; if (const auto C = Owner.Pin()) C->Select(Snapshot, MountIds.IsValidIndex(I) ? MountIds[I] : FString()); RefreshView(); }
void URPGRidingPreferencesWidget::FavoriteChanged(FString, ESelectInfo::Type)
{ if (bClosing || bUpdating) return; const int32 I = FavoriteBox->GetSelectedIndex(); FavoriteTarget = MountIds.IsValidIndex(I) ? MountIds[I] : FString(); RefreshView(); }
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
