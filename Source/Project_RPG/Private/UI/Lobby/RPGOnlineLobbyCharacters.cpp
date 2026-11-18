#include "UI/Lobby/RPGOnlineLobbyWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "Online/RPGOnlineLobbySubsystem.h"
#include "Online/RPGSteamAuthSubsystem.h"
#include "UI/Lobby/RPGCharacterLineupStage.h"
#include "UI/Lobby/RPGCharacterConfigurationWidget.h"
#include "UI/Lobby/RPGOnlineLobbyLayout.h"

#define LOCTEXT_NAMESPACE "RPGCharacterLineup"

UImage* URPGOnlineLobbyWidget::BuildLineupImage(UVerticalBox& Parent, bool bInteractive)
{
	auto* Scale = WidgetTree->ConstructWidget<UScaleBox>();
	Scale->SetStretch(EStretch::ScaleToFit);
	Scale->SetStretchDirection(EStretchDirection::DownOnly);
	Parent.AddChildToVerticalBox(Scale)->SetPadding(FMargin(0, 0, 0, 12));
	auto* Frame = WidgetTree->ConstructWidget<USizeBox>();
	Frame->SetWidthOverride(ARPGCharacterLineupStage::ImageWidth);
	Frame->SetHeightOverride(ARPGCharacterLineupStage::ImageHeight);
	Scale->SetContent(Frame);
	auto* Overlay = WidgetTree->ConstructWidget<UOverlay>();
	Frame->SetContent(Overlay);
	auto* Background = WidgetTree->ConstructWidget<UBorder>();
	Background->SetBrushColor(FLinearColor(.027f, .042f, .063f, 1.f));
	Background->SetPadding(FMargin(0));
	auto* BackgroundSlot = Overlay->AddChildToOverlay(Background);
	BackgroundSlot->SetHorizontalAlignment(HAlign_Fill);
	BackgroundSlot->SetVerticalAlignment(VAlign_Fill);
	auto* Photo = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),
		bInteractive ? TEXT("CharacterLineupImage") : TEXT("LobbyTitleStage"));
	Photo->SetVisibility(ESlateVisibility::HitTestInvisible);
	Background->SetContent(Photo);
	if (!bInteractive) return Photo;

	LineupFrame = Scale;
	LineupSlots = WidgetTree->ConstructWidget<UHorizontalBox>();
	auto* SlotsOverlay = Overlay->AddChildToOverlay(LineupSlots);
	SlotsOverlay->SetHorizontalAlignment(HAlign_Fill);
	SlotsOverlay->SetVerticalAlignment(VAlign_Fill);
	for (int32 Index = 0; Index < ARPGCharacterLineupStage::SlotCount; ++Index)
	{
		auto* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),
			FName(*FString::Printf(TEXT("CharacterLineupSlot%d"), Index)));
		FSlateBrush Clear;
		Clear.DrawAs = ESlateBrushDrawType::NoDrawType;
		FButtonStyle Style = Button->GetStyle();
		Style.SetNormal(Clear).SetDisabled(Clear);
		Style.Hovered.TintColor = FSlateColor(FLinearColor(.68f, .83f, 1.f, .28f));
		Style.Pressed.TintColor = FSlateColor(FLinearColor(.87f, .69f, .35f, .4f));
		Style.SetNormalPadding(FMargin(2.f)).SetPressedPadding(FMargin(2.f));
		Button->SetStyle(Style);
		auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
		Label->SetJustification(ETextJustify::Center);
		Label->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		auto Font = Label->GetFont();
		Font.Size = 17;
		Label->SetFont(Font);
		Label->SetShadowOffset(FVector2D(1, 1));
		Label->SetShadowColorAndOpacity(FLinearColor::Black);
		auto* ButtonSlot = CastChecked<UButtonSlot>(Button->AddChild(Label));
		ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
		ButtonSlot->SetVerticalAlignment(VAlign_Bottom);
		ButtonSlot->SetPadding(FMargin(6, 4, 6, 13));
		auto* CharacterSlot = LineupSlots->AddChildToHorizontalBox(Button);
		CharacterSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		CharacterSlot->SetVerticalAlignment(VAlign_Fill);
		LineupSlotButtons.Add(Button);
		LineupSlotLabels.Add(Label);
	}
	return Photo;
}

