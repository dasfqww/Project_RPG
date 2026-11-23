#include "UI/Riding/RPGRidingCollectionWidget.h"
#include "UI/Riding/RPGRidingPreferencesWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/DragDropOperation.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/ButtonSlot.h"
#include "Components/CheckBox.h"
#include "Components/EditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Brushes/SlateImageBrush.h"
#include "Misc/Paths.h"
#include "InputCoreTypes.h"

namespace
{
    const FLinearColor Gold(.88f, .64f, .30f);
    const FLinearColor Ink(.008f, .009f, .010f);

    UTextBlock* Label(UWidgetTree* Tree, const FText& Value, int32 FontSize = 12, bool Bold = false)
    {
        auto* Text = Tree->ConstructWidget<UTextBlock>();
        Text->SetText(Value);
        auto Font = Text->GetFont();
        Font.Size = FontSize;
        Font.TypefaceFontName = Bold ? TEXT("Bold") : TEXT("Regular");
        Text->SetFont(Font);
        Text->SetAutoWrapText(false);
        return Text;
    }

    FButtonStyle ButtonStyle(bool Selected)
    {
        const FLinearColor Edge = Selected ? FLinearColor(.13f,.56f,.95f) : FLinearColor(.048f,.052f,.054f);
        FButtonStyle Style;
        Style.SetNormal(FSlateRoundedBoxBrush(FLinearColor(.012f,.014f,.016f), 0.f, Edge, 1.f));
        Style.SetHovered(FSlateRoundedBoxBrush(FLinearColor(.031f,.035f,.042f), 0.f, Selected ? Edge : Gold, 1.f));
        Style.SetPressed(Style.Hovered);
        Style.SetNormalPadding(FMargin(6,4));
        Style.SetPressedPadding(FMargin(6,4));
        return Style;
    }

    UButton* Action(UWidgetTree* Tree, const TCHAR* Name, const FText& Value)
    {
        auto* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
        Button->SetStyle(ButtonStyle(false));
        Button->SetContent(Label(Tree, Value));
        return Button;
    }

    UBorder* Surface(UWidgetTree* Tree, FLinearColor Color, FMargin Padding)
    {
        auto* Border = Tree->ConstructWidget<UBorder>();
        Border->SetBrushColor(Color);
        Border->SetPadding(Padding);
        return Border;
    }
}

FSlateBrush URPGRidingCollectionWidget::MountPortrait(const FString& Id, float Size)
{
    const TCHAR* File = Id.StartsWith(TEXT("wolf")) ? TEXT("Fang.png") :
                        Id.StartsWith(TEXT("stag")) ? TEXT("Hart.png") : TEXT("Courser.png");
    const FName Resource(*(FPaths::ProjectContentDir() / TEXT("Slate/Riding") / File));
    auto& Cached = PortraitBrushes.FindOrAdd(Resource);
    if (!Cached) Cached = MakeShared<FSlateDynamicImageBrush>(Resource, FVector2D(Size));
    FSlateBrush Brush = *Cached;
    Brush.SetImageSize(FVector2D(Size));
    return Brush;
}

