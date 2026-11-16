#include "UI/Shop/RPGShopWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Component/UI/RPGSkillWindowComponent.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Misc/PackageName.h"
#include "UI/RPGWindowInputState.h"

namespace RPGShopUI
{
    const FLinearColor Ink(.92f, .81f, .64f);
    const FLinearColor Muted(.72f, .66f, .56f);

    UTexture2D* LoadArt(const TCHAR* Name)
    {
        const FString Package = FString(TEXT("/Game/Assets/GUIParts/Gui_parts/")) + Name;
        return FPackageName::DoesPackageExist(Package)
            ? LoadObject<UTexture2D>(nullptr, *(Package + TEXT(".") + Name)) : nullptr;
    }

    FSlateBrush Brush(UTexture2D* Texture, FVector2D Size, bool bBox = false)
    {
        FSlateBrush Out;
        Out.SetResourceObject(Texture);
        Out.ImageSize = Size;
        Out.DrawAs = bBox ? ESlateBrushDrawType::Box : ESlateBrushDrawType::Image;
        Out.Margin = bBox ? FMargin(.16f) : FMargin(0);
        return Out;
    }

    FText Currency(const FString& Code)
    {
        return Code == TEXT("Gold") || Code == TEXT("RosterGold")
            ? NSLOCTEXT("RPGShop", "Gold", "골드") : NSLOCTEXT("RPGShop", "Currency", "재화");
    }

    FText Money(int64 Amount, const FString& Code)
    {
        return FText::Format(NSLOCTEXT("RPGShop", "Money", "{0} {1}"), FText::AsNumber(Amount), Currency(Code));
    }

    void Visible(UWidget* Widget, bool bVisible)
    {
        if (Widget) Widget->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    }

    void Add(UVerticalBox& Box, UWidget* Widget, float Padding = 5)
    {
        auto* Slot = Box.AddChildToVerticalBox(Widget);
        Slot->SetPadding(FMargin(0, Padding));
        Slot->SetHorizontalAlignment(HAlign_Fill);
    }
}

void URPGShopRowButton::BindChoice(FSimpleDelegate InChoice)
{
    Choice = MoveTemp(InChoice);
    OnClicked.AddUniqueDynamic(this, &ThisClass::Choose);
}

void URPGShopRowButton::Choose() { Choice.ExecuteIfBound(); }

UTextBlock* URPGShopWidget::MakeText(const FText& Label, int32 Size)
{
    auto* Text = WidgetTree->ConstructWidget<UTextBlock>();
    Text->SetText(Label);
    auto Font = Text->GetFont();
    Font.Size = Size;
    Text->SetFont(Font);
    Text->SetColorAndOpacity(FSlateColor(RPGShopUI::Ink));
    Text->SetAutoWrapText(true);
    return Text;
}

void URPGShopWidget::StyleButton(UButton& Button) const
{
    auto Style = Button.GetStyle();
    if (ButtonTexture)
    {
        Style.Normal = RPGShopUI::Brush(ButtonTexture, FVector2D(160, 44), true);
        Style.Hovered = Style.Normal;
        Style.Hovered.TintColor = FSlateColor(FLinearColor(1.45f, 1.3f, 1.15f));
        Style.Pressed = Style.Normal;
        Style.Pressed.TintColor = FSlateColor(FLinearColor(.65f, .65f, .65f));
        Style.Disabled = Style.Normal;
        Style.Disabled.TintColor = FSlateColor(FLinearColor(.5f, .5f, .5f));
    }
    Style.NormalPadding = FMargin(12, 7);
    Style.PressedPadding = FMargin(12, 8, 12, 6);
    Button.SetStyle(Style);
    // Native UButton defaults to keyboard focusable; it has no runtime focusability setter.
}

UButton* URPGShopWidget::MakeButton(const TCHAR* Name, const FText& Label)
{
    auto* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
    StyleButton(*Button);
    auto* Text = MakeText(Label);
    Text->SetJustification(ETextJustify::Center);
    Button->SetContent(Text);
    return Button;
}

void URPGShopWidget::LabelButton(UButton* Button, const FText& Label)
{
    if (auto* Text = Button ? Cast<UTextBlock>(Button->GetContent()) : nullptr) Text->SetText(Label);
}