void URPGOnlineLobbyWidget::BuildCharacterContent(UVerticalBox& Parent)
{
	CharacterContent = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("OnlineLobbyCharacterContent"));
	CharacterContent->SetVisibility(ESlateVisibility::Collapsed);
	Parent.AddChildToVerticalBox(CharacterContent)->SetHorizontalAlignment(HAlign_Center);
	RPGOnlineLobbyLayout::Text(*WidgetTree, *CharacterContent, TEXT("CharacterServerName"),
		IsDevelopmentUIEnabled() ? LOCTEXT("DevelopmentServer", "개발 서버") : LOCTEXT("GameServer", "게임 서버"), 12);
	LineupImage = BuildLineupImage(*CharacterContent, true);
	auto* Rows = RPGOnlineLobbyLayout::Section(*WidgetTree, *CharacterContent, TEXT("OnlineLobbyCharacterSection"),
		NSLOCTEXT("RPGOnlineLobby", "MyCharacters", "내 캐릭터"));
	auto AddAction = [this, &Rows](FName Name, const FText& Label)
	{
		auto* Button = RPGOnlineLobbyLayout::Action(*WidgetTree, *Rows, Name, Label);
		Buttons.Add(Name, Button);
		ActionUnavailableTexts.Add(Name, RPGOnlineLobbyLayout::UnavailableReason(*WidgetTree, *Rows, Name));
		return Button;
	};
	CharacterSelectionSection = Rows->GetParent();
	CharacterListText = RPGOnlineLobbyLayout::Text(*WidgetTree, *Rows, TEXT("OnlineLobbyCharacterListStatus"), FText::GetEmpty());
	CharactersBox = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("OwnedCharacters"));
	Rows->AddChildToVerticalBox(CharactersBox);
	// Kept for existing native integration callers that dispatch selections by name.
	CharactersBox->SetVisibility(ESlateVisibility::Collapsed);
	SelectedCharacterText = RPGOnlineLobbyLayout::Text(*WidgetTree, *Rows, TEXT("OnlineLobbySelectedCharacter"), FText::GetEmpty(), 20);
	SelectedCharacterDetailsText = RPGOnlineLobbyLayout::Text(*WidgetTree, *Rows, TEXT("OnlineLobbyCharacterDetails"), FText::GetEmpty());
	auto AddActionColumn = [this](UHorizontalBox& Row)
	{
		auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
		auto* ColumnSlot = Row.AddChildToHorizontalBox(Column);
		ColumnSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ColumnSlot->SetPadding(FMargin(0, 0, 8, 0));
		return Column;
	};
	auto* PrimaryActions = WidgetTree->ConstructWidget<UHorizontalBox>();
	auto* PageActions = WidgetTree->ConstructWidget<UHorizontalBox>();
	Rows->AddChildToVerticalBox(PrimaryActions);
	Rows->AddChildToVerticalBox(PageActions);
	Rows = AddActionColumn(*PrimaryActions);
	AddAction(TEXT("ContinueWithCharacterButton"), LOCTEXT("EnterLobby", "로비 입장"))
		->SetBackgroundColor(FLinearColor(.08f, .38f, .43f));
	Rows = AddActionColumn(*PrimaryActions);
	AddAction(TEXT("OpenCharacterCreationButton"), LOCTEXT("NewCharacter", "새 캐릭터"));
	Buttons.Add(TEXT("PreviousCharacterPage"), RPGOnlineLobbyLayout::Action(*WidgetTree,
		*AddActionColumn(*PageActions), TEXT("PreviousCharacterPage"), LOCTEXT("PreviousPage", "← 이전")));
	Buttons.Add(TEXT("NextCharacterPage"), RPGOnlineLobbyLayout::Action(*WidgetTree,
		*AddActionColumn(*PageActions), TEXT("NextCharacterPage"), LOCTEXT("NextPage", "다음 →")));
	auto* CreationFrame = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("OnlineLobbyCharacterCreationSection"));
	CreationFrame->SetWidthOverride(744.f);
	CharacterContent->AddChildToVerticalBox(CreationFrame);
	CharacterCreationSection = CreationFrame;
	auto* Columns = WidgetTree->ConstructWidget<UWrapBox>();
	Columns->SetInnerSlotPadding(FVector2D(20.f, 12.f));
	Columns->SetHorizontalAlignment(HAlign_Center);
	CreationFrame->SetContent(Columns);

	CreationPreviewColumn = WidgetTree->ConstructWidget<UVerticalBox>();
	Columns->AddChildToWrapBox(CreationPreviewColumn);
	auto* PortraitFrame = WidgetTree->ConstructWidget<USizeBox>();
	PortraitFrame->SetWidthOverride(320.f);
	PortraitFrame->SetHeightOverride(400.f);
	CreationPreviewColumn->AddChildToVerticalBox(PortraitFrame);
	CreationPortrait = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("CreationPortrait"));
	CreationPortrait->SetVisibility(ESlateVisibility::HitTestInvisible);
	PortraitFrame->SetContent(CreationPortrait);
	auto* TurnActions = WidgetTree->ConstructWidget<UHorizontalBox>();
	CreationPreviewColumn->AddChildToVerticalBox(TurnActions)->SetPadding(FMargin(0, 6, 0, 0));
	auto StyleCreationAction = [](UButton* Button, const FLinearColor& Color)
	{
		FButtonStyle Style = Button->GetStyle();
		Style.Normal.TintColor = Color;
		Style.Hovered.TintColor = FLinearColor::LerpUsingHSV(Color, FLinearColor::White, .16f);
		Style.Pressed.TintColor = Color * .7f;
		Button->SetStyle(Style);
		return Button;
	};
	const FLinearColor Secondary(.09f, .15f, .21f);
	auto* Left = RPGOnlineLobbyLayout::Action(*WidgetTree, *AddActionColumn(*TurnActions),
		TEXT("TurnCreationPreviewLeft"), LOCTEXT("TurnLeft", "왼쪽 회전"));
	auto* Right = RPGOnlineLobbyLayout::Action(*WidgetTree, *AddActionColumn(*TurnActions),
		TEXT("TurnCreationPreviewRight"), LOCTEXT("TurnRight", "오른쪽 회전"));
	Buttons.Add(TEXT("TurnCreationPreviewLeft"), StyleCreationAction(Left, Secondary));
	Buttons.Add(TEXT("TurnCreationPreviewRight"), StyleCreationAction(Right, Secondary));

	auto* FormFrame = WidgetTree->ConstructWidget<USizeBox>();
	FormFrame->SetWidthOverride(400.f);
	Columns->AddChildToWrapBox(FormFrame)->SetVerticalAlignment(VAlign_Top);
	auto* Form = WidgetTree->ConstructWidget<UVerticalBox>();
	FormFrame->SetContent(Form);
	Rows = RPGOnlineLobbyLayout::Section(*WidgetTree, *Form, TEXT("CreationForm"), LOCTEXT("CreationClass", "직업"));
	CreationClassBox = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("CharacterCreationClass"));
	RPGOnlineLobbyLayout::Control(*WidgetTree, *Rows, *CreationClassBox);
	RPGOnlineLobbyLayout::Text(*WidgetTree, *Rows, TEXT("CharacterCreationClassName"), FText::GetEmpty(), 23);
	CreationClassText = RPGOnlineLobbyLayout::Text(*WidgetTree, *Rows, TEXT("CharacterCreationClassStatus"), FText::GetEmpty());
	StyleCreationAction(AddAction(TEXT("ReloadCreationOptionsButton"),
		NSLOCTEXT("RPGOnlineLobby", "ReloadCreationClasses", "생성 가능한 직업 새로고침")), Secondary);
	Rows = RPGOnlineLobbyLayout::Section(*WidgetTree, *Form, TEXT("CreationNameForm"), LOCTEXT("CreationNameLabel", "캐릭터 이름"));
	CharacterName = RPGOnlineLobbyLayout::Input(*WidgetTree, *Rows, TEXT("NewCharacterName"),
		NSLOCTEXT("RPGOnlineLobby", "CharacterName", "새 캐릭터 이름 (2~20자)"));
	FEditableTextBoxStyle NameStyle = CharacterName->GetWidgetStyle();
	NameStyle.SetBackgroundColor(FLinearColor(.045f, .063f, .085f));
	NameStyle.SetForegroundColor(FLinearColor::White).SetFocusedForegroundColor(FLinearColor::White);
	NameStyle.SetPadding(FMargin(10, 5));
	NameStyle.TextStyle.Font.Size = 17;
	CharacterName->SetWidgetStyle(NameStyle);
	StyleCreationAction(AddAction(TEXT("CreateCharacterButton"),
		NSLOCTEXT("RPGOnlineLobby", "CreateCharacter", "캐릭터 생성")), FLinearColor(.055f, .31f, .34f));
	StyleCreationAction(AddAction(TEXT("CloseCharacterCreationButton"), LOCTEXT("BackToLineup", "캐릭터 선택으로")), Secondary);
	Rows = CharacterContent;
	AddAction(TEXT("ReloadCharactersButton"), LOCTEXT("RetryRoster", "캐릭터 목록 다시 시도"));
}

