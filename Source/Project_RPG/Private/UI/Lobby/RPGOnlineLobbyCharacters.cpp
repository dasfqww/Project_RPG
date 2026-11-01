#include "UI/Lobby/RPGOnlineLobbyWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/GameInstance.h"
#include "Online/RPGOnlineLobbySubsystem.h"
#include "Online/RPGSteamAuthSubsystem.h"
#include "UI/Lobby/RPGOnlineLobbyLayout.h"

void URPGOnlineLobbyWidget::BuildCharacterContent(UVerticalBox& Parent)
{
	CharacterContent = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("OnlineLobbyCharacterContent"));
	CharacterContent->SetVisibility(ESlateVisibility::Collapsed);
	Parent.AddChildToVerticalBox(CharacterContent);
	auto* Rows = RPGOnlineLobbyLayout::Section(*WidgetTree, *CharacterContent, TEXT("OnlineLobbyCharacterSection"),
		NSLOCTEXT("RPGOnlineLobby", "MyCharacters", "내 캐릭터"));
	auto AddAction = [this, &Rows](FName Name, const FText& Label)
	{
		auto* Button = RPGOnlineLobbyLayout::Action(*WidgetTree, *Rows, Name, Label);
		Buttons.Add(Name, Button);
		ActionUnavailableTexts.Add(Name, RPGOnlineLobbyLayout::UnavailableReason(*WidgetTree, *Rows, Name));
		return Button;
	};
	CharacterListText = RPGOnlineLobbyLayout::Text(*WidgetTree, *Rows, TEXT("OnlineLobbyCharacterListStatus"), FText::GetEmpty());
	CharactersBox = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("OwnedCharacters"));
	RPGOnlineLobbyLayout::Control(*WidgetTree, *Rows, *CharactersBox);
	SelectedCharacterText = RPGOnlineLobbyLayout::Text(*WidgetTree, *Rows, TEXT("OnlineLobbySelectedCharacter"), FText::GetEmpty(), 20);
	SelectedCharacterDetailsText = RPGOnlineLobbyLayout::Text(*WidgetTree, *Rows, TEXT("OnlineLobbyCharacterDetails"), FText::GetEmpty());
	AddAction(TEXT("ContinueWithCharacterButton"), NSLOCTEXT("RPGOnlineLobby", "ContinueWithCharacter", "접속"));
	AddAction(TEXT("BackToServersButton"), NSLOCTEXT("RPGFrontend", "BackServers", "서버 선택으로"));
	AddAction(TEXT("ReloadCharactersButton"), NSLOCTEXT("RPGOnlineLobby", "ReloadCharacters", "캐릭터 목록 새로고침"));
	Rows = RPGOnlineLobbyLayout::Section(*WidgetTree, *CharacterContent, TEXT("OnlineLobbyCharacterCreationSection"),
		NSLOCTEXT("RPGOnlineLobby", "CharacterCreationSection", "새 캐릭터 만들기"));
	RPGOnlineLobbyLayout::Text(*WidgetTree, *Rows, TEXT("OnlineLobbyCharacterCreationHint"),
		NSLOCTEXT("RPGOnlineLobby", "CharacterCreationHint", "같은 Steam 계정에 새로운 캐릭터를 만듭니다. 캐릭터의 레벨과 스킬 구성은 각각 저장됩니다."));
	RPGOnlineLobbyLayout::Text(*WidgetTree, *Rows, TEXT("OnlineLobbyCreationClassLabel"),
		NSLOCTEXT("RPGOnlineLobby", "CreationClassLabel", "직업 선택"));
	CreationClassBox = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("CharacterCreationClass"));
	RPGOnlineLobbyLayout::Control(*WidgetTree, *Rows, *CreationClassBox);
	CreationClassText = RPGOnlineLobbyLayout::Text(*WidgetTree, *Rows, TEXT("CharacterCreationClassStatus"), FText::GetEmpty());
	AddAction(TEXT("ReloadCreationOptionsButton"), NSLOCTEXT("RPGOnlineLobby", "ReloadCreationClasses", "생성 가능한 직업 새로고침"));
	CharacterName = RPGOnlineLobbyLayout::Input(*WidgetTree, *Rows, TEXT("NewCharacterName"),
		NSLOCTEXT("RPGOnlineLobby", "CharacterName", "새 캐릭터 이름 (2~20자)"));
	AddAction(TEXT("CreateCharacterButton"), NSLOCTEXT("RPGOnlineLobby", "CreateCharacter", "캐릭터 생성"));
}