TSharedRef<SWidget> URPGShopWidget::RebuildWidget()
{
    using namespace RPGShopUI;
    if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this, TEXT("ShopTree"));
    if (!WidgetTree->RootWidget)
    {
        // These textures are already referenced and cooked by the existing bag skin.
        BackgroundTexture = LoadArt(TEXT("big_background"));
        FrameTexture = LoadArt(TEXT("Frame_big"));
        ButtonTexture = LoadArt(TEXT("button"));

        auto* Root = WidgetTree->ConstructWidget<UCanvasPanel>();
        WidgetTree->RootWidget = Root;
        auto* Shade = WidgetTree->ConstructWidget<UBorder>();
        Shade->SetBrushColor(FLinearColor(0, 0, 0, .6f));
        auto* ShadeSlot = Root->AddChildToCanvas(Shade);
        ShadeSlot->SetAnchors(FAnchors(0, 0, 1, 1));
        ShadeSlot->SetOffsets(FMargin(0));

        WindowBounds = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ShopBounds"));
        auto* Center = Root->AddChildToCanvas(WindowBounds);
        Center->SetAnchors(FAnchors(.5f));
        Center->SetAlignment(FVector2D(.5f));
        Center->SetAutoSize(true);
        auto* Layers = WidgetTree->ConstructWidget<UOverlay>();
        WindowBounds->SetContent(Layers);
        auto* Background = WidgetTree->ConstructWidget<UImage>();
        if (BackgroundTexture) Background->SetBrush(Brush(BackgroundTexture, FVector2D(960, 680)));
        else Background->SetColorAndOpacity(FLinearColor(.045f, .032f, .021f));
        Background->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* BackSlot = Layers->AddChildToOverlay(Background);
        BackSlot->SetHorizontalAlignment(HAlign_Fill);
        BackSlot->SetVerticalAlignment(VAlign_Fill);
        WindowBorder = WidgetTree->ConstructWidget<UBorder>();
        if (FrameTexture) WindowBorder->SetBrush(Brush(FrameTexture, FVector2D(960, 680)));
        else WindowBorder->SetBrushColor(FLinearColor(.09f, .06f, .035f));
        WindowBorder->SetPadding(FMargin(22));
        WindowBorder->SetClipping(EWidgetClipping::ClipToBoundsAlways);
        auto* FrameSlot = Layers->AddChildToOverlay(WindowBorder);
        FrameSlot->SetHorizontalAlignment(HAlign_Fill);
        FrameSlot->SetVerticalAlignment(VAlign_Fill);
        auto* Shell = WidgetTree->ConstructWidget<UVerticalBox>();
        WindowBorder->SetContent(Shell);

        auto* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
        Add(*Shell, Header);
        TitleText = MakeText(NSLOCTEXT("RPGShop", "Title", "상점"), 22);
        Header->AddChildToHorizontalBox(TitleText)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        CloseButton = MakeButton(TEXT("ShopClose"), NSLOCTEXT("RPGShop", "Close", "닫기"));
        Header->AddChildToHorizontalBox(CloseButton);

        auto* Tabs = WidgetTree->ConstructWidget<UHorizontalBox>();
        Add(*Shell, Tabs);
        BuyButton = MakeButton(TEXT("ShopBuyTab"), NSLOCTEXT("RPGShop", "BuyTab", "구매"));
        SellButton = MakeButton(TEXT("ShopSellTab"), NSLOCTEXT("RPGShop", "SellTab", "판매"));
        ReloadButton = MakeButton(TEXT("ShopRefresh"), NSLOCTEXT("RPGShop", "Refresh", "새로고침"));
        for (auto* Button : {BuyButton.Get(), SellButton.Get(), ReloadButton.Get()})
            Tabs->AddChildToHorizontalBox(Button)->SetPadding(FMargin(0, 0, 6, 0));
        WalletText = MakeText(FText::GetEmpty());
        CapacityText = MakeText(FText::GetEmpty(), 12);
        CapacityText->SetColorAndOpacity(FSlateColor(Muted));
        Add(*Shell, WalletText, 2);
        Add(*Shell, CapacityText, 2);

        auto* Body = WidgetTree->ConstructWidget<UHorizontalBox>();
        auto* BodySlot = Shell->AddChildToVerticalBox(Body);
        BodySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        BodySlot->SetPadding(FMargin(0, 8));
        ListPanel = WidgetTree->ConstructWidget<UVerticalBox>();
        DetailPanel = WidgetTree->ConstructWidget<UVerticalBox>();
        Body->AddChildToHorizontalBox(ListPanel)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* DetailSlot = Body->AddChildToHorizontalBox(DetailPanel);
        DetailSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        DetailSlot->SetPadding(FMargin(12, 0, 0, 0));
        ModeText = MakeText(NSLOCTEXT("RPGShop", "Products", "판매 중인 물품"), 16);
        Add(*ListPanel, ModeText, 3);
        UnavailableCheck = WidgetTree->ConstructWidget<UCheckBox>();
        UnavailableCheck->SetContent(MakeText(NSLOCTEXT("RPGShop", "ShowUnavailable", "판매할 수 없는 물품 보기"), 12));
        Add(*ListPanel, UnavailableCheck, 3);
        ListScroll = WidgetTree->ConstructWidget<UScrollBox>();
        ListScroll->SetScrollWhenFocusChanges(EScrollWhenFocusChanges::InstantScroll);
        ListScroll->SetNavigationDestination(EDescendantScrollDestination::IntoView);
        ListScroll->SetClipping(EWidgetClipping::ClipToBoundsAlways);
        ListPanel->AddChildToVerticalBox(ListScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        ItemRows = WidgetTree->ConstructWidget<UVerticalBox>();
        ListScroll->AddChild(ItemRows);
        EmptyText = MakeText(FText::GetEmpty());
        Add(*ListPanel, EmptyText);

        BackButton = MakeButton(TEXT("ShopBackToList"), NSLOCTEXT("RPGShop", "BackToList", "물품 목록으로"));
        Add(*DetailPanel, BackButton, 2);
        DetailScroll = WidgetTree->ConstructWidget<UScrollBox>();
        DetailScroll->SetClipping(EWidgetClipping::ClipToBoundsAlways);
        DetailPanel->AddChildToVerticalBox(DetailScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        DetailText = MakeText(NSLOCTEXT("RPGShop", "Choose", "목록에서 물품을 선택하세요."), 16);
        DetailScroll->AddChild(DetailText);

        QuantityRow = WidgetTree->ConstructWidget<UHorizontalBox>();
        Add(*Shell, QuantityRow, 3);
        QuantityRow->AddChildToHorizontalBox(MakeText(NSLOCTEXT("RPGShop", "Quantity", "수량  ")))->SetVerticalAlignment(VAlign_Center);
        MinusButton = MakeButton(TEXT("ShopMinus"), FText::FromString(TEXT("−")));
        PlusButton = MakeButton(TEXT("ShopPlus"), FText::FromString(TEXT("+")));
        MaximumButton = MakeButton(TEXT("ShopMaximum"), NSLOCTEXT("RPGShop", "Maximum", "최대"));
        QuantityInput = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("ShopQuantity"));
        auto QuantityStyle = QuantityInput->GetWidgetStyle();
        QuantityStyle.SetBackgroundColor(FSlateColor(FLinearColor(.055f, .04f, .025f)));
        QuantityStyle.SetForegroundColor(FSlateColor(Ink));
        QuantityStyle.SetFocusedForegroundColor(FSlateColor(Ink));
        QuantityStyle.SetReadOnlyForegroundColor(FSlateColor(Ink));
        QuantityInput->SetWidgetStyle(QuantityStyle);
        QuantityInput->SetText(FText::FromString(TEXT("1")));
        QuantityInput->SetSelectAllTextWhenFocused(true);
        QuantityInput->SetClearKeyboardFocusOnCommit(false);
        QuantityRow->AddChildToHorizontalBox(MinusButton);
        auto* QuantitySlot = QuantityRow->AddChildToHorizontalBox(QuantityInput);
        QuantitySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        QuantitySlot->SetPadding(FMargin(6, 0));
        QuantityRow->AddChildToHorizontalBox(PlusButton)->SetPadding(FMargin(0, 0, 6, 0));
        QuantityRow->AddChildToHorizontalBox(MaximumButton);
        QuantityHint = MakeText(FText::GetEmpty(), 12);
        Add(*Shell, QuantityHint, 2);
        TotalText = MakeText(FText::GetEmpty(), 16);
        Add(*Shell, TotalText, 3);

        ReviewText = MakeText(FText::GetEmpty());
        Add(*Shell, ReviewText, 3);
        ExecuteButton = MakeButton(TEXT("ShopExecute"), NSLOCTEXT("RPGShop", "ChooseBeforeBuy", "구매할 물품을 선택하세요"));
        Add(*Shell, ExecuteButton, 3);
        ReviewActions = WidgetTree->ConstructWidget<UHorizontalBox>();
        Add(*Shell, ReviewActions, 3);
        DismissButton = MakeButton(TEXT("ShopDismissSale"), NSLOCTEXT("RPGShop", "EditSale", "수정하기"));
        ConfirmButton = MakeButton(TEXT("ShopConfirmSale"), NSLOCTEXT("RPGShop", "ConfirmSale", "위 내용으로 판매"));
        ReviewActions->AddChildToHorizontalBox(DismissButton)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* ConfirmSlot = ReviewActions->AddChildToHorizontalBox(ConfirmButton);
        ConfirmSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        ConfirmSlot->SetPadding(FMargin(8, 0, 0, 0));
        PendingText = MakeText(FText::GetEmpty());
        StatusText = MakeText(FText::GetEmpty(), 12);
        Add(*Shell, PendingText, 2);
        Add(*Shell, StatusText, 2);
        auto* Recovery = WidgetTree->ConstructWidget<UWrapBox>();
        Recovery->SetInnerSlotPadding(FVector2D(6, 4));
        Add(*Shell, Recovery, 2);
        RecoverButton = MakeButton(TEXT("ShopRecover"), NSLOCTEXT("RPGShop", "Recover", "거래 결과 확인"));
        RetryButton = MakeButton(TEXT("ShopRetry"), NSLOCTEXT("RPGShop", "Retry", "이전 거래 다시 시도"));
        AssetsButton = MakeButton(TEXT("ShopAssets"), NSLOCTEXT("RPGShop", "Assets", "가방 · 재화 갱신"));
        StorageButton = MakeButton(TEXT("ShopStorage"), NSLOCTEXT("RPGShop", "Storage", "복구 기록 다시 확인"));
        for (auto* Button : {RecoverButton.Get(), RetryButton.Get(), AssetsButton.Get(), StorageButton.Get()}) Recovery->AddChildToWrapBox(Button);

        BuyButton->OnClicked.AddUniqueDynamic(this, &ThisClass::BuyTab);
        SellButton->OnClicked.AddUniqueDynamic(this, &ThisClass::SellTab);
        CloseButton->OnClicked.AddUniqueDynamic(this, &ThisClass::CloseScreen);
        ReloadButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Reload);
        UnavailableCheck->OnCheckStateChanged.AddUniqueDynamic(this, &ThisClass::ShowUnavailable);
        QuantityInput->OnTextChanged.AddUniqueDynamic(this, &ThisClass::QuantityEdited);
        QuantityInput->OnTextCommitted.AddUniqueDynamic(this, &ThisClass::QuantityCommitted);
        MinusButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Decrease);
        PlusButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Increase);
        MaximumButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Maximum);
        ExecuteButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Execute);
        ConfirmButton->OnClicked.AddUniqueDynamic(this, &ThisClass::ConfirmSale);
        DismissButton->OnClicked.AddUniqueDynamic(this, &ThisClass::DismissSale);
        BackButton->OnClicked.AddUniqueDynamic(this, &ThisClass::BackToList);
        RecoverButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Recover);
        RetryButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Retry);
        AssetsButton->OnClicked.AddUniqueDynamic(this, &ThisClass::ReloadAssets);
        StorageButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RecheckStorage);
    }
    SetIsFocusable(true);
    bRowsDirty = true;
    UpdateLayout(FVector2D(992, 712));
    RefreshView();
    return Super::RebuildWidget();
}