void URPGOnlineLobbyWidget::RefreshCharacterPresentation()
{
	const int32 SlotCount = ARPGCharacterLineupStage::SlotCount;
	const int32 LastPage = FMath::Max(0, (OwnedCharacters.Num() - 1) / SlotCount);
	CharacterPage = FMath::Clamp(CharacterPage, 0, LastPage);
	const auto View = CurrentPresentation();
	const bool bCanSelect = View.bCanSelectCharacter && (!Steam || !Steam->IsInPartyLobby())
		&& (!ConfigurationPanel || !ConfigurationPanel->HasUnsavedChanges());
	const bool bCanOpenCreation = View.bCanCreateCharacter && bHasLoadedCharacterList && PendingCreatedCharacterId.IsEmpty();
	const FString SelectedId = Flow ? Flow->GetSelectedCharacterId() : FString();
	uint8 OccupiedSlots = 0;
	int32 SelectedSlot = INDEX_NONE;
	for (int32 SlotIndex = 0; SlotIndex < LineupSlotButtons.Num(); ++SlotIndex)
	{
		const int32 Index = CharacterPage * SlotCount + SlotIndex;
		auto* Label = LineupSlotLabels[SlotIndex].Get();
		auto* Button = LineupSlotButtons[SlotIndex].Get();
		const bool bOccupied = OwnedCharacters.IsValidIndex(Index);
		const bool bNewSlot = !bOccupied && Index == OwnedCharacters.Num() && bHasLoadedCharacterList;
		const bool bSelected = bOccupied && OwnedCharacters[Index].CharacterId == SelectedId;
		if (bOccupied)
		{
			OccupiedSlots |= 1 << SlotIndex;
			const auto& Character = OwnedCharacters[Index];
			const FText* ClassName = CharacterClassNames.Find(Character.ClassId);
			Label->SetText(Character.CharacterLevel > 0 && ClassName
				? FText::Format(LOCTEXT("RosterLabel", "{0}\n{1} · Lv. {2}"),
					FText::FromString(Character.Name), *ClassName, FText::AsNumber(Character.CharacterLevel))
				: FText::FromString(Character.Name));
		}
		else Label->SetText(bNewSlot ? LOCTEXT("EmptyNewSlot", "+ 새 캐릭터") : FText::GetEmpty());
		if (bSelected) SelectedSlot = SlotIndex;
		Button->SetVisibility(bOccupied || bNewSlot ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
		Button->SetIsEnabled(bOccupied ? bCanSelect : bNewSlot && bCanOpenCreation);
		Label->SetColorAndOpacity(FSlateColor(bSelected ? FLinearColor(1.f, .78f, .43f) : FLinearColor(.88f, .94f, 1.f)));
	}
	if (LineupSlots) LineupSlots->SetVisibility(bCharacterCreationOpen ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	if (LineupFrame) LineupFrame->SetVisibility(bCharacterCreationOpen ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	if (CharacterSelectionSection) CharacterSelectionSection->SetVisibility(bCharacterCreationOpen ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	if (CharacterCreationSection) CharacterCreationSection->SetVisibility(bCharacterCreationOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	auto Control = [this](const FName Name, bool bEnabled, bool bVisible)
	{
		if (auto* Button = Buttons.FindRef(Name).Get())
		{
			Button->SetIsEnabled(bEnabled);
			RPGOnlineLobbyLayout::SetControlVisibility(*Button, bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}
	};
	Control(TEXT("OpenCharacterCreationButton"), bCanOpenCreation, OwnedCharacters.Num() > 0 && bHasLoadedCharacterList);
	Control(TEXT("PreviousCharacterPage"), CharacterPage > 0, LastPage > 0);
	Control(TEXT("NextCharacterPage"), CharacterPage < LastPage, LastPage > 0);
	Control(TEXT("CloseCharacterCreationButton"), !bCreatingCharacter, true);
	const auto* CreationClass = GetSelectedCreationClass();
	const bool bHasPortrait = CreationClass && CreationClass->ClassId == TEXT("RPGPlayerClassDefinition:GreatSwordWarrior");
	if (CreationPreviewColumn) CreationPreviewColumn->SetVisibility(bHasPortrait ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	Control(TEXT("TurnCreationPreviewLeft"), bCharacterCreationOpen && bHasPortrait && !bCreatingCharacter, bHasPortrait);
	Control(TEXT("TurnCreationPreviewRight"), bCharacterCreationOpen && bHasPortrait && !bCreatingCharacter, bHasPortrait);
	if (auto* Button = Buttons.FindRef(TEXT("ReloadCharactersButton")).Get())
		RPGOnlineLobbyLayout::SetControlVisibility(*Button, UIError == TEXT("load_characters_failed")
			|| UIError == TEXT("create_character_failed") ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (auto* Button = Buttons.FindRef(TEXT("ReloadCreationOptionsButton")).Get())
		RPGOnlineLobbyLayout::SetControlVisibility(*Button, !bLoadingCreationOptions && CreationClasses.IsEmpty()
			? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (CreationClassBox) RPGOnlineLobbyLayout::SetControlVisibility(*CreationClassBox,
		CreationClasses.Num() > 1 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (auto* ClassName = Cast<UTextBlock>(GetWidgetFromName(TEXT("CharacterCreationClassName"))))
	{
		ClassName->SetText(CreationClass ? FText::FromString(CreationClass->DisplayName) : FText::GetEmpty());
		ClassName->SetVisibility(CreationClass && CreationClasses.Num() == 1 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	const bool bTitle = LoginContent && LoginContent->IsVisible();
	RefreshLineupStage(bTitle || (CharacterContent && CharacterContent->IsVisible() && (!bCharacterCreationOpen || bHasPortrait)));
	if (LineupStage && LineupStage->IsActorTickEnabled())
		LineupStage->Present(OccupiedSlots, SelectedSlot, bTitle ? ERPGLineupView::Title :
			bCharacterCreationOpen ? ERPGLineupView::Creation : ERPGLineupView::Roster, CreationPreviewYaw);

	FText ListStatus;
	if (bLoadingCharacters)
		ListStatus = NSLOCTEXT("RPGOnlineLobby", "LoadingCharacterList", "내 캐릭터를 불러오고 있습니다.");
	else if (UIError == TEXT("load_characters_failed"))
		ListStatus = bHasLoadedCharacterList
			? NSLOCTEXT("RPGOnlineLobby", "CachedCharacterList", "목록 갱신에 실패해 이전 조회 내용을 표시하고 있습니다. 다시 새로고침하세요.")
			: NSLOCTEXT("RPGOnlineLobby", "UnconfirmedCharacterList", "캐릭터 목록을 확인하지 못했습니다. 목록을 새로고침하세요.");
	else if (!bHasLoadedCharacterList)
		ListStatus = NSLOCTEXT("RPGOnlineLobby", "CharacterListNotLoaded", "접속 후 내 캐릭터 목록을 불러옵니다.");
	else if (OwnedCharacters.IsEmpty())
		ListStatus = LOCTEXT("EmptyRoster", "빈 자리에서 첫 캐릭터를 만들어 보세요.");
	else
		ListStatus = LastPage > 0 ? FText::Format(LOCTEXT("RosterPage", "{0} / {1}"),
			FText::AsNumber(CharacterPage + 1), FText::AsNumber(LastPage + 1)) : FText::GetEmpty();
	if (CharacterListText) CharacterListText->SetText(ListStatus);
	if (CreationClassText)
	{
		const auto* SelectedClass = GetSelectedCreationClass();
		CreationClassText->SetText(bLoadingCreationOptions
			? NSLOCTEXT("RPGOnlineLobby", "LoadingCreationClasses", "생성 가능한 직업을 확인하고 있습니다.")
			: !bCreationOptionsReady ? NSLOCTEXT("RPGOnlineLobby", "UnavailableCreationClasses", "직업 목록을 확인해야 캐릭터를 생성할 수 있습니다. 새로고침을 눌러 다시 확인하세요.")
			: CreationClasses.IsEmpty() ? NSLOCTEXT("RPGOnlineLobby", "EmptyCreationClasses", "현재 생성할 수 있는 직업이 없습니다.")
			: SelectedClass ? FText::FromString(SelectedClass->Description)
			: NSLOCTEXT("RPGOnlineLobby", "ChooseCreationClass", "새 캐릭터의 직업을 선택하세요."));
	}

	const auto* Selected = OwnedCharacters.FindByPredicate([&SelectedId](const FRPGBackendCharacter& Character)
		{ return !SelectedId.IsEmpty() && Character.CharacterId == SelectedId; });
	const FString SelectedName = Selected ? Selected->Name : FString();
	if (SelectedCharacterText)
	{
		SelectedCharacterText->SetText(FText::FromString(SelectedName));
		SelectedCharacterText->SetVisibility(Selected ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	const FText Progression = Selected ? GetCharacterProgressionText(*Selected) : FText::GetEmpty();
	if (SelectedCharacterDetailsText)
	{
		SelectedCharacterDetailsText->SetText(Progression);
		SelectedCharacterDetailsText->SetVisibility(Selected ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (PreparationCharacterText) PreparationCharacterText->SetText(SelectedName.IsEmpty()
		? NSLOCTEXT("RPGOnlineLobby", "CheckingSelectedCharacter", "선택한 캐릭터를 확인하고 있습니다.")
		: FText::Format(NSLOCTEXT("RPGOnlineLobby", "PreparationCharacterDetails", "{0}\n{1}"), FText::FromString(SelectedName), Progression));
	// Restore the authoritative selection after rejected/forced combo dispatches.
	// Names are display labels only; Flow continues to own the actual CharacterId.
	const FString LegacySelection = GetCharacterOption(SelectedId);
	if (CharactersBox && !bUpdatingOptions && CharactersBox->GetSelectedOption() != LegacySelection)
	{
		TGuardValue<bool> Updating(bUpdatingOptions, true);
		if (LegacySelection.IsEmpty()) CharactersBox->ClearSelection();
		else CharactersBox->SetSelectedOption(LegacySelection);
	}
}

void URPGOnlineLobbyWidget::RefreshLineupStage(bool bShowStage)
{
	UWorld* World = GetWorld();
	if (bShowStage && !LineupStage && FApp::CanEverRender() && IsInViewport() && World && World->HasBegunPlay())
	{
		FActorSpawnParameters Spawn;
		Spawn.ObjectFlags = RF_Transient;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		LineupStage = World->SpawnActor<ARPGCharacterLineupStage>(FVector(0, 0, 24000), FRotator::ZeroRotator, Spawn);
		if (LineupStage)
		{
			for (UImage* Image : {LineupImage.Get(), TitleImage.Get(), CreationPortrait.Get()})
			{
				if (!Image) continue;
				FSlateBrush Brush;
				Brush.SetResourceObject(LineupStage->GetImage());
				Brush.ImageSize = Image == CreationPortrait
					? FVector2D(ARPGCharacterLineupStage::PortraitWidth, ARPGCharacterLineupStage::PortraitHeight)
					: FVector2D(ARPGCharacterLineupStage::ImageWidth, ARPGCharacterLineupStage::ImageHeight);
				Image->SetBrush(Brush);
			}
		}
	}
	if (!LineupStage) return;
	if (LineupStage->IsActorTickEnabled() != bShowStage)
	{
		LineupStage->SetActorTickEnabled(bShowStage);
		LineupStage->SetActorHiddenInGame(!bShowStage);
		if (!bShowStage) LineupStage->Present(0, INDEX_NONE, ERPGLineupView::Roster);
	}
}

void URPGOnlineLobbyWidget::ActivateCharacterSlot(int32 SlotIndex)
{
	if (!CharacterContent || !CharacterContent->IsVisible() || bCharacterCreationOpen
		|| !LineupSlotButtons.IsValidIndex(SlotIndex) || !LineupSlotButtons[SlotIndex]->GetIsEnabled()) return;
	const int32 Index = CharacterPage * ARPGCharacterLineupStage::SlotCount + SlotIndex;
	if (OwnedCharacters.IsValidIndex(Index)) SelectOwnedCharacter(OwnedCharacters[Index].CharacterId);
	else if (Index == OwnedCharacters.Num()) OpenCharacterCreation();
}

void URPGOnlineLobbyWidget::ActivateCharacterSlot0() { ActivateCharacterSlot(0); }
void URPGOnlineLobbyWidget::ActivateCharacterSlot1() { ActivateCharacterSlot(1); }
void URPGOnlineLobbyWidget::ActivateCharacterSlot2() { ActivateCharacterSlot(2); }
void URPGOnlineLobbyWidget::ActivateCharacterSlot3() { ActivateCharacterSlot(3); }
void URPGOnlineLobbyWidget::ActivateCharacterSlot4() { ActivateCharacterSlot(4); }
void URPGOnlineLobbyWidget::ActivateCharacterSlot5() { ActivateCharacterSlot(5); }

void URPGOnlineLobbyWidget::PreviousCharacterPage() { MoveCharacterPage(-1); }
void URPGOnlineLobbyWidget::NextCharacterPage() { MoveCharacterPage(1); }

void URPGOnlineLobbyWidget::MoveCharacterPage(int32 Direction)
{
	if (!CharacterContent || !CharacterContent->IsVisible() || bCharacterCreationOpen) return;
	CharacterPage += Direction;
	RefreshPresentation();
}

void URPGOnlineLobbyWidget::OpenCharacterCreation()
{
	if (!CharacterContent || !CharacterContent->IsVisible() || bCharacterCreationOpen
		|| !bHasLoadedCharacterList || !PendingCreatedCharacterId.IsEmpty() || !CurrentPresentation().bCanCreateCharacter) return;
	bCharacterCreationOpen = true;
	CreationPreviewYaw = 0.f;
	UIError.Reset();
	RefreshPresentation();
}

void URPGOnlineLobbyWidget::CloseCharacterCreation()
{
	if (!bCharacterCreationOpen || bCreatingCharacter) return;
	bCharacterCreationOpen = false;
	RefreshPresentation();
}

void URPGOnlineLobbyWidget::TurnCreationPreviewLeft() { TurnCreationPreview(-45.f); }
void URPGOnlineLobbyWidget::TurnCreationPreviewRight() { TurnCreationPreview(45.f); }

void URPGOnlineLobbyWidget::TurnCreationPreview(float Degrees)
{
	if (!bCharacterCreationOpen || bCreatingCharacter || !CharacterContent || !CharacterContent->IsVisible()
		|| !CreationPreviewColumn || !CreationPreviewColumn->IsVisible()) return;
	CreationPreviewYaw = FRotator::NormalizeAxis(CreationPreviewYaw + Degrees);
	if (LineupStage && LineupStage->IsActorTickEnabled())
		LineupStage->Present(0, INDEX_NONE, ERPGLineupView::Creation, CreationPreviewYaw);
}

FText URPGOnlineLobbyWidget::GetCharacterProgressionText(const FRPGBackendCharacter& Character) const
{
	if (Character.ProgressionState == TEXT("Missing"))
		return NSLOCTEXT("RPGOnlineLobby", "MissingCharacterProgression", "저장된 성장 기록이 없습니다. 직업과 레벨을 확인할 수 없습니다.");
	if (Character.ProgressionState != TEXT("Available") || Character.CharacterLevel < 1 || Character.ClassId.IsEmpty())
		return NSLOCTEXT("RPGOnlineLobby", "UnavailableCharacterProgression", "성장 정보를 확인할 수 없습니다. 캐릭터 목록을 새로고침하세요.");
	const FText* ClassName = CharacterClassNames.Find(Character.ClassId);
	return FText::Format(NSLOCTEXT("RPGOnlineLobby", "CharacterProgressionDetails", "{0} · 레벨 {1}"),
		ClassName && !ClassName->IsEmpty() ? *ClassName : NSLOCTEXT("RPGOnlineLobby", "UnknownCharacterClassName", "직업 이름 확인 불가"),
		FText::AsNumber(Character.CharacterLevel));
}

const FRPGCharacterCreationOption* URPGOnlineLobbyWidget::GetSelectedCreationClass() const
{
	const int32 Index = CreationClassBox ? CreationClassBox->GetSelectedIndex() : INDEX_NONE;
	return bCreationOptionsReady && !bLoadingCreationOptions && CreationClasses.IsValidIndex(Index)
		&& CreationClassBox->GetSelectedOption() == CreationClasses[Index].DisplayName ? &CreationClasses[Index] : nullptr;
}

void URPGOnlineLobbyWidget::BeginCreationOptionsLoad()
{
	const auto* Lobby = GetGameInstance() ? GetGameInstance()->GetSubsystem<URPGOnlineLobbySubsystem>() : nullptr;
	if (!Web || !Auth || !Lobby || !Lobby->IsLobbyController(GetOwningPlayer()) || !Web->HasAccessToken()
		|| bLoadingCreationOptions || bLoggingIn || bLoggingOut || Auth->IsLoginRequestInFlight() || Web->IsPlayerLogoutInFlight()) return;
	bLoadingCreationOptions = true;
	bCreationOptionsReady = false;
	RefreshPresentation();
	Web->LoadCharacterCreationOptions();
}

void URPGOnlineLobbyWidget::ReloadCreationOptions()
{
	if (CanUse(TEXT("ReloadCreationOptionsButton"))) BeginCreationOptionsLoad();
}

void URPGOnlineLobbyWidget::HandleCreationClassSelected(FString, ESelectInfo::Type)
{
	if (!bUpdatingCreationOptions) RefreshPresentation();
}

void URPGOnlineLobbyWidget::HandleCreationOptions(const TArray<FRPGCharacterCreationOption>& Options, const bool bSuccess)
{
	if (!bLoadingCreationOptions) return;
	const int32 OldIndex = CreationClassBox ? CreationClassBox->GetSelectedIndex() : INDEX_NONE;
	const FString OldId = CreationClasses.IsValidIndex(OldIndex) ? CreationClasses[OldIndex].ClassId : FString();
	bLoadingCreationOptions = false;
	bCreationOptionsReady = bSuccess;
	CreationClasses = bSuccess ? Options : TArray<FRPGCharacterCreationOption>();
	if (CreationClassBox)
	{
		TGuardValue<bool> Updating(bUpdatingCreationOptions, true);
		CreationClassBox->ClearOptions();
		int32 SelectedIndex = INDEX_NONE;
		for (int32 Index = 0; Index < CreationClasses.Num(); ++Index)
		{
			CreationClassBox->AddOption(CreationClasses[Index].DisplayName);
			if (CreationClasses[Index].ClassId == OldId) SelectedIndex = Index;
		}
		// One authored recipe is unambiguous. Multiple classes require a choice.
		if (SelectedIndex == INDEX_NONE && CreationClasses.Num() == 1) SelectedIndex = 0;
		if (SelectedIndex != INDEX_NONE) CreationClassBox->SetSelectedIndex(SelectedIndex);
		else CreationClassBox->ClearSelection();
	}
	RefreshPresentation();
}

void URPGOnlineLobbyWidget::ContinueWithCharacter()
{
	if (!CanUse(TEXT("ContinueWithCharacterButton"))) return;
	bPreparationOpen = true;
	UIError.Reset();
	RefreshPresentation();
}

void URPGOnlineLobbyWidget::ChangeCharacter()
{
	if (!CanUse(TEXT("ChangeCharacterButton"))) return;
	bPreparationOpen = false;
	UIError.Reset();
	RefreshPresentation();
}

#undef LOCTEXT_NAMESPACE
