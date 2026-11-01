#include "UI/Content/RPGTrainingGuideWidget.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Attribute/RPGAttributeSet.h"
#include "Blueprint/WidgetTree.h"
#include "Component/Progression/RPGTrainingObjectiveComponent.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Controller/RPGPlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Manager/HttpWebManager.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "Player/RPGPlayerState.h"
#include "UI/Content/RPGDungeonResultActionsWidget.h"
#include "UObject/StrongObjectPtr.h"
#include "RPGGameplayTags.h"
#include "Skill/QuickSlot/RPGQuickSlotInputBindings.h"

#define LOCTEXT_NAMESPACE "RPGTrainingGuideWidget"

APawn* URPGTrainingGuideWidget::GetDisplayedPawn() const { return DisplayedPawn.Get(); }

namespace
{
	void SetTextIfChanged(UTextBlock& Block, const FText& Text)
	{
		if (!Block.GetText().EqualTo(Text)) Block.SetText(Text);
	}
	bool IsLivingPawn(APawn* Pawn)
	{
		if (!IsValid(Pawn) || Pawn->IsActorBeingDestroyed()) return false;
		const auto* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn);
		if (!ASC || !ASC->HasAttributeSetForAttribute(URPGAttributeSet::GetCurrentHealthAttribute())) return true;
		const float Health = ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
		return FMath::IsFinite(Health) && Health > 0.f;
	}
	bool ReadTrainingResultScope(const URPGTrainingGuideWidget& Guide, FString& CharacterId, FString& SessionId)
	{
		const auto* Controller = Cast<ARPGPlayerController>(Guide.GetOwningPlayer());
		const UWorld* World = Guide.GetWorld();
		const auto* Instance = World ? World->GetGameInstance() : nullptr;
		const auto* Flow = Instance ? Instance->GetSubsystem<URPGDungeonSessionSubsystem>() : nullptr;
		const auto* Web = Instance ? Instance->GetSubsystem<UHttpWebManager>() : nullptr;
		const auto* State = Controller ? Controller->GetPlayerState<ARPGPlayerState>() : nullptr;
		if (!World || World->bIsTearingDown || World->GetNetMode() != NM_Client || !Guide.IsInViewport()
			|| !IsValid(Controller) || !Controller->IsLocalController() || Controller->IsActorBeingDestroyed()
			|| Controller->GetWorld() != World || Controller->GetPawn() != Guide.GetDisplayedPawn()
			|| !IsLivingPawn(Guide.GetDisplayedPawn()) || !Flow || !Web || !State || !State->HasAuthenticatedCharacter()
			|| Web->GetAuthenticatedSteamId().IsEmpty()
			|| !Web->GetAuthenticatedSteamId().Equals(State->GetAuthenticatedSteamId(), ESearchCase::CaseSensitive)) return false;
		const auto Session = Flow->GetCurrentDungeonSession();
		if (!Session.DungeonId.Equals(TEXT("Progression.Training"), ESearchCase::CaseSensitive)
			|| !Session.Difficulty.Equals(TEXT("Normal"), ESearchCase::CaseSensitive)
			|| (!Session.State.Equals(TEXT("InProgress"), ESearchCase::CaseSensitive)
				&& !Session.State.Equals(TEXT("SettlementPending"), ESearchCase::CaseSensitive)
				&& !Session.State.Equals(TEXT("Cleared"), ESearchCase::CaseSensitive)
				&& !Session.State.Equals(TEXT("Failed"), ESearchCase::CaseSensitive))
			|| Flow->GetSelectedCharacterId().IsEmpty()
			|| !Flow->GetSelectedCharacterId().Equals(State->GetBackendCharacterId(), ESearchCase::CaseSensitive)
			|| Session.DungeonSessionId.IsEmpty()
			|| !Session.DungeonSessionId.Equals(State->GetBackendDungeonSessionId(), ESearchCase::CaseSensitive)
			|| !Session.Members.ContainsByPredicate([&](const FRPGDungeonSessionMember& Member)
				{ return Member.CharacterId.Equals(State->GetBackendCharacterId(), ESearchCase::CaseSensitive); })) return false;
		CharacterId = State->GetBackendCharacterId(); SessionId = Session.DungeonSessionId;
		return true;
	}
}