void URPGShopWidget::Subscribe()
{
    if (auto Current = Owner.Pin())
    {
        Current->OnViewChanged.Remove(ChangedHandle);
        ChangedHandle = Current->OnViewChanged.AddUObject(this, &ThisClass::RefreshView);
    }
}

void URPGShopWidget::SetShopOwner(TSharedRef<FRPGShopCoordinator> InOwner)
{
    if (auto Old = Owner.Pin()) { Old->OnViewChanged.Remove(ChangedHandle); Old->DismissSale(); }
    Owner = InOwner;
    ChangedHandle.Reset();
    BoundContext = {};
    DisplayedSnapshot.Invalidate();
    DisplayedReview.Invalidate();
    bClosing = false;
    bSell = false;
    ClearSelection();
    Subscribe();
    RefreshView();
    if (IsInViewport())
    {
        const auto View = InOwner->GetView();
        if (View.bCanRefresh && !View.bHasSnapshot && !View.bHasPending) InOwner->Refresh();
    }
}

void URPGShopWidget::NativeConstruct()
{
    Super::NativeConstruct();
    bClosing = false;
    Subscribe();
    if (auto* Controller = GetOwningPlayer(); Controller && Controller->IsLocalController())
    {
        const auto* Skills = Controller->FindComponentByClass<URPGSkillWindowComponent>();
        InputState = Skills ? Skills->GetWindowInputState() : nullptr;
        if (!InputState) InputState = MakeShared<FRPGWindowInputState>();
        if (InputState->BeginWindow(*Controller, *this, 150)) InputState->FocusWindow(*this);
    }
    RefreshView();
    if (auto Current = Owner.Pin())
    {
        const auto View = Current->GetView();
        if (View.bCanRefresh && !View.bHasSnapshot && !View.bHasPending) Current->Refresh();
    }
    if (CloseButton && GetOwningPlayer()) CloseButton->SetUserFocus(GetOwningPlayer());
}

