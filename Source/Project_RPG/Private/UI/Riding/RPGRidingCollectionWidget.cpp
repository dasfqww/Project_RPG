#include "UI/Riding/RPGRidingCollectionWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/EditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "InputCoreTypes.h"

TSharedRef<SWidget> URPGRidingCollectionWidget::RebuildWidget()
{
    if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"));
    if (!WidgetTree->RootWidget)
    {
        auto* Root = WidgetTree->ConstructWidget<UCanvasPanel>(); WidgetTree->RootWidget = Root;
        auto* Frame = WidgetTree->ConstructWidget<UBorder>(); Frame->SetBrushColor(FLinearColor(.025f, .035f, .045f, .98f)); Frame->SetPadding(FMargin(18));
        auto* CanvasSlot = Root->AddChildToCanvas(Frame); CanvasSlot->SetAnchors(FAnchors(.5f)); CanvasSlot->SetAlignment(FVector2D(.5f)); CanvasSlot->SetAutoSize(true);
        auto* Size = WidgetTree->ConstructWidget<USizeBox>(); Size->SetWidthOverride(700); Size->SetHeightOverride(520); Frame->SetContent(Size);
        auto* OuterScroll = WidgetTree->ConstructWidget<UScrollBox>(); Size->SetContent(OuterScroll);
        auto* List = WidgetTree->ConstructWidget<UVerticalBox>(); OuterScroll->AddChild(List);
        auto Text = [&](const FText& Value) { auto* W = WidgetTree->ConstructWidget<UTextBlock>(); W->SetText(Value); W->SetAutoWrapText(true); List->AddChild(W); return W; };
        auto Button = [&](const TCHAR* Name, const FText& Value) { auto* W = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name)); auto* Label = WidgetTree->ConstructWidget<UTextBlock>(); Label->SetText(Value); W->SetContent(Label); List->AddChild(W); return W; };
        Text(NSLOCTEXT("RidingCollection", "Title", "탈것 보관함"));
        Status = Text(FText::GetEmpty()); Settings = Text(FText::GetEmpty());
        ReloadButton = Button(TEXT("RidingCollectionReload"), NSLOCTEXT("RidingCollection", "Reload", "보유 탈것 불러오기"));
        Search = WidgetTree->ConstructWidget<UEditableTextBox>(); Search->SetHintText(NSLOCTEXT("RidingCollection", "Search", "탈것 이름 검색")); List->AddChild(Search);
        FavoritesOnly = WidgetTree->ConstructWidget<UCheckBox>(); auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
        Label->SetText(NSLOCTEXT("RidingCollection", "FavoritesOnly", "즐겨찾기만 보기")); FavoritesOnly->SetContent(Label); List->AddChild(FavoritesOnly);
        Empty = Text(FText::GetEmpty());
        auto* MountSize = WidgetTree->ConstructWidget<USizeBox>(); MountSize->SetHeightOverride(140); List->AddChild(MountSize);
        auto* Scroll = WidgetTree->ConstructWidget<UScrollBox>(); MountSize->SetContent(Scroll);
        Rows = WidgetTree->ConstructWidget<UVerticalBox>(); Scroll->AddChild(Rows);
        auto* MainList = List; RegistrationGroup = WidgetTree->ConstructWidget<UVerticalBox>(); List->AddChild(RegistrationGroup); List = RegistrationGroup;
        Text(NSLOCTEXT("RidingRegistration", "Title", "가방 물품으로 탈것 등록")); RegistrationStatus = Text(FText::GetEmpty());
        RegistrationReload = Button(TEXT("RidingRegistrationReload"), NSLOCTEXT("RidingRegistration", "Reload", "등록할 최신 물품 불러오기"));
        RegistrationRows = WidgetTree->ConstructWidget<UComboBoxString>(); List->AddChild(RegistrationRows);
        RegistrationReviewButton = Button(TEXT("RidingRegistrationReview"), NSLOCTEXT("RidingRegistration", "ReviewButton", "등록 내용 확인"));
        RegistrationConsent = Text(FText::GetEmpty());
        RegistrationConfirm = Button(TEXT("RidingRegistrationConfirm"), NSLOCTEXT("RidingRegistration", "ConfirmButton", "물품 1개를 소비하고 등록"));
        RegistrationDismiss = Button(TEXT("RidingRegistrationDismiss"), NSLOCTEXT("RidingRegistration", "Dismiss", "등록 확인 취소"));
        RegistrationRecover = Button(TEXT("RidingRegistrationRecover"), NSLOCTEXT("RidingRegistration", "Recover", "원래 요청의 처리 결과 확인"));
        RegistrationRetry = Button(TEXT("RidingRegistrationRetry"), NSLOCTEXT("RidingRegistration", "Retry", "같은 요청 다시 보내기"));
        RegistrationStorage = Button(TEXT("RidingRegistrationStorage"), NSLOCTEXT("RidingRegistration", "StorageButton", "원래 요청의 저장 상태 재확인"));
        RegistrationAssets = Button(TEXT("RidingRegistrationAssets"), NSLOCTEXT("RidingRegistration", "RefreshAssets", "최신 보유·가방 정보 다시 확인"));
        RegistrationReload->OnClicked.AddUniqueDynamic(this, &ThisClass::ReloadRegistration);
        RegistrationRows->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::RegistrationSelection);
        RegistrationReviewButton->OnClicked.AddUniqueDynamic(this, &ThisClass::ReviewRegistration);
        RegistrationConfirm->OnClicked.AddUniqueDynamic(this, &ThisClass::ConfirmRegistration);
        RegistrationDismiss->OnClicked.AddUniqueDynamic(this, &ThisClass::DismissRegistration);
        RegistrationRecover->OnClicked.AddUniqueDynamic(this, &ThisClass::RecoverRegistration);
        RegistrationRetry->OnClicked.AddUniqueDynamic(this, &ThisClass::RetryRegistration);
        RegistrationStorage->OnClicked.AddUniqueDynamic(this, &ThisClass::StorageRegistration);
        RegistrationAssets->OnClicked.AddUniqueDynamic(this, &ThisClass::RefreshRegistrationAssets);
        List = MainList;
        CloseButton = Button(TEXT("RidingCollectionClose"), NSLOCTEXT("RidingCollection", "Close", "닫기"));
        ReloadButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Reload); CloseButton->OnClicked.AddUniqueDynamic(this, &ThisClass::CloseScreen);
        Search->OnTextChanged.AddUniqueDynamic(this, &ThisClass::SearchChanged); FavoritesOnly->OnCheckStateChanged.AddUniqueDynamic(this, &ThisClass::FavoritesChanged);
    }
    SetIsFocusable(true); RowsKey.Reset(); RefreshView(); RefreshRegistration(); return Super::RebuildWidget();
}
void URPGRidingCollectionWidget::SetReadOwner(TSharedRef<FRPGRidingCollectionReader> InReader, FPresenter InPresenter)
{
    Detach(); Reader = InReader; Presenter = MoveTemp(InPresenter); BoundContext = InReader->GetContext(); bClosing = false;
    RowsKey.Reset(); Subscribe(); RefreshView();
}
void URPGRidingCollectionWidget::Subscribe()
{
    if (bClosing) return;
    if (const auto Owner = Reader.Pin()) { Owner->OnChanged.Remove(Changed); Changed = Owner->OnChanged.AddUObject(this, &ThisClass::RefreshView); }
    if (const auto Owner = Registration.Pin()) { Owner->OnChanged.Remove(RegistrationChanged); RegistrationChanged = Owner->OnChanged.AddUObject(this, &ThisClass::RefreshRegistration); }
    if (!TickHandle.IsValid()) TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::TickView), .25f);
}
void URPGRidingCollectionWidget::Detach()
{
    if (const auto Owner = Reader.Pin()) Owner->OnChanged.Remove(Changed); Changed.Reset();
    if (const auto Owner = Registration.Pin()) { Owner->OnChanged.Remove(RegistrationChanged); Owner->Dismiss(); } RegistrationChanged.Reset();
    FTSTicker::RemoveTicker(TickHandle); TickHandle.Reset();
}
void URPGRidingCollectionWidget::NativeConstruct()
{
    Super::NativeConstruct(); Subscribe(); RefreshView();
    if (CloseButton && GetOwningPlayer()) CloseButton->SetKeyboardFocus();
}
void URPGRidingCollectionWidget::NativeDestruct() { Detach(); Super::NativeDestruct(); }
bool URPGRidingCollectionWidget::TickView(float) { if (bClosing) return false; RefreshView(); RefreshRegistration(); return !bClosing; }
void URPGRidingCollectionWidget::Reload() { if (!bClosing) if (const auto Owner = Reader.Pin()) Owner->Read(); RefreshView(); }
void URPGRidingCollectionWidget::SearchChanged(const FText&) { RowsKey.Reset(); RefreshView(); }
void URPGRidingCollectionWidget::FavoritesChanged(bool) { RowsKey.Reset(); RefreshView(); }
void URPGRidingCollectionWidget::RefreshView()
{
    if (bClosing || bUpdating) return; TGuardValue<bool> Guard(bUpdating, true);
    const auto Owner = Reader.Pin(); const auto Current = Owner ? Owner->GetContext() : FRPGRidingCollectionContext{};
    if (!Owner || !Current.bEligible || !Current.SameIdentity(BoundContext)) { CloseScreen(); return; }
    if (!Status || !Rows) return;
    const auto State = Owner->GetState(); const auto* C = Owner->GetCollection(); const double Retry = Owner->GetRetryAfterSeconds();
    const bool bReady = C != nullptr;
    ReloadButton->SetIsEnabled(State != ERPGRidingCollectionReadState::Loading && Retry <= 0);
    Search->SetIsEnabled(bReady); FavoritesOnly->SetIsEnabled(bReady);
    FText Message = NSLOCTEXT("RidingCollection", "Prompt", "보유 탈것을 불러와 주세요.");
    if (State == ERPGRidingCollectionReadState::Loading) Message = NSLOCTEXT("RidingCollection", "Loading", "보유 탈것을 불러오는 중입니다.");
    else if (State == ERPGRidingCollectionReadState::Failed) Message = NSLOCTEXT("RidingCollection", "Failed", "탈것 정보를 불러오지 못했습니다. 다시 시도해 주세요.");
    else if (Retry > 0) Message = FText::Format(NSLOCTEXT("RidingCollection", "Retry", "{0}초 후 다시 불러올 수 있습니다."), FMath::CeilToInt(Retry));
    else if (bReady) Message = C->bCatalogReady ? NSLOCTEXT("RidingCollection", "Ready", "현재 캐릭터에 저장된 탈것 정보입니다.")
        : NSLOCTEXT("RidingCollection", "Disabled", "보유 내역입니다. 현재 탈것 등록과 설정 변경을 사용할 수 없습니다.");
    Status->SetText(Message); Settings->SetText(FText::GetEmpty());
    TMap<FString, FText> Names; FString Key;
    if (C)
    {
        for (int32 I = 0; I < C->Mounts.Num(); ++I)
        {
            const auto& M = C->Mounts[I]; FText Name = Presenter ? Presenter(M.MountId) : FText::GetEmpty();
            if (Name.IsEmpty()) Name = FText::Format(NSLOCTEXT("RidingCollection", "Unnamed", "탈것 {0}"), I + 1);
            Names.Add(M.MountId, Name); Key += M.MountId + TEXT("|") + Name.ToString() + TEXT("\n");
        }
        auto Name = [&](const FString& Id) { const auto* Value = Names.Find(Id); return Value ? *Value : NSLOCTEXT("RidingCollection", "None", "없음"); };
        FText Shortcut = NSLOCTEXT("RidingCollection", "SelectedShortcut", "선택 탈것 호출");
        if (C->Shortcut.Kind == ERPGRidingSelectorKind::Specific) Shortcut = Name(C->Shortcut.MountId);
        if (C->Shortcut.Kind == ERPGRidingSelectorKind::RandomFavorites) Shortcut = NSLOCTEXT("RidingCollection", "RandomShortcut", "즐겨찾기 무작위 호출");
        Settings->SetText(FText::Format(NSLOCTEXT("RidingCollection", "Settings", "선택 탈것: {0}\n빠른 호출: {1}"), Name(C->SelectedMountId), Shortcut));
        Key += LexToString(C->RosterRevision) + TEXT("/") + LexToString(C->PreferencesRevision);
    }
    const FString Filter = Search->GetText().ToString().TrimStartAndEnd(); const bool bFavorites = FavoritesOnly->IsChecked();
    Key += TEXT("/") + Filter + (bFavorites ? TEXT("/favorite") : TEXT("/all"));
    if (Key == RowsKey) return; RowsKey = Key; Rows->ClearChildren(); int32 Count = 0;
    if (C) for (const auto& M : C->Mounts)
    {
        const bool bFavorite = C->Favorites.Contains(M.MountId); const auto& Name = Names.FindChecked(M.MountId);
        if ((bFavorites && !bFavorite) || (!Filter.IsEmpty() && !Name.ToString().Contains(Filter))) continue;
        const FText Mark = C->SelectedMountId == M.MountId ? NSLOCTEXT("RidingCollection", "Selected", "선택됨") : NSLOCTEXT("RidingCollection", "Owned", "보유");
        auto* Text = WidgetTree->ConstructWidget<UTextBlock>(); Text->SetAutoWrapText(true);
        Text->SetText(FText::Format(NSLOCTEXT("RidingCollection", "Row", "{0} · {1}{2}"), Name, Mark,
            bFavorite ? NSLOCTEXT("RidingCollection", "Favorite", " · 즐겨찾기") : FText::GetEmpty()));
        Rows->AddChild(Text); ++Count;
    }
    Empty->SetText(!C ? FText::GetEmpty() : C->Mounts.IsEmpty() ? NSLOCTEXT("RidingCollection", "Empty", "보유한 탈것이 없습니다.")
        : Count == 0 ? NSLOCTEXT("RidingCollection", "FilteredEmpty", "검색 조건에 맞는 탈것이 없습니다.") : FText::GetEmpty());
}
void URPGRidingCollectionWidget::CloseScreen()
{
    if (bClosing) return; bClosing = true; Detach();
    if (Rows) Rows->ClearChildren(); if (Settings) Settings->SetText(FText::GetEmpty());
    Reader.Reset(); Presenter = {}; RowsKey.Reset(); SetVisibility(ESlateVisibility::Collapsed);
    Registration.Reset(); RegistrationIds.Reset(); RegistrationSnapshot.Invalidate(); RegistrationReview.Invalidate(); SelectedRegistration.Invalidate();
    if (IsInViewport() || GetParent()) RemoveFromParent(); OnClosed.Broadcast();
}
FReply URPGRidingCollectionWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{ if (Event.GetKey() == EKeys::Escape) { CloseScreen(); return FReply::Handled(); } return Super::NativeOnKeyDown(Geometry, Event); }