TSharedRef<SWidget> URPGTrainingGuideWidget::RebuildWidget()
{
	SetIsFocusable(false);
	// Hide only the card: the root must keep detecting late replication and possession.
	SetVisibility(ESlateVisibility::HitTestInvisible);
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		auto* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Canvas;
		auto* Width = WidgetTree->ConstructWidget<USizeBox>();
		Width->SetWidthOverride(360.f);
		auto* Position = Canvas->AddChildToCanvas(Width);
		Position->SetPosition(FVector2D(32.f, 220.f));
		Position->SetAutoSize(true);
		Card = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TrainingGuideCard"));
		Card->SetBrushColor(FLinearColor(.025f, .04f, .06f, .9f));
		Card->SetPadding(FMargin(14.f));
		Width->SetContent(Card);
		auto* Content = WidgetTree->ConstructWidget<UVerticalBox>();
		Card->SetContent(Content);
		Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TrainingGuideTitle"));
		auto Font = Title->GetFont(); Font.Size = 17; Title->SetFont(Font);
		Title->SetAutoWrapText(true);
		Content->AddChildToVerticalBox(Title)->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
		for (int32 Index = 0; Index < 4; ++Index)
		{
			auto* Row = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
				FName(*FString::Printf(TEXT("TrainingGoal%d"), Index)));
			Font.Size = 14; Row->SetFont(Font); Row->SetAutoWrapText(true);
			Content->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 3.f));
			Goals.Add(Row);
		}
		Hint = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TrainingGuideHint"));
		Font.Size = 13; Hint->SetFont(Font); Hint->SetAutoWrapText(true);
		Content->AddChildToVerticalBox(Hint)->SetPadding(FMargin(0.f, 12.f, 0.f, 0.f));
		ClearPresentation();
	}
	return Super::RebuildWidget();
}

void URPGTrainingGuideWidget::NativeConstruct()
{
	Super::NativeConstruct();
	bAttached = true;
	RefreshCurrentPawn();
}

void URPGTrainingGuideWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	if (bAttached) RefreshCurrentPawn();
}

void URPGTrainingGuideWidget::RefreshCurrentPawn()
{
	const auto* Controller = Cast<ARPGPlayerController>(GetOwningPlayer());
	APawn* Pawn = IsValid(Controller) ? Controller->GetPawn() : nullptr;
	if (!bAttached || !GetWorld() || GetWorld()->bIsTearingDown || !IsValid(Controller)
		|| !Controller->IsLocalController() || Controller->IsActorBeingDestroyed()
		|| Controller->GetWorld() != GetWorld() || !IsLivingPawn(Pawn)
		|| Pawn->GetController() != Controller || Pawn->GetWorld() != GetWorld())
	{
		ClearPresentation(); return;
	}
	const auto* Objectives = Pawn->FindComponentByClass<URPGTrainingObjectiveComponent>();
	if (!IsValid(Objectives) || !Objectives->IsRegistered() || Objectives->IsBeingDestroyed())
	{
		ClearPresentation(); return;
	}
	const auto Progress = Objectives->GetTrainingProgress();
	if (!Progress.bActive) { ClearPresentation(); return; }
	DisplayedPawn = Pawn;
	ShowProgress(Progress, *Controller);
}

FText URPGTrainingGuideWidget::ReadControlHint(int32 Goal, const ARPGPlayerController& Controller) const
{
	FText KeyLabel;
	if (Goal < 2)
	{
		const FKey Key = Controller.GetCurrentKeyForTag(Goal == 0
			? RPGGameplayTags::InputTag_LightAttack_GreatSword : RPGGameplayTags::InputTag_Roll);
		if (Key.IsValid()) KeyLabel = Key.GetDisplayName(false);
	}
	else
	{
		const auto* Slots = DisplayedPawn.IsValid() ? DisplayedPawn->FindComponentByClass<UQuickSlotComponent>() : nullptr;
		if (Slots)
		{
			const FGameplayTag Skill = Goal == 2 ? RPGGameplayTags::Player_Ability_Skill_AssultBlade : RPGGameplayTags::Player_Ability_Skill_Charge;
			for (int32 Index = 0; Index < Slots->GetMaxSkillSlots(); ++Index)
			{
				const auto* SlotContent = Slots->GetSkillSlotContent(Index);
				if (SlotContent && SlotContent->AbilityTag == Skill)
				{
					KeyLabel = FRPGQuickSlotInputBindings::ReadKeyLabel(&Controller, true, Index);
					break;
				}
			}
		}
	}
	if (KeyLabel.IsEmpty())
	{
		return Goal < 2
			? LOCTEXT("MissingControl", "입력 설정에서 이 동작의 키를 확인해 주세요.")
			: LOCTEXT("MissingSkillControl", "스킬 슬롯과 입력 설정에서 이 스킬의 배치와 키를 확인해 주세요.");
	}
	return FText::Format(Goal == 3
		? LOCTEXT("ChargeControl", "[{0}] 키를 길게 누르고, 끝까지 모은 뒤 놓으세요.")
		: LOCTEXT("TapControl", "[{0}] 키로 동작을 사용하고 끝까지 마쳐 보세요."), KeyLabel);
}