void URPGShopWidget::ReleaseInput()
{
    auto Previous = MoveTemp(InputState);
    if (Previous) Previous->EndWindow(*this);
}

void URPGShopWidget::NativeDestruct()
{
    if (auto Current = Owner.Pin()) { Current->OnViewChanged.Remove(ChangedHandle); Current->DismissSale(); }
    ChangedHandle.Reset();
    DisplayedReview.Invalidate();
    ReleaseInput();
    Super::NativeDestruct();
}

void URPGShopWidget::NativeTick(const FGeometry& Geometry, float Seconds)
{
    Super::NativeTick(Geometry, Seconds);
    UpdateLayout(Geometry.GetLocalSize());
    ViewRefreshElapsed += Seconds;
    if (ViewRefreshElapsed >= .25f)
    {
        ViewRefreshElapsed = 0;
        RefreshView();
    }
}

void URPGShopWidget::UpdateLayout(FVector2D Viewport)
{
    if (!WindowBounds || !WindowBorder || !FMath::IsFinite(Viewport.X) || !FMath::IsFinite(Viewport.Y)
        || Viewport.X <= 0 || Viewport.Y <= 0) return;
    const double Width = FMath::Min(960., FMath::Max(1., Viewport.X - FMath::Min(32., Viewport.X * .08)));
    const double Height = FMath::Min(680., FMath::Max(1., Viewport.Y - FMath::Min(32., Viewport.Y * .08)));
    WindowBounds->SetWidthOverride(static_cast<float>(Width));
    WindowBounds->SetHeightOverride(static_cast<float>(Height));
    WindowBorder->SetPadding(FMargin(Width < 600 ? 12 : 22));
    bNarrow = Width < 740;
    RPGShopUI::Visible(ListPanel, !bNarrow || !bDetailPage);
    RPGShopUI::Visible(DetailPanel, !bNarrow || bDetailPage);
    RPGShopUI::Visible(BackButton, bNarrow && bDetailPage);
}

void URPGShopWidget::ClearSelection()
{
    SelectedOffer.Reset();
    SelectedItem.Invalidate();
    InputError = FText::GetEmpty();
    bDetailPage = false;
    bRowsDirty = true;
    SetQuantity(1);
}

void URPGShopWidget::SetQuantity(int32 Quantity)
{
    if (!QuantityInput) return;
    TGuardValue<bool> Guard(bUpdating, true);
    QuantityInput->SetText(FText::FromString(LexToString(Quantity)));
}

bool URPGShopWidget::ReadQuantity(int32& Out) const
{
    Out = 0;
    if (!QuantityInput) return false;
    const FString Value = QuantityInput->GetText().ToString();
    if (Value.IsEmpty() || Value.Len() > 10) return false;
    int64 Parsed = 0;
    for (TCHAR Character : Value)
    {
        if (Character < '0' || Character > '9') return false;
        Parsed = Parsed * 10 + Character - '0';
        if (Parsed > MAX_int32) return false;
    }
    Out = static_cast<int32>(Parsed);
    return Out > 0;
}

int32 URPGShopWidget::MaximumQuantity(const FRPGShopScreenView& View) const
{
    if (!View.bHasSnapshot) return 0;
    if (bSell)
    {
        const auto* Item = View.Snapshot.SaleItems.FindByPredicate([&](const auto& Entry) { return Entry.ItemId == SelectedItem; });
        return Item && Item->bCanSell ? FMath::Max(0, FMath::Min(Item->Quantity, Item->MaximumQuantity)) : 0;
    }
    const auto* Offer = View.Snapshot.Offers.FindByPredicate([&](const auto& Entry) { return Entry.OfferId == SelectedOffer; });
    if (!Offer || Offer->UnitPrice <= 0 || View.Snapshot.AvailableBalance < 0) return 0;
    const int64 MoneyLimit = View.Snapshot.AvailableBalance / Offer->UnitPrice;
    return static_cast<int32>(FMath::Max<int64>(0, FMath::Min<int64>(MoneyLimit,
        FMath::Min(Offer->MaximumQuantity, Offer->MaximumPurchasable))));
}