void URPGOnlineLobbyWidget::RefreshCharacterPresentation()
{
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
		ListStatus = NSLOCTEXT("RPGOnlineLobby", "EmptyCharacterList", "아직 캐릭터가 없습니다. 아래에서 첫 캐릭터를 만들어 보세요.");
	else
		ListStatus = FText::Format(NSLOCTEXT("RPGOnlineLobby", "CharacterCount", "내 캐릭터 {0}명 · 플레이할 캐릭터를 선택하세요."), FText::AsNumber(OwnedCharacters.Num()));
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

	const FString SelectedName = Flow ? GetCharacterOption(Flow->GetSelectedCharacterId()) : FString();
	if (SelectedCharacterText) SelectedCharacterText->SetText(SelectedName.IsEmpty()
		? NSLOCTEXT("RPGOnlineLobby", "NoSelectedCharacter", "선택한 캐릭터가 없습니다.")
		: FText::Format(NSLOCTEXT("RPGOnlineLobby", "SelectedCharacter", "선택한 캐릭터: {0}"), FText::FromString(SelectedName)));
	const auto* Selected = Flow ? OwnedCharacters.FindByPredicate([this](const FRPGBackendCharacter& Character)
		{ return Character.CharacterId == Flow->GetSelectedCharacterId(); }) : nullptr;
	const FText Progression = Selected ? GetCharacterProgressionText(*Selected) : FText::GetEmpty();
	if (SelectedCharacterDetailsText) SelectedCharacterDetailsText->SetText(Progression);
	if (PreparationCharacterText) PreparationCharacterText->SetText(SelectedName.IsEmpty()
		? NSLOCTEXT("RPGOnlineLobby", "CheckingSelectedCharacter", "선택한 캐릭터를 확인하고 있습니다.")
		: FText::Format(NSLOCTEXT("RPGOnlineLobby", "PreparationCharacterDetails", "{0}\n{1}"), FText::FromString(SelectedName), Progression));
	// Restore the authoritative selection after rejected/forced combo dispatches.
	// Names are display labels only; Flow continues to own the actual CharacterId.
	if (CharactersBox && !bUpdatingOptions && CharactersBox->GetSelectedOption() != SelectedName)
	{
		TGuardValue<bool> Updating(bUpdatingOptions, true);
		if (SelectedName.IsEmpty()) CharactersBox->ClearSelection();
		else CharactersBox->SetSelectedOption(SelectedName);
	}
}

FText URPGOnlineLobbyWidget::GetCharacterProgressionText(const FRPGBackendCharacter& Character) const
{
	if (Character.ProgressionState == TEXT("Missing"))
		return NSLOCTEXT("RPGOnlineLobby", "MissingCharacterProgression", "저장된 성장 기록이 없습니다. 직업과 레벨을 확인할 수 없습니다.");
	if (Character.ProgressionState != TEXT("Available") || Character.CharacterLevel < 1 || Character.ClassId.IsEmpty())
		return NSLOCTEXT("RPGOnlineLobby", "UnavailableCharacterProgression", "성장 정보를 확인할 수 없습니다. 캐릭터 목록을 새로고침하세요.");
	const FText* ClassName = CharacterClassNames.Find(Character.ClassId);
	return FText::Format(NSLOCTEXT("RPGOnlineLobby", "CharacterProgressionDetails", "{0} · 레벨 {1}\n캐릭터 목록 조회 시점의 저장 정보"),
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
