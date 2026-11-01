#include "UI/Inventory/Projection/RPGRewardMailWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "UI/MVVM/RPGInventoryProjectionViewModel.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGRewardMailWidget)

namespace RPGRewardMailUI
{
FText GetEmptyText(const URPGInventoryProjectionViewModel& Model)
{
	if (Model.bInventoryAuthorityRevoked)
		return NSLOCTEXT("RPGRewardMail", "AccessEnded", "Reward mail access is unavailable.");
	if (Model.LoadState == ERPGInventoryProjectionLoadState::Ready)
		return NSLOCTEXT("RPGRewardMail", "Empty", "No unclaimed rewards.");
	if (Model.LoadState == ERPGInventoryProjectionLoadState::Failed)
		return NSLOCTEXT("RPGRewardMail", "LoadFailed", "Unable to load reward mail. Retry item load.");
	return NSLOCTEXT("RPGRewardMail", "NotReady", "Reward mail is not ready yet.");
}

}

void URPGRewardMailEntryWidget::InitializeEntry(
	const FRPGInventoryProjectionEntry& Entry, URPGInventoryProjectionViewModel* ViewModel)
{
	Attachment = Entry;
	EntryViewModel = ViewModel;
	if (MailClaimFeedback)
	{
		MailClaimFeedback->SetText(FText::GetEmpty());
		MailClaimFeedback->SetVisibility(ESlateVisibility::Collapsed);
	}
	RefreshAttachmentPresentation();
}

void URPGRewardMailEntryWidget::RefreshAttachmentPresentation()
{
	if (!MailItemName) return;
	const auto Presentation = EntryViewModel
		? EntryViewModel->GetItemPresentation(Attachment.GetItemId())
		: URPGInventoryProjectionViewModel::FItemPresentation();
	const FText Name = Presentation.DisplayName.IsEmpty()
		? FText::FromName(Attachment.GetDefinitionId().PrimaryAssetName) : Presentation.DisplayName;
	MailItemName->SetText(FText::Format(NSLOCTEXT("RPGRewardMail", "AttachmentLabel", "{0}  x{1}"),
		Name, FText::AsNumber(Attachment.GetQuantity())));
	const FText Details = Presentation.GetDetailsText();
	SetToolTipText(Details.IsEmpty() ? Name : Details);
	RefreshClaimPresentation();
}

void URPGRewardMailEntryWidget::RefreshClaimPresentation()
{
	FRPGMailClaimPresentation Presentation;
	if (EntryViewModel) Presentation = EntryViewModel->GetMailClaimPresentation(Attachment.GetItemId());
	else
	{
		Presentation.ActionLabel = NSLOCTEXT("RPGRewardMail", "ClaimUnavailableLabel", "Unavailable");
		Presentation.Reason = NSLOCTEXT("RPGRewardMail", "ClaimUnavailableReason", "Items are not ready for claiming.");
	}
	if (ClaimButton)
	{
		ClaimButton->SetIsEnabled(Presentation.State == ERPGMailClaimState::Available);
	}
	// The authored button may contain an icon/size box as well as its label.
	if (ClaimButtonText) ClaimButtonText->SetText(Presentation.ActionLabel);
	if (MailClaimFeedback)
	{
		MailClaimFeedback->SetText(Presentation.Reason);
		MailClaimFeedback->SetVisibility(Presentation.Reason.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
}

TSharedRef<SWidget> URPGRewardMailEntryWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		WidgetTree->RootWidget = Row;
		auto* DetailsColumn = WidgetTree->ConstructWidget<UVerticalBox>();
		UHorizontalBoxSlot* DetailsSlot = Row->AddChildToHorizontalBox(DetailsColumn);
		DetailsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		DetailsSlot->SetPadding(FMargin(8.f));
		MailItemName = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MailItemName"));
		MailItemName->SetAutoWrapText(true);
		FSlateFontInfo NameFont = MailItemName->GetFont();
		NameFont.Size = 14;
		MailItemName->SetFont(NameFont);
		DetailsColumn->AddChildToVerticalBox(MailItemName);
		MailClaimFeedback = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MailClaimFeedback"));
		MailClaimFeedback->SetAutoWrapText(true);
		MailClaimFeedback->SetVisibility(ESlateVisibility::Collapsed);
		MailClaimFeedback->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.68f, 0.35f, 1.f)));
		FSlateFontInfo FeedbackFont = MailClaimFeedback->GetFont();
		FeedbackFont.Size = 11;
		MailClaimFeedback->SetFont(FeedbackFont);
		DetailsColumn->AddChildToVerticalBox(MailClaimFeedback)->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));
		ClaimButton = WidgetTree->ConstructWidget<UButton>(
			UButton::StaticClass(), TEXT("ClaimButton"));
		ClaimButtonText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ClaimButtonText"));
		ClaimButtonText->SetText(NSLOCTEXT("RPGRewardMail", "Claim", "Claim"));
		ClaimButton->SetContent(ClaimButtonText);
		Row->AddChildToHorizontalBox(ClaimButton)->SetPadding(FMargin(8.f));
	}
	RefreshAttachmentPresentation();
	return Super::RebuildWidget();
}

void URPGRewardMailEntryWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RefreshAttachmentPresentation();
	if (ClaimButton) ClaimButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleClaimClicked);
}