void URPGShopWidget::RebuildRows(const FRPGShopScreenView& View)
{
    if (!ItemRows) return;
    ItemRows->ClearChildren();
    RowKeys.Reset();
    const auto AddRow = [this](const FText& Label, const FText& Help, FSimpleDelegate OnSelect)
    {
        auto* Row = WidgetTree->ConstructWidget<URPGShopRowButton>();
        StyleButton(*Row);
        Row->SetContent(MakeText(Label));
        Row->SetToolTipText(Help);
        Row->BindChoice(MoveTemp(OnSelect));
        RPGShopUI::Add(*ItemRows, Row, 3);
    };
    int32 Count = 0;
    if (View.bHasSnapshot && bSell)
    {
        for (const auto& Item : View.Snapshot.SaleItems)
        {
            if (!Item.bCanSell && !bShowUnavailable) continue;
            const auto State = !Item.bCanSell ? NSLOCTEXT("RPGShop", "Unavailable", "판매 불가")
                : Item.ItemId == SelectedItem ? NSLOCTEXT("RPGShop", "Selected", "선택됨") : FText::GetEmpty();
            const FText Label = FText::Format(NSLOCTEXT("RPGShop", "SaleRow", "{0}  {1}\n보유 {2}개 · 개당 {3}"),
                Item.DisplayName, State, FText::AsNumber(Item.Quantity), RPGShopUI::Money(Item.UnitPrice, View.Snapshot.CurrencyCode));
            const FGuid ItemId = Item.ItemId, Token = View.SnapshotToken;
            AddRow(Label, Item.bCanSell ? NSLOCTEXT("RPGShop", "SaleSelect", "선택 후 판매 수량을 확인하세요.") : Item.Reason,
                FSimpleDelegate::CreateWeakLambda(this, [this, Token, ItemId] { SelectItem(Token, ItemId); }));
            RowKeys.Add(ItemId.ToString(EGuidFormats::Digits));
            ++Count;
        }
    }
    else if (View.bHasSnapshot)
    {
        for (const auto& Offer : View.Snapshot.Offers)
        {
            const FText State = Offer.OfferId == SelectedOffer ? NSLOCTEXT("RPGShop", "Selected", "선택됨") : FText::GetEmpty();
            const FText Label = FText::Format(NSLOCTEXT("RPGShop", "BuyRow", "{0}  {1}\n개당 {2}"),
                Offer.DisplayName, State, RPGShopUI::Money(Offer.UnitPrice, View.Snapshot.CurrencyCode));
            const FString Id = Offer.OfferId;
            const FGuid Token = View.SnapshotToken;
            AddRow(Label, NSLOCTEXT("RPGShop", "BuySelect", "선택 후 구매 수량을 정하세요."),
                FSimpleDelegate::CreateWeakLambda(this, [this, Token, Id] { SelectOffer(Token, Id); }));
            RowKeys.Add(Id);
            ++Count;
        }
    }
    if (EmptyText)
    {
        RPGShopUI::Visible(EmptyText, Count == 0);
        EmptyText->SetText(!View.bHasSnapshot ? (View.bBusy
            ? NSLOCTEXT("RPGShop", "LoadingRows", "물품을 불러오는 중입니다.")
            : NSLOCTEXT("RPGShop", "ReadFirst", "상점 정보를 불러오세요."))
            : !bSell ? NSLOCTEXT("RPGShop", "NoOffers", "현재 판매 중인 물품이 없습니다.")
            : View.Snapshot.SaleItems.IsEmpty() ? NSLOCTEXT("RPGShop", "EmptyBag", "가방이 비었습니다.")
            : NSLOCTEXT("RPGShop", "NoSaleItems", "판매할 수 있는 물품이 없습니다.\n위에서 판매 불가 물품과 이유를 확인할 수 있습니다."));
    }
    bRowsDirty = false;
}

