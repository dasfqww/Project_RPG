#include "UI/Content/RPGIntroApproachGuideWidget.h"

#include "Component/Progression/RPGIntroApproachObjectiveComponent.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Controller/RPGPlayerController.h"
#include "GameFramework/Pawn.h"
#include "RPGGameplayTags.h"
#include "Skill/QuickSlot/RPGQuickSlotInputBindings.h"

#define LOCTEXT_NAMESPACE "RPGIntroApproachGuideWidget"

const TCHAR* URPGIntroApproachGuideWidget::GetPracticeContentId() const { return TEXT("Progression.Intro1"); }

bool URPGIntroApproachGuideWidget::HasActiveProgress(const APawn* Pawn) const
{
	const auto* Objectives = IsValid(Pawn) ? Pawn->FindComponentByClass<URPGIntroApproachObjectiveComponent>() : nullptr;
	return IsValid(Objectives) && Objectives->IsRegistered() && !Objectives->IsBeingDestroyed()
		&& Objectives->GetApproachProgress().bActive;
}

FText URPGIntroApproachGuideWidget::ReadStepHint(int32 Step, const ARPGPlayerController& Controller) const
{
	if (Step == 0) return LOCTEXT("Start", "파란 01 출발 표시 위로 이동해 주세요.");
	const FGameplayTag Skill = Step == 1 ? RPGGameplayTags::Player_Ability_Skill_AssultBlade : RPGGameplayTags::Player_Ability_Skill_WhirlWind;
	const auto* Pawn = GetDisplayedPawn();
	const auto* Slots = Pawn ? Pawn->FindComponentByClass<UQuickSlotComponent>() : nullptr;
	FText Key;
	bool bSlotted = false;
	if (Slots)
		for (int32 Index = 0; Index < Slots->GetMaxSkillSlots(); ++Index)
		{
			const auto* Content = Slots->GetSkillSlotContent(Index);
			if (Content && Content->AbilityTag == Skill)
			{
				bSlotted = true;
				Key = FRPGQuickSlotInputBindings::ReadKeyLabel(&Controller, true, Index);
				break;
			}
		}
	if (!bSlotted)
		return Step == 1
			? LOCTEXT("AssaultMissing", "로비의 스킬 구성에서 돌진 베기를 편성·저장한 뒤 다시 입장해 주세요.")
			: LOCTEXT("WhirlwindMissing", "로비의 스킬 구성에서 회전 베기를 편성·저장한 뒤 다시 입장해 주세요.");
	if (Key.IsEmpty()) return LOCTEXT("KeyMissing", "입력 설정에서 현재 스킬 슬롯의 키를 확인해 주세요.");
	return FText::Format(Step == 1
		? LOCTEXT("Approach", "가림막 옆으로 이동하세요. 02 접근 표시 밖에서 [{0}] 돌진 베기를 시작하고, 표시 안에서 마쳐 주세요.")
		: LOCTEXT("Attack", "03 공격 표시 안에서 [{0}] 회전 베기를 사용하세요. 시작과 정상 종료가 모두 표시 안이어야 합니다."), Key);
}

void URPGIntroApproachGuideWidget::ShowCurrentProgress(const ARPGPlayerController& Controller)
{
	const auto* Pawn = GetDisplayedPawn();
	const auto* Objectives = Pawn ? Pawn->FindComponentByClass<URPGIntroApproachObjectiveComponent>() : nullptr;
	if (!IsValid(Objectives) || !HasActiveProgress(Pawn)) { ClearPresentation(); return; }
	if (!Card || !Title || !Hint || Goals.Num() != 3) return;
	const auto Progress = Objectives->GetApproachProgress();
	const bool Completed[] = {Progress.bStartReached, Progress.bApproachCompleted, Progress.bAttackCompleted};
	const FText Names[] = {LOCTEXT("GoalStart", "01 출발 지점 확인"), LOCTEXT("GoalApproach", "02 돌진으로 접근"), LOCTEXT("GoalAttack", "03 회전 베기 실습")};
	int32 Count = 0, Next = INDEX_NONE;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (Completed[Index]) ++Count; else if (Next == INDEX_NONE) Next = Index;
		const FText Text = FText::Format(Completed[Index] ? LOCTEXT("Done", "[완료] {0}") : LOCTEXT("Pending", "[대기] {0}"), Names[Index]);
		if (!Goals[Index]->GetText().EqualTo(Text)) Goals[Index]->SetText(Text);
		Goals[Index]->SetColorAndOpacity(Completed[Index] ? FLinearColor(.35f, .95f, .6f)
			: Next == Index ? FLinearColor(1.f, .8f, .3f) : FLinearColor(.85f, .85f, .85f));
	}
	const FText NewTitle = FText::Format(LOCTEXT("Title", "입문 1 · 접근 실습 {0}/3"), FText::AsNumber(Count));
	if (!Title->GetText().EqualTo(NewTitle)) Title->SetText(NewTitle);
	const FText NewHint = Progress.IsComplete()
		? LOCTEXT("Complete", "개인 실습 완료! 서버의 파티 완료·정산 확인 뒤 결과 화면에서 로비로 돌아갈 수 있습니다.")
		: ReadStepHint(Next, Controller);
	if (!Hint->GetText().EqualTo(NewHint)) Hint->SetText(NewHint);
	Card->SetVisibility(ESlateVisibility::HitTestInvisible);
	RefreshCompletionActions(Progress.IsComplete());
}

#undef LOCTEXT_NAMESPACE