void URPGRidingCollectionWidget::SetRegistrationOwner(TSharedRef<FRPGRidingRegistrationClient> InOwner)
{
    if (const auto Old = Registration.Pin()) { Old->OnChanged.Remove(RegistrationChanged); Old->Dismiss(); }
    Registration = InOwner; RegistrationChanged = InOwner->OnChanged.AddUObject(this, &ThisClass::RefreshRegistration);
    RegistrationSnapshot.Invalidate(); SelectedRegistration.Invalidate(); RefreshRegistration();
}
void URPGRidingCollectionWidget::RefreshRegistration()
{
    if (bClosing || bRegistrationUpdating || !RegistrationGroup) return; TGuardValue<bool> Guard(bRegistrationUpdating, true);
    const auto Owner = Registration.Pin(); const auto V = Owner ? Owner->GetView() : FRPGRidingRegistrationView{};
    RegistrationGroup->SetVisibility(V.bAvailable ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); if (!V.bAvailable) return;
    RegistrationStatus->SetText(V.Status); RegistrationReload->SetIsEnabled(V.bCanReload);
    if (V.Snapshot != RegistrationSnapshot) { RegistrationSnapshot = V.Snapshot; SelectedRegistration.Invalidate(); if (Owner) Owner->Dismiss(); }
    bool Rebuild = RegistrationIds.Num() != V.Rows.Num();
    if (!Rebuild) for (int32 I = 0; I < V.Rows.Num(); ++I) if (RegistrationIds[I] != V.Rows[I].Item || RegistrationRows->GetOptionAtIndex(I) != V.Rows[I].Label.ToString()) { Rebuild = true; break; }
    if (Rebuild)
    {
        RegistrationIds.Reset(); RegistrationRows->ClearOptions();
        for (const auto& Row : V.Rows) { RegistrationIds.Add(Row.Item); RegistrationRows->AddOption(Row.Label.ToString()); }
        const int32 Index = RegistrationIds.IndexOfByKey(SelectedRegistration); if (Index != INDEX_NONE) RegistrationRows->SetSelectedIndex(Index);
    }
    RegistrationRows->SetIsEnabled(V.bCanReview);
    const auto* Selected = V.Rows.FindByPredicate([&](const auto& Row) { return Row.Item == SelectedRegistration && Row.bNamed; });
    RegistrationReviewButton->SetIsEnabled(V.bCanReview && Selected);
    RegistrationReview = V.Review; RegistrationConsent->SetText(V.ReviewText);
    RegistrationConfirm->SetIsEnabled(V.bCanConfirm); RegistrationConfirm->SetVisibility(V.Review.IsValid() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    RegistrationDismiss->SetVisibility(V.Review.IsValid() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    RegistrationRecover->SetVisibility(V.bCanRecover ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    RegistrationRetry->SetVisibility(V.bCanRetry ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    RegistrationStorage->SetVisibility(V.bCanStorage ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    RegistrationAssets->SetVisibility(V.bCanRefreshAssets ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}
void URPGRidingCollectionWidget::RegistrationSelection(FString, ESelectInfo::Type)
{ if (bClosing || bRegistrationUpdating) return; const int32 I = RegistrationRows->GetSelectedIndex(); SelectedRegistration = RegistrationIds.IsValidIndex(I) ? RegistrationIds[I] : FGuid(); DismissRegistration(); }
void URPGRidingCollectionWidget::ReloadRegistration() { if (const auto C = Registration.Pin(); C && !bClosing) C->Reload(); RefreshRegistration(); }
void URPGRidingCollectionWidget::ReviewRegistration() { if (const auto C = Registration.Pin(); C && !bClosing) C->Review(RegistrationSnapshot, SelectedRegistration); RefreshRegistration(); }
void URPGRidingCollectionWidget::ConfirmRegistration() { if (const auto C = Registration.Pin(); C && !bClosing) C->Confirm(RegistrationReview); RefreshRegistration(); }
void URPGRidingCollectionWidget::DismissRegistration() { if (const auto C = Registration.Pin()) C->Dismiss(); RefreshRegistration(); }
void URPGRidingCollectionWidget::RecoverRegistration() { if (const auto C = Registration.Pin(); C && !bClosing) C->Recover(); RefreshRegistration(); }
void URPGRidingCollectionWidget::RetryRegistration() { if (const auto C = Registration.Pin(); C && !bClosing) C->Retry(); RefreshRegistration(); }
void URPGRidingCollectionWidget::StorageRegistration() { if (const auto C = Registration.Pin(); C && !bClosing) C->RetryStorage(); RefreshRegistration(); }
void URPGRidingCollectionWidget::RefreshRegistrationAssets() { if (const auto C = Registration.Pin(); C && !bClosing) C->RefreshAssets(); RefreshRegistration(); }