void URPGShopWidget::RefreshDetails(const FRPGShopScreenView& View)
{
    const FRPGShopOffer* Offer = !bSell ? View.Snapshot.Offers.FindByPredicate([&](const auto& Entry) { return Entry.OfferId == SelectedOffer; }) : nullptr;
    const FRPGShopSaleItem* Item = bSell ? View.Snapshot.SaleItems.FindByPredicate([&](const auto& Entry) { return Entry.ItemId == SelectedItem; }) : nullptr;
    const bool bSelected = Offer || Item;
    const int32 Limit = MaximumQuantity(View);
    const int64 Price = Offer ? Offer->UnitPrice : Item ? Item->UnitPrice : 0;
    int32 Quantity = 0;
    const bool bValid = ReadQuantity(Quantity) && Quantity <= Limit && Price > 0 && Price <= MAX_int64 / Quantity;
    const int64 Total = bValid ? Price * Quantity : 0;
    const bool bBalanceValid = bValid && (bSell ? View.Snapshot.Balance <= MAX_int64 - Total : View.Snapshot.Balance >= Total);
    const bool bCanEdit = bSelected && !View.bHasReview && !View.bHasPending && !View.bBusy && View.bCanTrade;

    if (DetailText)
    {
        FText Details = NSLOCTEXT("RPGShop", "Choose", "목록에서 물품을 선택하세요.");
        if (Offer) Details = FText::Format(NSLOCTEXT("RPGShop", "BuyDetail", "{0}\n\n개당 {1}\n현재 구매 가능: 최대 {2}개"),
            Offer->DisplayName, RPGShopUI::Money(Price, View.Snapshot.CurrencyCode), FText::AsNumber(Limit));
        if (Item) Details = FText::Format(NSLOCTEXT("RPGShop", "SaleDetail", "{0}\n\n보유 {1}개\n개당 받는 금액: {2}\n\n{3}"),
            Item->DisplayName, FText::AsNumber(Item->Quantity), RPGShopUI::Money(Price, View.Snapshot.CurrencyCode),
            Item->bCanSell ? NSLOCTEXT("RPGShop", "NoBuyback", "판매한 물품은 재매입할 수 없습니다.") : Item->Reason);
        DetailText->SetText(Details);
    }
    if (QuantityInput) QuantityInput->SetIsEnabled(bCanEdit && Limit > 0);
    if (MinusButton) MinusButton->SetIsEnabled(bCanEdit && Quantity > 1);
    if (PlusButton) PlusButton->SetIsEnabled(bCanEdit && Quantity > 0 && Quantity < Limit);
    if (MaximumButton) MaximumButton->SetIsEnabled(bCanEdit && Limit > 0);
    if (QuantityHint)
    {
        FText Hint;
        if (!InputError.IsEmpty()) Hint = InputError;
        else if (bSelected && Limit == 0 && Offer)
            Hint = View.Snapshot.AvailableBalance < Price ? NSLOCTEXT("RPGShop", "NotEnoughMoney", "사용 가능한 재화가 부족합니다.")
                : NSLOCTEXT("RPGShop", "NoCapacity", "지금 구매할 수 없습니다. 가방 공간과 물품 상태를 확인하세요.");
        else if (bSelected && Limit > 0 && !bValid)
            Hint = FText::Format(NSLOCTEXT("RPGShop", "QuantityRange", "1~{0} 사이의 정수 수량을 입력하세요."), FText::AsNumber(Limit));
        QuantityHint->SetText(Hint);
    }
    if (TotalText)
    {
        TotalText->SetText(bBalanceValid ? FText::Format(bSell
            ? NSLOCTEXT("RPGShop", "SaleTotal", "받을 금액 {0} · 판매 후 {1}")
            : NSLOCTEXT("RPGShop", "BuyTotal", "합계 {0} · 구매 후 {1}"),
                RPGShopUI::Money(Total, View.Snapshot.CurrencyCode),
                RPGShopUI::Money(bSell ? View.Snapshot.Balance + Total : View.Snapshot.Balance - Total, View.Snapshot.CurrencyCode))
            : FText::GetEmpty());
    }
    if (ExecuteButton)
    {
        ExecuteButton->SetIsEnabled(View.bCanTrade && !View.bHasReview && bBalanceValid && (!Item || Item->bCanSell));
        RPGShopUI::Visible(ExecuteButton, !View.bHasReview);
        LabelButton(ExecuteButton, bBalanceValid ? FText::Format(bSell
            ? NSLOCTEXT("RPGShop", "ReviewSaleButton", "{0}개 판매 내용 확인 · {1} 받기")
            : NSLOCTEXT("RPGShop", "BuyButton", "{0}개 구매 · {1}"), FText::AsNumber(Quantity), RPGShopUI::Money(Total, View.Snapshot.CurrencyCode))
            : bSell ? NSLOCTEXT("RPGShop", "ChooseBeforeSell", "판매할 물품과 수량을 확인하세요")
                : NSLOCTEXT("RPGShop", "ChooseBeforeBuy", "구매할 물품과 수량을 확인하세요"));
    }
}

void URPGShopWidget::RefreshView()
{
    if (bClosing) return;
    const auto Current = Owner.Pin();
    const auto View = Current ? Current->GetView() : FRPGShopScreenView{};
    if (BoundContext.IsValid() && (!(BoundContext == View.Context) || !View.bAvailable)) { CloseScreen(); return; }
    if (View.Context.IsValid()) BoundContext = View.Context;
    if (DisplayedSnapshot != View.SnapshotToken)
    {
        DisplayedSnapshot = View.SnapshotToken;
        ClearSelection();
    }
    if (TitleText) TitleText->SetText(View.Snapshot.DisplayName.IsEmpty() ? NSLOCTEXT("RPGShop", "Title", "상점")
        : FText::Format(NSLOCTEXT("RPGShop", "NamedShop", "{0} · 상점"), View.Snapshot.DisplayName));
    if (WalletText) WalletText->SetText(View.bHasSnapshot ? FText::Format(NSLOCTEXT("RPGShop", "Wallet", "보유 {0} · 사용 가능 {1}"),
        RPGShopUI::Money(View.Snapshot.Balance, View.Snapshot.CurrencyCode), RPGShopUI::Money(View.Snapshot.AvailableBalance, View.Snapshot.CurrencyCode)) : FText::GetEmpty());
    if (CapacityText) CapacityText->SetText(View.Snapshot.InventoryFreeSlots.Num() == 3 ? FText::Format(
        NSLOCTEXT("RPGShop", "BagSpace", "가방 빈칸  장비 {0} · 소비 {1} · 기타 {2}"),
        FText::AsNumber(View.Snapshot.InventoryFreeSlots[0]), FText::AsNumber(View.Snapshot.InventoryFreeSlots[1]),
        FText::AsNumber(View.Snapshot.InventoryFreeSlots[2])) : FText::GetEmpty());
    LabelButton(BuyButton, !bSell ? NSLOCTEXT("RPGShop", "BuyActive", "구매 · 선택됨") : NSLOCTEXT("RPGShop", "BuyTab", "구매"));
    LabelButton(SellButton, bSell ? NSLOCTEXT("RPGShop", "SellActive", "판매 · 선택됨") : NSLOCTEXT("RPGShop", "SellTab", "판매"));
    if (ModeText) ModeText->SetText(!bSell ? NSLOCTEXT("RPGShop", "Products", "판매 중인 물품")
        : bShowUnavailable ? NSLOCTEXT("RPGShop", "AllOwnedItems", "내 물품 판매 · 판매 불가 포함")
            : NSLOCTEXT("RPGShop", "EligibleOwnedItems", "내 물품 판매 · 판매 가능 물품만 표시"));
    RPGShopUI::Visible(UnavailableCheck, bSell);
    if (ReloadButton) ReloadButton->SetIsEnabled(View.bCanRefresh);
    if (bRowsDirty) RebuildRows(View);
    DisplayedReview = View.ReviewToken;
    RPGShopUI::Visible(ReviewActions, View.bHasReview);
    RPGShopUI::Visible(ReviewText, View.bHasReview);
    if (ReviewText) ReviewText->SetText(View.ReviewText);
    if (ConfirmButton) ConfirmButton->SetIsEnabled(View.bCanConfirm);
    if (PendingText) { PendingText->SetText(View.PendingText); RPGShopUI::Visible(PendingText, !View.PendingText.IsEmpty()); }
    if (StatusText) StatusText->SetText(Current ? View.Status : NSLOCTEXT("RPGShop", "NoOwner", "상점을 이용할 수 없습니다."));
    const auto RecoveryButton = [](UButton* Button, bool bShow)
    {
        RPGShopUI::Visible(Button, bShow);
        if (Button) Button->SetIsEnabled(bShow);
    };
    RecoveryButton(RecoverButton, View.bCanRecover);
    RecoveryButton(RetryButton, View.bCanRetry);
    RecoveryButton(AssetsButton, View.bCanAssets);
    RecoveryButton(StorageButton, View.bCanStorage);
    RefreshDetails(View);
    RPGShopUI::Visible(ListPanel, !bNarrow || !bDetailPage);
    RPGShopUI::Visible(DetailPanel, !bNarrow || bDetailPage);
    RPGShopUI::Visible(BackButton, bNarrow && bDetailPage);
    UpdateNavigation();
}

