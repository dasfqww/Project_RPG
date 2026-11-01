#include "UI/Skill/RPGSkillWindow.h"

#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Controller/RPGPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "CommonTextBlock.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Skill/RPGSkillCatalogSubsystem.h"
#include "Skill/RPGSkillDefinition.h"
#include "UI/MVVM/RPGSkillViewModel.h"
#include "UI/Skill/RPGSkillDetailModule.h"
#include "UI/Skill/RPGSkillListModule.h"

void URPGSkillWindow::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (UButton* Close = Cast<UButton>(GetWidgetFromName(TEXT("CloseButton"))))
	{
		Close->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleCloseButtonClicked);
	}
}

void URPGSkillWindow::HandleCloseButtonClicked()
{
	DeactivateWidget();
	RemoveFromParent();
}

void URPGSkillWindow::NativeDestruct()
{
	PendingSkillComponent.Reset();
	if (SkillListModule) SkillListModule->OnSkillSelected.RemoveAll(this);
	if (SkillDetailModule) SkillDetailModule->SetSelectedSkill(nullptr);
	if (SkillViewModel)
	{
		SkillViewModel->RemoveAllFieldValueChangedDelegates(this);
		SkillViewModel->InitializeSkillData(nullptr, {});
	}
	OnWindowClosed.Broadcast();
	Super::NativeDestruct();
}

void URPGSkillWindow::NativeOnDeactivated()
{
	Super::NativeOnDeactivated();
	OnWindowClosed.Broadcast();
}

FReply URPGSkillWindow::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	if (ARPGPlayerController* Controller = Cast<ARPGPlayerController>(GetOwningPlayer()))
	{
		if (Controller->HandlePlayerWindowKeyDown(this, KeyEvent)) return FReply::Handled();
	}
	if (KeyEvent.GetKey() == EKeys::Escape || CloseKeys.Contains(KeyEvent.GetKey()))
	{
		if (!KeyEvent.IsRepeat()) HandleCloseButtonClicked();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(Geometry, KeyEvent);
}

void URPGSkillWindow::NativeConstruct()
{
	Super::NativeConstruct();

	if (!SkillViewModel)
	{
		SkillViewModel = NewObject<URPGSkillViewModel>(this);
	}

	// Prefer the Designer-authored SP label. Keep a runtime fallback for widget
	// assets without one; both use the same view-model subscriptions below.
	UCommonTextBlock* PointsText = Cast<UCommonTextBlock>(GetWidgetFromName(TEXT("SkillPointsText")));
	if (!PointsText && WidgetTree && TitleBar && TitleBar->GetParent())
	{
		PointsText = WidgetTree->ConstructWidget<UCommonTextBlock>(
			UCommonTextBlock::StaticClass(), TEXT("SkillPointsText"));
		TitleBar->GetParent()->AddChild(PointsText);
	}
	SkillViewModel->RemoveAllFieldValueChangedDelegates(this);
	const TWeakObjectPtr<UCommonTextBlock> WeakPointsText = PointsText;
	const auto RefreshPoints = [this, WeakPointsText]()
	{
		if (WeakPointsText.IsValid() && SkillViewModel)
		{
			WeakPointsText->SetText(FText::Format(
				NSLOCTEXT("RPGSkillUI", "SkillPoints", "SP {0} / {1}"),
				FText::AsNumber(SkillViewModel->RemainingSP),
				FText::AsNumber(SkillViewModel->TotalSP)));
		}
	};
	const auto OnPointsChanged = INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateWeakLambda(
		this, [RefreshPoints](UObject*, UE::FieldNotification::FFieldId) { RefreshPoints(); });
	SkillViewModel->AddFieldValueChangedDelegate(
		URPGSkillViewModel::FFieldNotificationClassDescriptor::RemainingSP, OnPointsChanged);
	SkillViewModel->AddFieldValueChangedDelegate(
		URPGSkillViewModel::FFieldNotificationClassDescriptor::TotalSP, OnPointsChanged);
	SkillViewModel->AddFieldValueChangedDelegate(
		URPGSkillViewModel::FFieldNotificationClassDescriptor::SkillSlots,
		INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateWeakLambda(
			this, [this](UObject*, UE::FieldNotification::FFieldId)
			{
				if (SkillListModule && SkillViewModel) SkillListModule->InitSkillList(SkillViewModel->SkillSlots);
				else if (SkillDetailModule) SkillDetailModule->SetSelectedSkill(nullptr);
			}));
	RefreshPoints();

	if (SkillListModule)
	{
		SkillListModule->OnSkillSelected.AddUniqueDynamic(
			this,
			&ThisClass::HandleSkillSelection);
	}

	if (SkillDetailModule)
	{
		SkillDetailModule->SetSelectedSkill(nullptr);
	}
	InitializeSkillWindow();
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
	// Clear stale entries immediately. The shared catalog request can complete
	// asynchronously and is coalesced with requests from other consumers.
	PendingSkillComponent.Reset();
	SkillViewModel->InitializeSkillData(
		SkillComponent,
		TArray<URPGSkillDefinition*>());
	if (SkillListModule)
	{
		SkillListModule->InitSkillList(SkillViewModel->SkillSlots);
	}
	else if (SkillDetailModule)
	{
		SkillDetailModule->SetSelectedSkill(nullptr);
	}
	if (!SkillComponent)
	{
		return;
	}
	PendingSkillComponent = SkillComponent;

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
	if (GetOwningPlayerPawn() != SkillComponent->GetOwner())
	{
		// Possession may have changed while the shared catalog was loading.
		InitializeSkillWindow();
		return;
	}
	PendingSkillComponent.Reset();

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
		SkillDetailModule->SetSelectedSkill(SkillViewModel &&
			SkillViewModel->SkillSlots.Contains(SelectedSlotViewModel)
				? SelectedSlotViewModel : nullptr);
	}
}