void URPGRewardMailEntryWidget::NativeDestruct()
{
	if (ClaimButton)
	{
		ClaimButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleClaimClicked);
	}
	Super::NativeDestruct();
}

void URPGRewardMailEntryWidget::HandleClaimClicked()
{
	// Also guard synthetic delegate dispatch; a disabled button alone is not a command boundary.
	if (!EntryViewModel || EntryViewModel->GetMailClaimPresentation(Attachment.GetItemId()).State != ERPGMailClaimState::Available)
	{
		RefreshClaimPresentation();
		return;
	}
	FGuid RequestId;
	const bool bSubmitted = EntryViewModel->RequestClaimMailToFirstFreeSlot(Attachment.GetItemId(), RequestId);
	RefreshClaimPresentation();
	if (!bSubmitted && MailClaimFeedback && MailClaimFeedback->GetText().IsEmpty())
	{
		MailClaimFeedback->SetText(NSLOCTEXT("RPGRewardMail", "NoSlot",
			"Unable to claim: inventory is full or item state is not ready."));
		MailClaimFeedback->SetVisibility(ESlateVisibility::Visible);
	}
}

FText URPGRewardMailEntryWidget::GetFeedbackText() const
{
	return MailClaimFeedback ? MailClaimFeedback->GetText() : FText::GetEmpty();
}

URPGRewardMailWidget::URPGRewardMailWidget()
{
	MailEntryWidgetClass = URPGRewardMailEntryWidget::StaticClass();
}

TSharedRef<SWidget> URPGRewardMailWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>();
		Size->SetWidthOverride(720.f);
		Size->SetHeightOverride(540.f);
		WidgetTree->RootWidget = Size;
		UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
		Background->SetBrushColor(FLinearColor(0.04f, 0.055f, 0.075f, 0.98f));
		Background->SetPadding(FMargin(16.f));
		Size->SetContent(Background);
		UVerticalBox* Layout = WidgetTree->ConstructWidget<UVerticalBox>();
		Background->SetContent(Layout);
		UHorizontalBox* TitleRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		Layout->AddChildToVerticalBox(TitleRow)->SetPadding(FMargin(8.f));
		UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
		Title->SetText(NSLOCTEXT("RPGRewardMail", "Title", "Dungeon Reward Mail"));
		UHorizontalBoxSlot* TitleSlot = TitleRow->AddChildToHorizontalBox(Title);
		TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TitleSlot->SetVerticalAlignment(VAlign_Center);
		CloseWindowButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("CloseWindowButton"));
		CloseWindowButton->SetToolTipText(NSLOCTEXT("RPGItemUI", "CloseWindow", "Close window (Esc)"));
		UTextBlock* CloseLabel = WidgetTree->ConstructWidget<UTextBlock>();
		CloseLabel->SetText(FText::FromString(TEXT("X")));
		CloseWindowButton->SetContent(CloseLabel);
		TitleRow->AddChildToHorizontalBox(CloseWindowButton)->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));
		LoadStateText = WidgetTree->ConstructWidget<UTextBlock>();
		Layout->AddChildToVerticalBox(LoadStateText)->SetPadding(FMargin(8.f));
		UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
		Layout->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		MailRows = WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(), TEXT("MailRows"));
		Scroll->AddChild(MailRows);
		CommandResultText = WidgetTree->ConstructWidget<UTextBlock>();
		CommandResultText->SetAutoWrapText(true);
		Layout->AddChildToVerticalBox(CommandResultText)->SetPadding(FMargin(8.f));
	}
	return Super::RebuildWidget();
}

void URPGRewardMailWidget::RefreshInteractionFromViewModel()
{
	Super::RefreshInteractionFromViewModel();
	if (!InventoryViewModel || !MailRows) return;
	for (UWidget* Child : MailRows->GetAllChildren())
	{
		if (auto* Row = Cast<URPGRewardMailEntryWidget>(Child)) Row->RefreshClaimPresentation();
		else if (auto* Empty = Cast<UTextBlock>(Child); Empty && InventoryViewModel->MailItems.IsEmpty())
			Empty->SetText(RPGRewardMailUI::GetEmptyText(*InventoryViewModel));
	}
}

void URPGRewardMailWidget::RefreshFromViewModel()
{
	Super::RefreshFromViewModel();
	if (!InventoryViewModel || !MailRows)
	{
		return;
	}
	MailRows->ClearChildren();
	if (InventoryViewModel->MailItems.IsEmpty())
	{
		UTextBlock* Empty = WidgetTree->ConstructWidget<UTextBlock>();
		Empty->SetText(RPGRewardMailUI::GetEmptyText(*InventoryViewModel));
		MailRows->AddChildToVerticalBox(Empty);
	}
	for (const FRPGInventoryProjectionEntry& Entry : InventoryViewModel->MailItems)
	{
		URPGRewardMailEntryWidget* Row = CreateWidget<URPGRewardMailEntryWidget>(
			GetOwningPlayer(), MailEntryWidgetClass ? MailEntryWidgetClass.Get() : URPGRewardMailEntryWidget::StaticClass());
		if (Row)
		{
			Row->InitializeEntry(Entry, InventoryViewModel);
			MailRows->AddChildToVerticalBox(Row);
		}
	}
}
