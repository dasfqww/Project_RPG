#include "UI/Skill/RPGSkillWindow.h"

#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Skill/RPGSkillCatalogSubsystem.h"
#include "Skill/RPGSkillDefinition.h"
#include "UI/MVVM/RPGSkillViewModel.h"
#include "UI/Skill/RPGSkillDetailModule.h"
#include "UI/Skill/RPGSkillListModule.h"

void URPGSkillWindow::NativeConstruct()
{
	Super::NativeConstruct();

	if (!SkillViewModel)
	{
		SkillViewModel = NewObject<URPGSkillViewModel>(this);
	}

	if (SkillListModule)
	{
		SkillListModule->OnSkillSelected.AddUniqueDynamic(
			this,
			&ThisClass::HandleSkillSelection);
	}
}

void URPGSkillWindow::InitializeSkillWindow()
{
	if (!SkillViewModel)
	{
		SkillViewModel = NewObject<URPGSkillViewModel>(this);
	}

	APlayerController* PlayerController = GetOwningPlayer();
	APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	URPGPlayerSkillComponent* SkillComponent = Pawn
		? Pawn->FindComponentByClass<URPGPlayerSkillComponent>()
		: nullptr;
	if (!SkillComponent)
	{
		return;
	}

	// Clear stale entries immediately. The shared catalog request can complete
	// asynchronously and is coalesced with requests from other consumers.
	PendingSkillComponent = SkillComponent;
	SkillViewModel->InitializeSkillData(
		SkillComponent,
		TArray<URPGSkillDefinition*>());
	if (SkillListModule)
	{
		SkillListModule->InitSkillList(SkillViewModel->SkillSlots);
	}

	UGameInstance* GameInstance = GetGameInstance();
	URPGSkillCatalogSubsystem* Catalog = GameInstance
		? GameInstance->GetSubsystem<URPGSkillCatalogSubsystem>()
		: nullptr;
	if (!Catalog)
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("Skill window could not access the skill catalog."));
		return;
	}

	Catalog->RequestSkillDefinitions(
		FSimpleDelegate::CreateUObject(
			this,
			&ThisClass::HandleSkillDefinitionsReady));
}

void URPGSkillWindow::HandleSkillDefinitionsReady()
{
	URPGPlayerSkillComponent* SkillComponent = PendingSkillComponent.Get();
	UGameInstance* GameInstance = GetGameInstance();
	URPGSkillCatalogSubsystem* Catalog = GameInstance
		? GameInstance->GetSubsystem<URPGSkillCatalogSubsystem>()
		: nullptr;
	if (!SkillComponent || !SkillViewModel || !Catalog)
	{
		return;
	}

	SkillViewModel->InitializeSkillData(
		SkillComponent,
		Catalog->GetSkillDefinitions());
	if (SkillListModule)
	{
		SkillListModule->InitSkillList(SkillViewModel->SkillSlots);
	}
}

void URPGSkillWindow::HandleSkillSelection(
	URPGSkillSlotViewModel* SelectedSlotViewModel)
{
	if (SkillDetailModule)
	{
		SkillDetailModule->SetSelectedSkill(SelectedSlotViewModel);
	}
}