TSharedRef<SWidget> URPGRidingCollectionWidget::RebuildWidget()
{
    if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"));
    if (!WidgetTree->RootWidget)
    {
        auto* Backdrop = Surface(WidgetTree, FLinearColor(.004f,.007f,.012f,.82f), FMargin(14));
        WidgetTree->RootWidget = Backdrop;
        auto* Fit = WidgetTree->ConstructWidget<UScaleBox>();
        Fit->SetStretch(EStretch::ScaleToFit);
        Fit->SetStretchDirection(EStretchDirection::DownOnly);
        Backdrop->SetContent(Fit);
        auto* Size = WidgetTree->ConstructWidget<USizeBox>();
        Size->SetWidthOverride(465);
        Size->SetHeightOverride(650);
        Fit->SetContent(Size);
        CastChecked<UScaleBoxSlot>(Size->Slot)->SetHorizontalAlignment(HAlign_Center);
        CastChecked<UScaleBoxSlot>(Size->Slot)->SetVerticalAlignment(VAlign_Center);
        auto* Rim = Surface(WidgetTree, FLinearColor(.30f,.23f,.15f), FMargin(1));
        Size->SetContent(Rim);
        auto* Shell = Surface(WidgetTree, Ink, FMargin(10));
        Rim->SetContent(Shell);
        auto* Layout = WidgetTree->ConstructWidget<UVerticalBox>();
        Shell->SetContent(Layout);
        auto* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
        Layout->AddChildToVerticalBox(Header)->SetPadding(FMargin(0,0,0,4));
        auto* Title = Label(WidgetTree, NSLOCTEXT("RidingCollection","MountTitle","탈 것"), 20, true);
        Title->SetColorAndOpacity(FLinearColor(.9f,.83f,.67f));
        Title->SetJustification(ETextJustify::Center);
        Header->AddChildToHorizontalBox(Title)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        CloseButton = Action(WidgetTree, TEXT("RidingCollectionClose"), FText::FromString(TEXT("×")));
        CloseButton->SetToolTipText(NSLOCTEXT("RidingCollection","Close","닫기"));
        Header->AddChild(CloseButton);

        auto* Rule = WidgetTree->ConstructWidget<UHorizontalBox>();
        Layout->AddChildToVerticalBox(Rule)->SetPadding(FMargin(0,0,0,6));
        auto AddRule = [&]
        {
            auto* Hairline = WidgetTree->ConstructWidget<USizeBox>();
            Hairline->SetHeightOverride(1);
            Hairline->SetContent(Surface(WidgetTree, FLinearColor(.30f,.19f,.065f), FMargin(0)));
            auto* Cell = Rule->AddChildToHorizontalBox(Hairline);
            Cell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            Cell->SetVerticalAlignment(VAlign_Center);
        };
        AddRule();
        auto* Diamond = Label(WidgetTree, FText::FromString(TEXT("◇")), 9);
        Diamond->SetColorAndOpacity(Gold);
        Rule->AddChild(Diamond);
        AddRule();
        Pages = WidgetTree->ConstructWidget<UWidgetSwitcher>();
        Layout->AddChildToVerticalBox(Pages)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* CollectionPage = WidgetTree->ConstructWidget<UVerticalBox>();
        Pages->AddChild(CollectionPage);
        auto* ListSurface = Surface(WidgetTree, FLinearColor(.006f,.007f,.008f), FMargin(5));
        CollectionPage->AddChildToVerticalBox(ListSurface)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* List = WidgetTree->ConstructWidget<UVerticalBox>();
        ListSurface->SetContent(List);
        auto Text = [&](const FText& Value)
        {
            auto* W = Label(WidgetTree, Value);
            W->SetAutoWrapText(true);
            List->AddChildToVerticalBox(W)->SetPadding(FMargin(0,2));
            return W;
        };
        auto Button = [&](const TCHAR* Name, const FText& Value)
        {
            auto* W = Action(WidgetTree, Name, Value);
            List->AddChildToVerticalBox(W)->SetPadding(FMargin(0,2));
            return W;
        };
        auto* SearchLine = WidgetTree->ConstructWidget<UHorizontalBox>();
        List->AddChild(SearchLine);
        Search = WidgetTree->ConstructWidget<UEditableTextBox>();
        Search->SetHintText(NSLOCTEXT("RidingCollection","Search","탈것 이름 검색"));
        auto SearchStyle = Search->GetWidgetStyle();
        auto SearchFont = SearchStyle.TextStyle.Font;
        SearchFont.Size = 12;
        SearchStyle.SetFont(SearchFont);
        SearchStyle.SetForegroundColor(FLinearColor::White);
        SearchStyle.SetFocusedForegroundColor(FLinearColor::White);
        SearchStyle.SetBackgroundImageNormal(ButtonStyle(false).Normal);
        SearchStyle.SetBackgroundImageHovered(ButtonStyle(false).Hovered);
        SearchStyle.SetBackgroundImageFocused(ButtonStyle(true).Normal);
        Search->SetWidgetStyle(SearchStyle);
        SearchLine->AddChildToHorizontalBox(Search)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* SearchButton = Action(WidgetTree,TEXT("FocusRidingSearch"),NSLOCTEXT("RidingCollection","SearchButton","검색"));
        auto* Magnifier = WidgetTree->ConstructWidget<UImage>();
        Magnifier->SetBrush(FSlateVectorImageBrush(FPaths::ProjectContentDir() / TEXT("Slate/Riding/Search.svg"), FVector2D(18)));
        SearchButton->SetContent(Magnifier);
        SearchButton->SetToolTipText(NSLOCTEXT("RidingCollection","SearchButton","검색"));
        SearchButton->OnClicked.AddUniqueDynamic(this,&ThisClass::FocusSearch);
        SearchLine->AddChild(SearchButton);
        auto* FilterLine = WidgetTree->ConstructWidget<UHorizontalBox>();
        List->AddChildToVerticalBox(FilterLine)->SetPadding(FMargin(0,4));

        FavoritesOnly = WidgetTree->ConstructWidget<UCheckBox>();
        FavoritesOnly->SetContent(Label(WidgetTree, NSLOCTEXT("RidingCollection","FavoritesOnly","즐겨찾기만 보기")));
        FilterLine->AddChildToHorizontalBox(FavoritesOnly)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* Sort = Action(WidgetTree,TEXT("RidingSort"),NSLOCTEXT("RidingCollection","DefaultSort","기본 정렬"));
        SortLabel = CastChecked<UTextBlock>(Sort->GetContent());
        SortLabel->SetColorAndOpacity(Gold);
        Sort->OnClicked.AddUniqueDynamic(this,&ThisClass::ToggleSort);
        FilterLine->AddChild(Sort);
        Empty = Text(FText::GetEmpty());
        RosterScroll = WidgetTree->ConstructWidget<UScrollBox>();
        List->AddChildToVerticalBox(RosterScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        Rows = WidgetTree->ConstructWidget<UVerticalBox>();
        RosterScroll->AddChild(Rows);
        QuickSlotDropZone = Surface(WidgetTree, FLinearColor(.14f,.10f,.063f), FMargin(6));
        List->AddChildToVerticalBox(QuickSlotDropZone)->SetPadding(FMargin(0, 4, 0, 0));
        auto* SlotContent = WidgetTree->ConstructWidget<UHorizontalBox>();
        QuickSlotDropZone->SetContent(SlotContent);
        QuickSlotIcon = WidgetTree->ConstructWidget<UImage>();
        SlotContent->AddChild(QuickSlotIcon);
        QuickSlotLabel = Label(WidgetTree, FText::GetEmpty(), 11);
        auto* DropLabelSlot = SlotContent->AddChildToHorizontalBox(QuickSlotLabel);
        DropLabelSlot->SetVerticalAlignment(VAlign_Center);
        DropLabelSlot->SetPadding(FMargin(4,0));
        Status = Label(WidgetTree, FText::GetEmpty(), 11);
        Status->SetAutoWrapText(true);
        List->AddChildToVerticalBox(Status)->SetPadding(FMargin(0, 4, 0, 0));

        auto* Hint = Label(WidgetTree, NSLOCTEXT("RidingCollection", "QuickSlotHint", "탈것을 퀵슬롯에 등록하여 탑승할수 있습니다."), 11);
        Hint->SetColorAndOpacity(Gold);
        Hint->SetJustification(ETextJustify::Center);
        Hint->SetAutoWrapText(true);
        CollectionPage->AddChildToVerticalBox(Hint)->SetPadding(FMargin(0, 8, 0, 5));
        auto* Footer = WidgetTree->ConstructWidget<UHorizontalBox>();
        CollectionPage->AddChild(Footer);

        MountButton = Action(WidgetTree, TEXT("RidingCollectionMount"), NSLOCTEXT("RidingCollection", "Mount", "탑승"));
        auto* MountSlot = Footer->AddChildToHorizontalBox(MountButton);
        MountSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        MountSlot->SetPadding(FMargin(0, 0, 3, 0));
        MountButton->OnClicked.AddUniqueDynamic(this, &ThisClass::MountSelected);
        auto* FooterClose = Action(WidgetTree, TEXT("RidingCollectionFooterClose"), NSLOCTEXT("RidingCollection", "Close", "닫기"));
        Footer->AddChildToHorizontalBox(FooterClose)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        FooterClose->OnClicked.AddUniqueDynamic(this, &ThisClass::CloseScreen);

        auto* RegistrationPage = WidgetTree->ConstructWidget<UVerticalBox>();
        Pages->AddChild(RegistrationPage);
        BackButton = Action(WidgetTree,TEXT("RidingRegistrationBack"), NSLOCTEXT("RidingRegistration","Back","‹  탈것 목록으로"));
        RegistrationPage->AddChildToVerticalBox(BackButton)->SetHorizontalAlignment(HAlign_Left);
        BackButton->OnClicked.AddUniqueDynamic(this,&ThisClass::BackToCollection);
        RegistrationUnavailable = Label(WidgetTree,NSLOCTEXT("RidingRegistration","Unavailable","현재 등록할 물품 정보를 사용할 수 없습니다."));
        RegistrationPage->AddChild(RegistrationUnavailable);
        auto* RegistrationScroll = WidgetTree->ConstructWidget<UScrollBox>();
        RegistrationPage->AddChildToVerticalBox(RegistrationScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        RegistrationGroup = WidgetTree->ConstructWidget<UVerticalBox>();
        RegistrationScroll->AddChild(RegistrationGroup);
        auto* PreferencesPage = WidgetTree->ConstructWidget<UVerticalBox>();
        Pages->AddChild(PreferencesPage);
        auto* PreferencesBack = Action(WidgetTree, TEXT("RidingPreferencesBack"), NSLOCTEXT("RidingRegistration","Back","‹  탈것 목록으로"));
        PreferencesPage->AddChildToVerticalBox(PreferencesBack)->SetHorizontalAlignment(HAlign_Left);
        PreferencesBack->OnClicked.AddUniqueDynamic(this, &ThisClass::BackToCollection);
        auto* PreferencesScroll = WidgetTree->ConstructWidget<UScrollBox>();
        PreferencesPage->AddChildToVerticalBox(PreferencesScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        Settings = Label(WidgetTree, FText::GetEmpty(), 11);
        Settings->SetAutoWrapText(true);
        PreferencesScroll->AddChild(Settings);
        PreferencesMountName = Label(WidgetTree, FText::GetEmpty(), 16, true);
        PreferencesMountName->SetColorAndOpacity(Gold);
        PreferencesScroll->AddChild(PreferencesMountName);
        PreferencesPanel = WidgetTree->ConstructWidget<URPGRidingPreferencesWidget>();
        PreferencesScroll->AddChild(PreferencesPanel);
        if (const auto P = PreferencesOwner.Pin()) PreferencesPanel->SetOwner(P.ToSharedRef());

        List = RegistrationGroup;
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
        CloseButton->OnClicked.AddUniqueDynamic(this, &ThisClass::CloseScreen);
        Search->OnTextChanged.AddUniqueDynamic(this, &ThisClass::SearchChanged); FavoritesOnly->OnCheckStateChanged.AddUniqueDynamic(this, &ThisClass::FavoritesChanged);
    }
    SetIsFocusable(true); RowsKey.Reset(); RefreshView(); RefreshRegistration(); return Super::RebuildWidget();
}
void URPGRidingCollectionWidget::SetReadOwner(TSharedRef<FRPGRidingCollectionReader> InReader, FPresenter InPresenter, FPresenter InDescription)
{
    Detach(); Reader = InReader; Presenter = MoveTemp(InPresenter); Description = MoveTemp(InDescription); BoundContext = InReader->GetContext(); bClosing = false;
    RowsKey.Reset(); Subscribe(); RefreshView();
}
void URPGRidingCollectionWidget::Subscribe()
{
    if (bClosing) return;
    if (const auto Owner = Reader.Pin()) { Owner->OnChanged.Remove(Changed); Changed = Owner->OnChanged.AddUObject(this, &ThisClass::RefreshView); }
    if (const auto Owner = Registration.Pin()) { Owner->OnChanged.Remove(RegistrationChanged); RegistrationChanged = Owner->OnChanged.AddUObject(this, &ThisClass::RefreshRegistration); }
    if (PreferencesPanel) if (const auto P = PreferencesOwner.Pin()) PreferencesPanel->SetOwner(P.ToSharedRef());
    if (!TickHandle.IsValid()) TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::TickView), .25f);
}
void URPGRidingCollectionWidget::Detach()
{
    if (const auto Owner = Reader.Pin()) Owner->OnChanged.Remove(Changed); Changed.Reset();
    if (const auto Owner = Registration.Pin()) { Owner->OnChanged.Remove(RegistrationChanged); Owner->Dismiss(); } RegistrationChanged.Reset();
    if (PreferencesPanel) PreferencesPanel->DetachOwner();
    FTSTicker::RemoveTicker(TickHandle); TickHandle.Reset();
}
void URPGRidingCollectionWidget::NativeConstruct()
{
    Super::NativeConstruct(); Subscribe(); RefreshView();
    if (CloseButton && GetOwningPlayer()) CloseButton->SetKeyboardFocus();
}
void URPGRidingCollectionWidget::NativeDestruct() { Detach(); Super::NativeDestruct(); }
bool URPGRidingCollectionWidget::TickView(float) { if (bClosing) return false; RefreshView(); RefreshRegistration(); return !bClosing; }
void URPGRidingCollectionWidget::Reload() { MountFeedback = FText::GetEmpty(); if (!bClosing) if (const auto Owner = Reader.Pin()) Owner->Read(); RefreshView(); }
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
    Search->SetIsEnabled(bReady); FavoritesOnly->SetIsEnabled(bReady);
    FText Message = NSLOCTEXT("RidingCollection", "Prompt", "보유 탈것을 확인하고 있습니다.");
    if (State == ERPGRidingCollectionReadState::Loading) Message = NSLOCTEXT("RidingCollection", "Loading", "보유 탈것을 불러오는 중입니다.");
    else if (State == ERPGRidingCollectionReadState::Failed) Message = NSLOCTEXT("RidingCollection", "Failed", "탈것 정보를 불러오지 못했습니다. 창을 닫고 다시 열어 주세요.");
    else if (Retry > 0) Message = FText::Format(NSLOCTEXT("RidingCollection", "Retry", "{0}초 후 다시 불러올 수 있습니다."), FMath::CeilToInt(Retry));
    else if (bReady) Message = C->bCatalogReady ? NSLOCTEXT("RidingCollection", "Ready", "현재 캐릭터에 저장된 탈것 정보입니다.")
        : NSLOCTEXT("RidingCollection", "Disabled", "보유 내역입니다. 현재 탈것 등록과 설정 변경을 사용할 수 없습니다.");
    Status->SetText(MountFeedback.IsEmpty() ? Message : MountFeedback); Settings->SetText(FText::GetEmpty());
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
    if (Key == RowsKey) return;
    RowsKey = Key;
    Rows->ClearChildren();

    const FString Assigned = C && C->Shortcut.Kind == ERPGRidingSelectorKind::Specific ? C->Shortcut.MountId
        : C && C->Shortcut.Kind == ERPGRidingSelectorKind::Selected ? C->SelectedMountId : FString();
    const bool bAssigned = Names.Contains(Assigned);
    QuickSlotIcon->SetVisibility(bAssigned ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    if (bAssigned) QuickSlotIcon->SetBrush(MountPortrait(Assigned, 30));
    QuickSlotLabel->SetText(bAssigned
        ? FText::Format(NSLOCTEXT("RidingCollection", "SlotAssigned", "퀵슬롯 · {0}"), Names.FindChecked(Assigned))
        : NSLOCTEXT("RidingCollection", "SlotDrop", "퀵슬롯 · 탈것 아이콘을 여기에 놓으세요."));

    TArray<FString> Visible;
    if (C) for (const auto& M : C->Mounts)
    {
        if (bFavorites && !C->Favorites.Contains(M.MountId)) continue;
        if (!Filter.IsEmpty() && !Names.FindChecked(M.MountId).ToString().Contains(Filter)) continue;
        Visible.Add(M.MountId);
    }
    if (bSortByName) Visible.StableSort([&Names](const FString& A, const FString& B) { return Names.FindChecked(A).CompareTo(Names.FindChecked(B)) < 0; });
    if (!Visible.Contains(BrowsedMount))
    {
        BrowsedMount = Visible.IsEmpty() ? FString() :
            Visible.Contains(C->SelectedMountId) ? C->SelectedMountId : Visible[0];
    }
    MountButton->SetIsEnabled(!BrowsedMount.IsEmpty());
    PreferencesMountName->SetText(Names.FindRef(BrowsedMount));
    PreferencesPanel->BrowseMount(BrowsedMount);
    for (const auto& Id : Visible)
    {
        auto* Row = WidgetTree->ConstructWidget<URPGRidingBrowseButton>();
        Row->Collection = this;
        Row->MountId = Id;
        Row->OnClicked.AddUniqueDynamic(Row,&URPGRidingBrowseButton::Browse);
        Row->SetStyle(ButtonStyle(Id == BrowsedMount));
        auto* Line = WidgetTree->ConstructWidget<UHorizontalBox>();
        CastChecked<UButtonSlot>(Row->AddChild(Line))->SetHorizontalAlignment(HAlign_Fill);
        const bool Starred = C->Favorites.Contains(Id);
        auto* Star = Label(WidgetTree,FText::FromString(Starred ? TEXT("★") : TEXT("☆")), 14);
        Star->SetColorAndOpacity(Starred ? Gold : FLinearColor(.18f,.19f,.20f));
        auto* StarSlot = Line->AddChildToHorizontalBox(Star);
        StarSlot->SetVerticalAlignment(VAlign_Center);
        StarSlot->SetPadding(FMargin(2,0,7,0));
        auto* Icon = WidgetTree->ConstructWidget<UImage>();
        Icon->SetBrush(MountPortrait(Id, 46));
        Row->DragIcon = Icon;
        auto* IconFrame = Surface(WidgetTree, FLinearColor(.20f,.16f,.10f), FMargin(1));
        IconFrame->SetContent(Icon);
        Line->AddChild(IconFrame);
        auto* Copy = WidgetTree->ConstructWidget<UVerticalBox>();
        auto* NameText = Label(WidgetTree,Names.FindChecked(Id),13,true);
        NameText->SetColorAndOpacity(Id == C->SelectedMountId ? Gold : Starred ? FLinearColor(.30f,.76f,1.f) : FLinearColor(.64f,.84f,.37f));
        NameText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
        Copy->AddChild(NameText);
        FText Caption = Description ? Description(Id) : FText::GetEmpty();
        if (Caption.IsEmpty())
            Caption = Id == C->SelectedMountId ? NSLOCTEXT("RidingCollection","DefaultDescription","기본 탈것으로 지정됨") :
                Starred ? NSLOCTEXT("RidingCollection","StarDescription","즐겨찾기에 등록된 탈것") : NSLOCTEXT("RidingCollection","OwnedDescription","보유 중인 탈것");
        auto* Sub = Label(WidgetTree,Caption,11);
        Sub->SetColorAndOpacity(FLinearColor(.57f,.59f,.60f));
        Sub->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
        Copy->AddChild(Sub);
        auto* NameSlot = Line->AddChildToHorizontalBox(Copy);
        NameSlot->SetPadding(FMargin(9,0));
        NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        NameSlot->SetVerticalAlignment(VAlign_Center);
        Row->SetToolTipText(Names.FindChecked(Id));
        Rows->AddChildToVerticalBox(Row)->SetPadding(FMargin(0,0,0,1));
    }
    const int32 Count = Visible.Num();
    Empty->SetVisibility(C && Visible.IsEmpty() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    Empty->SetText(!C ? FText::GetEmpty() : C->Mounts.IsEmpty() ? NSLOCTEXT("RidingCollection", "Empty", "보유한 탈것이 없습니다.")
        : Count == 0 ? NSLOCTEXT("RidingCollection", "FilteredEmpty", "검색 조건에 맞는 탈것이 없습니다.") : FText::GetEmpty());
}
void URPGRidingBrowseButton::Browse()
{
    if (Collection.IsValid()) Collection->Browse(MountId);
}
void URPGRidingCollectionWidget::Browse(const FString& MountId)
{
    if (BrowsedMount == MountId) return;
    MountFeedback = FText::GetEmpty();
    BrowsedMount = MountId;
    RowsKey.Reset();
    RefreshView();
}
void URPGRidingCollectionWidget::FocusSearch()
{
    Search->SetKeyboardFocus();
}
void URPGRidingCollectionWidget::ToggleSort()
{
    bSortByName = !bSortByName;
    SortLabel->SetText(bSortByName ? NSLOCTEXT("RidingCollection","NameSort","이름 정렬") : NSLOCTEXT("RidingCollection","DefaultSort","기본 정렬"));
    RowsKey.Reset();
    RefreshView();
}
void URPGRidingCollectionWidget::MountSelected()
{
    if (bClosing || !Pages || Pages->GetActiveWidgetIndex() != 0 || BrowsedMount.IsEmpty()) return;
    const auto Owner = Reader.Pin();
    const auto* Collection = Owner ? Owner->GetCollection() : nullptr;
    if (!Collection || !Collection->Mounts.ContainsByPredicate([this](const FRPGRidingOwnedMount& Mount)
        { return Mount.MountId == BrowsedMount; })) return;

    const FRPGRidingSelector Selector{ERPGRidingSelectorKind::Specific, BrowsedMount};
    MountFeedback = MountAction && MountAction(Selector) ? FText::GetEmpty()
        : NSLOCTEXT("RidingCollection", "MountUnavailable", "현재 장소에서는 탈것에 탑승할 수 없습니다.");
    RefreshView();
}

FReply URPGRidingCollectionWidget::NativeOnPreviewMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
    DragSource.Reset();
    if (!bClosing && Rows && Pages->GetActiveWidgetIndex() == 0
        && RosterScroll->GetCachedGeometry().IsUnderLocation(Event.GetScreenSpacePosition())
        && Event.GetEffectingButton() == EKeys::LeftMouseButton)
    {
        for (auto* Child : Rows->GetAllChildren())
        {
            auto* Row = Cast<URPGRidingBrowseButton>(Child);
            if (Row && Row->DragIcon && Row->DragIcon->GetCachedGeometry().IsUnderLocation(Event.GetScreenSpacePosition()))
            {
                DragSource = Row;
                return FReply::Handled().DetectDrag(GetCachedWidget().ToSharedRef(), EKeys::LeftMouseButton);
            }
        }
    }
    return Super::NativeOnPreviewMouseButtonDown(Geometry, Event);
}

FReply URPGRidingCollectionWidget::NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
    if (const auto* Row = DragSource.Get(); Row && Event.GetEffectingButton() == EKeys::LeftMouseButton)
    {
        DragSource.Reset();
        if (Row->GetParent() == Rows && Row->DragIcon->GetCachedGeometry().IsUnderLocation(Event.GetScreenSpacePosition()))
            Browse(Row->MountId);
        return FReply::Handled();
    }
    return Super::NativeOnMouseButtonUp(Geometry, Event);
}