void URPGShopWidget::UpdateNavigation()
{
    TArray<UWidget*> Targets;
    const auto AddTarget = [&Targets](UWidget* Widget)
    {
        if (Widget && Widget->IsVisible() && Widget->GetIsEnabled()) Targets.Add(Widget);
    };
    AddTarget(CloseButton);
    AddTarget(BuyButton);
    AddTarget(SellButton);
    AddTarget(ReloadButton);
    if (!bNarrow || !bDetailPage)
    {
        AddTarget(UnavailableCheck);
        if (ItemRows) for (auto* Row : ItemRows->GetAllChildren()) AddTarget(Row);
    }
    AddTarget(BackButton);
    AddTarget(MinusButton);
    AddTarget(QuantityInput);
    AddTarget(PlusButton);
    AddTarget(MaximumButton);
    AddTarget(ExecuteButton);
    if (ReviewActions && ReviewActions->IsVisible())
    {
        AddTarget(DismissButton);
        AddTarget(ConfirmButton);
    }
    AddTarget(RecoverButton);
    AddTarget(RetryButton);
    AddTarget(AssetsButton);
    AddTarget(StorageButton);
    for (int32 Index = 0; Index < Targets.Num(); ++Index)
    {
        Targets[Index]->SetNavigationRuleExplicit(EUINavigation::Next, Targets[(Index + 1) % Targets.Num()]);
        Targets[Index]->SetNavigationRuleExplicit(EUINavigation::Previous, Targets[(Index + Targets.Num() - 1) % Targets.Num()]);
    }
}

void URPGShopWidget::SelectOffer(FGuid Snapshot, FString Id)
{
    const auto Current = Owner.Pin();
    if (!Current || Current->GetView().SnapshotToken != Snapshot) return;
    Current->DismissSale();
    SelectedOffer = MoveTemp(Id);
    SelectedItem.Invalidate();
    InputError = FText::GetEmpty();
    SetQuantity(1);
    bDetailPage = true;
    bRowsDirty = true;
    RefreshView();
    if (bNarrow && BackButton && GetOwningPlayer()) BackButton->SetUserFocus(GetOwningPlayer());
    else FocusSelectedRow();
}

void URPGShopWidget::SelectItem(FGuid Snapshot, FGuid Item)
{
    const auto Current = Owner.Pin();
    if (!Current || Current->GetView().SnapshotToken != Snapshot) return;
    Current->DismissSale();
    SelectedItem = Item;
    SelectedOffer.Reset();
    InputError = FText::GetEmpty();
    SetQuantity(1);
    bDetailPage = true;
    bRowsDirty = true;
    RefreshView();
    if (bNarrow && BackButton && GetOwningPlayer()) BackButton->SetUserFocus(GetOwningPlayer());
    else FocusSelectedRow();
}

void URPGShopWidget::BuyTab()
{
    if (!bSell) return;
    if (auto Current = Owner.Pin()) Current->DismissSale();
    bSell = false;
    ClearSelection();
    if (ListScroll) ListScroll->ScrollToStart();
    RefreshView();
}

void URPGShopWidget::SellTab()
{
    if (bSell) return;
    if (auto Current = Owner.Pin()) Current->DismissSale();
    bSell = true;
    ClearSelection();
    if (ListScroll) ListScroll->ScrollToStart();
    RefreshView();
}

void URPGShopWidget::ShowUnavailable(bool bShow)
{
    if (bUpdating) return;
    if (auto Current = Owner.Pin()) Current->DismissSale();
    bShowUnavailable = bShow;
    ClearSelection();
    RefreshView();
}

