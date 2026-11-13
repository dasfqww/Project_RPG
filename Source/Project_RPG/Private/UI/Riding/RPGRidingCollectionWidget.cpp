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
        auto* List = WidgetTree->ConstructWidget<UVerticalBox>(); Size->SetContent(List);
        auto Text = [&](const FText& Value) { auto* W = WidgetTree->ConstructWidget<UTextBlock>(); W->SetText(Value); W->SetAutoWrapText(true); List->AddChild(W); return W; };
        auto Button = [&](const TCHAR* Name, const FText& Value) { auto* W = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name)); auto* Label = WidgetTree->ConstructWidget<UTextBlock>(); Label->SetText(Value); W->SetContent(Label); List->AddChild(W); return W; };
        Text(NSLOCTEXT("RidingCollection", "Title", "탈것 보관함"));
        Status = Text(FText::GetEmpty()); Settings = Text(FText::GetEmpty());
        ReloadButton = Button(TEXT("RidingCollectionReload"), NSLOCTEXT("RidingCollection", "Reload", "보유 탈것 불러오기"));
        Search = WidgetTree->ConstructWidget<UEditableTextBox>(); Search->SetHintText(NSLOCTEXT("RidingCollection", "Search", "탈것 이름 검색")); List->AddChild(Search);
        FavoritesOnly = WidgetTree->ConstructWidget<UCheckBox>(); auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
        Label->SetText(NSLOCTEXT("RidingCollection", "FavoritesOnly", "즐겨찾기만 보기")); FavoritesOnly->SetContent(Label); List->AddChild(FavoritesOnly);
        Empty = Text(FText::GetEmpty());
        auto* Scroll = WidgetTree->ConstructWidget<UScrollBox>(); List->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        Rows = WidgetTree->ConstructWidget<UVerticalBox>(); Scroll->AddChild(Rows);
        CloseButton = Button(TEXT("RidingCollectionClose"), NSLOCTEXT("RidingCollection", "Close", "닫기"));
        ReloadButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Reload); CloseButton->OnClicked.AddUniqueDynamic(this, &ThisClass::CloseScreen);
        Search->OnTextChanged.AddUniqueDynamic(this, &ThisClass::SearchChanged); FavoritesOnly->OnCheckStateChanged.AddUniqueDynamic(this, &ThisClass::FavoritesChanged);
    }
    SetIsFocusable(true); RowsKey.Reset(); RefreshView(); return Super::RebuildWidget();
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
    if (!TickHandle.IsValid()) TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::TickView), .25f);
}
void URPGRidingCollectionWidget::Detach()
{
    if (const auto Owner = Reader.Pin()) Owner->OnChanged.Remove(Changed); Changed.Reset();
    FTSTicker::RemoveTicker(TickHandle); TickHandle.Reset();
}
void URPGRidingCollectionWidget::NativeConstruct()
{
    Super::NativeConstruct(); Subscribe(); RefreshView();
    if (CloseButton && GetOwningPlayer()) CloseButton->SetKeyboardFocus();
}
void URPGRidingCollectionWidget::NativeDestruct() { Detach(); Super::NativeDestruct(); }
bool URPGRidingCollectionWidget::TickView(float) { if (bClosing) return false; RefreshView(); return !bClosing; }
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
    if (IsInViewport() || GetParent()) RemoveFromParent(); OnClosed.Broadcast();
}
FReply URPGRidingCollectionWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{ if (Event.GetKey() == EKeys::Escape) { CloseScreen(); return FReply::Handled(); } return Super::NativeOnKeyDown(Geometry, Event); }