void URPGRidingCollectionWidget::NativeOnDragDetected(const FGeometry& Geometry, const FPointerEvent& Event, UDragDropOperation*& Operation)
{
    Super::NativeOnDragDetected(Geometry, Event, Operation);
    auto* Row = DragSource.Get();
    DragSource.Reset();
    if (bClosing || !Row || Row->GetParent() != Rows) return;
    Operation = NewObject<UDragDropOperation>(this);
    Operation->Payload = Row;
    auto* Visual = NewObject<UImage>(this);
    Visual->SetBrush(MountPortrait(Row->MountId, 46));
    Operation->DefaultDragVisual = Visual;
    Operation->Pivot = EDragPivot::CenterCenter;
}

bool URPGRidingCollectionWidget::NativeOnDrop(const FGeometry& Geometry, const FDragDropEvent& Event, UDragDropOperation* Operation)
{
    if (Operation && Operation->GetOuter() == this && QuickSlotDropZone
        && QuickSlotDropZone->GetCachedGeometry().IsUnderLocation(Event.GetScreenSpacePosition()))
        return AcceptQuickSlotDrop(Cast<URPGRidingBrowseButton>(Operation->Payload));
    return Super::NativeOnDrop(Geometry, Event, Operation);
}

bool URPGRidingCollectionWidget::AcceptQuickSlotDrop(const URPGRidingBrowseButton* Row)
{
    const auto Preferences = PreferencesOwner.Pin();
    if (bClosing || !Preferences || Pages->GetActiveWidgetIndex() != 0 || !Row
        || Row->Collection.Get() != this || Row->GetParent() != Rows) return false;

    const auto View = Preferences->GetView();
    if (View.bCanRecover || View.bCanRetry || View.bCanStorage || View.bCanRefreshAssets
        || (!View.bCanEdit && View.bCanReload))
    {
        OpenPreferences();
        return true;
    }

    const FString Id = Row->MountId;
    if (!Preferences->AssignQuickSlot(View.Snapshot, Id)) return false;
    Browse(Id);
    Preferences->Review(View.Snapshot);
    OpenPreferences();
    return true;
}