void URPGTrainingGuideWidget::ShowProgress(const FRPGTrainingObjectiveProgress& Progress,
	const ARPGPlayerController& Controller)
{
	if (!Card || !Title || !Hint || Goals.Num() != 4) return;
	const bool Completed[] = {Progress.bBasicAttackCompleted, Progress.bEvadeCompleted,
		Progress.bAssaultCompleted, Progress.bChargeCompleted};
	const FText Names[] = {LOCTEXT("Attack", "기본 공격"), LOCTEXT("Evade", "회피"),
		LOCTEXT("Assault", "돌진 베기"), LOCTEXT("Charge", "차지 공격 · 끝까지 모아 사용")};
	int32 Count = 0, Next = INDEX_NONE;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (Completed[Index]) ++Count;
		else if (Next == INDEX_NONE) Next = Index;
		SetTextIfChanged(*Goals[Index], FText::Format(Completed[Index]
			? LOCTEXT("Done", "[완료] {0}") : LOCTEXT("Pending", "[대기] {0}"), Names[Index]));
		Goals[Index]->SetColorAndOpacity(Completed[Index] ? FLinearColor(.35f, .95f, .6f)
			: Next == Index ? FLinearColor(1.f, .8f, .3f) : FLinearColor(.85f, .85f, .85f));
	}
	SetTextIfChanged(*Title, FText::Format(LOCTEXT("Title", "기본 조작 훈련 {0}/4"), FText::AsNumber(Count)));
	SetTextIfChanged(*Hint, Progress.IsComplete()
		? LOCTEXT("PracticeComplete", "조작 실습 완료! 서버 확인 뒤 결과 화면에서 로비로 돌아갈 수 있습니다.")
		: FText::Format(LOCTEXT("Next", "순서는 자유입니다. 먼저 {0}을 연습해 보세요.\n{1}"), Names[Next], ReadControlHint(Next, Controller)));
	Card->SetVisibility(ESlateVisibility::HitTestInvisible);
	RefreshCompletionActions(Progress.IsComplete());
}

void URPGTrainingGuideWidget::RefreshCompletionActions(bool bPracticeComplete)
{
	FString CharacterId, SessionId;
	if (!bAttached || !bPracticeComplete || !ReadTrainingResultScope(*this, CharacterId, SessionId))
	{
		ClearCompletionActions(); return;
	}
	if (CompletionActions && (!CompletionCharacterId.Equals(CharacterId, ESearchCase::CaseSensitive)
		|| !CompletionSessionId.Equals(SessionId, ESearchCase::CaseSensitive)))
		ClearCompletionActions();
	if (CompletionActions) return;
	auto* Controller = Cast<ARPGPlayerController>(GetOwningPlayer());
	TStrongObjectPtr<URPGDungeonResultActionsWidget> Candidate(CreateWidget<URPGDungeonResultActionsWidget>(Controller));
	if (!Candidate.IsValid()) return;
	// This selects the non-defeat controls, not a backend clear or a reward grant.
	// The existing widget polls the session and rechecks a fresh terminal receipt
	// before ReturnToLobby. Personal practice completion cannot bypass settlement.
	Candidate->InitializeResult(true);
	Candidate->SetIsFocusable(true);
	CompletionActions = Candidate.Get();
	CompletionCharacterId = CharacterId; CompletionSessionId = SessionId;
	const bool bAdded = Candidate->AddToPlayerScreen(110);
	FString CurrentCharacter, CurrentSession;
	if (!bAdded || !bAttached || CompletionActions != Candidate.Get()
		|| !ReadTrainingResultScope(*this, CurrentCharacter, CurrentSession)
		|| !CurrentCharacter.Equals(CharacterId, ESearchCase::CaseSensitive)
		|| !CurrentSession.Equals(SessionId, ESearchCase::CaseSensitive))
	{
		Candidate->RemoveFromParent();
		if (CompletionActions == Candidate.Get()) ClearCompletionActions();
		return;
	}
	FInputModeGameAndUI InputMode;
	InputMode.SetWidgetToFocus(Candidate->TakeWidget());
	Controller->SetInputMode(InputMode);
	Controller->SetShowMouseCursor(true);
}

void URPGTrainingGuideWidget::ClearCompletionActions()
{
	TStrongObjectPtr<URPGDungeonResultActionsWidget> Previous(CompletionActions.Get());
	CompletionActions = nullptr;
	CompletionCharacterId.Reset(); CompletionSessionId.Reset();
	if (!Previous.IsValid()) return;
	auto* Controller = Cast<ARPGPlayerController>(GetOwningPlayer());
	const bool bOwnsFocus = Controller && (Previous->HasUserFocus(Controller) || Previous->HasUserFocusedDescendants(Controller));
	Previous->RemoveFromParent(); // Existing widget releases its polling/delegates.
	if (bOwnsFocus && IsValid(Controller) && Controller->IsLocalController() && !Controller->IsActorBeingDestroyed()
		&& GetWorld() && !GetWorld()->bIsTearingDown && Controller->GetWorld() == GetWorld())
	{
		Controller->SetInputMode(FInputModeGameOnly());
		Controller->SetShowMouseCursor(false);
	}
}

void URPGTrainingGuideWidget::ClearPresentation()
{
	ClearCompletionActions();
	DisplayedPawn.Reset();
	if (Card) Card->SetVisibility(ESlateVisibility::Collapsed);
	if (Title) Title->SetText(FText::GetEmpty());
	if (Hint) Hint->SetText(FText::GetEmpty());
	for (const auto& Goal : Goals) if (Goal) Goal->SetText(FText::GetEmpty());
}

void URPGTrainingGuideWidget::NativeDestruct()
{
	bAttached = false;
	ClearPresentation();
	Super::NativeDestruct();
}

#undef LOCTEXT_NAMESPACE