void URPGShopWidget::Reload() { if (auto Current = Owner.Pin()) Current->Refresh(); }

void URPGShopWidget::QuantityEdited(const FText&)
{
    if (bUpdating) return;
    InputError = FText::GetEmpty();
    if (auto Current = Owner.Pin()) Current->DismissSale();
    RefreshView();
}

void URPGShopWidget::QuantityCommitted(const FText&, ETextCommit::Type)
{
    // Enter in the editor only commits the text. It never performs a transaction.
    RefreshView();
}

void URPGShopWidget::Decrease()
{
    int32 Quantity = 0;
    if (!ReadQuantity(Quantity) || Quantity <= 1) return;
    if (auto Current = Owner.Pin()) Current->DismissSale();
    SetQuantity(Quantity - 1);
    RefreshView();
}

void URPGShopWidget::Increase()
{
    int32 Quantity = 0;
    const auto Current = Owner.Pin();
    if (!Current || !ReadQuantity(Quantity) || Quantity >= MaximumQuantity(Current->GetView())) return;
    Current->DismissSale();
    SetQuantity(Quantity + 1);
    RefreshView();
}

void URPGShopWidget::Maximum()
{
    const auto Current = Owner.Pin();
    if (!Current) return;
    const int32 Limit = MaximumQuantity(Current->GetView());
    if (Limit < 1) return;
    Current->DismissSale();
    SetQuantity(Limit);
    RefreshView();
}

void URPGShopWidget::Execute()
{
    const auto Current = Owner.Pin();
    if (!Current || !ExecuteButton || !ExecuteButton->GetIsEnabled()) return;
    const auto View = Current->GetView();
    int32 Quantity = 0;
    if (!View.bCanTrade || View.SnapshotToken != DisplayedSnapshot || !ReadQuantity(Quantity)
        || Quantity > MaximumQuantity(View)) return;
    if (bSell)
    {
        if (Current->ReviewSale(DisplayedSnapshot, SelectedItem, Quantity)
            && DismissButton && GetOwningPlayer()) DismissButton->SetUserFocus(GetOwningPlayer());
    }
    else
    {
        const FString Offer = SelectedOffer;
        const FGuid Token = DisplayedSnapshot;
        // Disarm before a synchronous callback can finish. A second click must select again.
        ClearSelection();
        ExecuteButton->SetIsEnabled(false);
        Current->Buy(Token, Offer, Quantity);
    }
    RefreshView();
}

void URPGShopWidget::ConfirmSale()
{
    const auto Current = Owner.Pin();
    if (!Current || !ConfirmButton || !ConfirmButton->GetIsEnabled() || !DisplayedReview.IsValid()) return;
    const FGuid Consent = DisplayedReview;
    DisplayedReview.Invalidate();
    ClearSelection();
    ConfirmButton->SetIsEnabled(false);
    Current->ConfirmSale(Consent);
    RefreshView();
}

void URPGShopWidget::DismissSale()
{
    if (auto Current = Owner.Pin()) Current->DismissSale();
    DisplayedReview.Invalidate();
    RefreshView();
    if (QuantityInput && GetOwningPlayer()) QuantityInput->SetUserFocus(GetOwningPlayer());
}

void URPGShopWidget::BackToList()
{
    if (auto Current = Owner.Pin()) Current->DismissSale();
    bDetailPage = false;
    RefreshView();
    FocusSelectedRow();
}

void URPGShopWidget::FocusSelectedRow()
{
    if (!ItemRows || !GetOwningPlayer()) return;
    const FString Key = bSell ? SelectedItem.ToString(EGuidFormats::Digits) : SelectedOffer;
    const int32 Index = RowKeys.IndexOfByKey(Key);
    if (Index == INDEX_NONE || Index >= ItemRows->GetChildrenCount()) return;
    UWidget* Row = ItemRows->GetChildAt(Index);
    Row->SetUserFocus(GetOwningPlayer());
    if (ListScroll) ListScroll->ScrollWidgetIntoView(Row, false, EDescendantScrollDestination::IntoView);
}

void URPGShopWidget::Recover() { if (auto Current = Owner.Pin()) Current->Recover(); }
void URPGShopWidget::Retry() { if (auto Current = Owner.Pin()) Current->Retry(); }
void URPGShopWidget::ReloadAssets() { if (auto Current = Owner.Pin()) Current->RefreshAssets(); }
void URPGShopWidget::RecheckStorage() { if (auto Current = Owner.Pin()) Current->RecheckStorage(); }

void URPGShopWidget::CloseScreen()
{
    if (bClosing) return;
    bClosing = true;
    if (auto Current = Owner.Pin()) { Current->OnViewChanged.Remove(ChangedHandle); Current->DismissSale(); }
    ChangedHandle.Reset();
    ReleaseInput();
    if (IsInViewport() || GetParent()) RemoveFromParent();
    OnClosed.Broadcast();
}

FReply URPGShopWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    if (Event.GetKey() == EKeys::Escape)
    {
        if (!Event.IsRepeat())
        {
            const auto Current = Owner.Pin();
            if (Current && Current->GetView().bHasReview) DismissSale();
            else if (bNarrow && bDetailPage) BackToList();
            else CloseScreen();
        }
        return FReply::Handled();
    }
    if (Event.IsRepeat() && (Event.GetKey() == EKeys::Enter || Event.GetKey() == EKeys::SpaceBar
        || Event.GetKey() == EKeys::Virtual_Accept || Event.GetKey() == EKeys::Gamepad_FaceButton_Bottom))
        return FReply::Handled();
    return Super::NativeOnPreviewKeyDown(Geometry, Event);
}