void URPGRidingCollectionWidget::OpenRegistration()
{
    Pages->SetActiveWidgetIndex(1);
    RefreshRegistration();
    BackButton->SetKeyboardFocus();
}
void URPGRidingCollectionWidget::OpenPreferences()
{
    Pages->SetActiveWidgetIndex(2);
    SetKeyboardFocus();
}
void URPGRidingCollectionWidget::BackToCollection()
{
    const bool bFromRegistration = Pages->GetActiveWidgetIndex() == 1;
    if (bFromRegistration) DismissRegistration();
    Pages->SetActiveWidgetIndex(0);
    CloseButton->SetKeyboardFocus();
}
void URPGRidingCollectionWidget::CloseScreen()
{
    if (bClosing) return; bClosing = true; Detach();
    if (Rows) Rows->ClearChildren(); if (Settings) Settings->SetText(FText::GetEmpty());
    Reader.Reset(); Presenter = {}; Description = {}; MountAction = {}; RowsKey.Reset(); SetVisibility(ESlateVisibility::Collapsed);
    PreferencesOwner.Reset(); Registration.Reset(); RegistrationIds.Reset(); RegistrationSnapshot.Invalidate(); RegistrationReview.Invalidate(); SelectedRegistration.Invalidate();
    if (IsInViewport() || GetParent()) RemoveFromParent(); OnClosed.Broadcast();
}
FReply URPGRidingCollectionWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{ if (Event.GetKey() == EKeys::Escape) { if (Pages && Pages->GetActiveWidgetIndex() != 0) BackToCollection(); else CloseScreen(); return FReply::Handled(); } return Super::NativeOnKeyDown(Geometry, Event); }

void URPGRidingCollectionWidget::SetPreferencesOwner(TSharedRef<FRPGRidingPreferencesClient> InOwner)
{ PreferencesOwner = InOwner; if (PreferencesPanel) PreferencesPanel->SetOwner(InOwner); }
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
    RegistrationUnavailable->SetVisibility(V.bAvailable ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
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
