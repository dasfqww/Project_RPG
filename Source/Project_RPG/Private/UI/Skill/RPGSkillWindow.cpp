// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Skill/RPGSkillWindow.h"
#include "UI/MVVM/RPGSkillViewModel.h"
#include "UI/Skill/RPGSkillListModule.h"
#include "UI/Skill/RPGSkillDetailModule.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Component/UI/PlayerUIComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Skill/RPGSkillDefinition.h"

// TODO: SkillManager나 GameInstance 등에서 전체 스킬 리스트를 가져오는 방법이 필요함.
// 현재는 임시로 에셋 레지스트리나 하드코딩된 리스트를 가정하거나, 
// PlayerUIComponent를 통해 가져와야 할 수도 있음.
// 여기서는 PlayerSkillComponent가 이미 알고 있는 스킬들이나, 
// 혹은 데이터 테이블에서 로드하는 방식을 사용할 수 있음.

void URPGSkillWindow::NativeConstruct()

{

	Super::NativeConstruct();



	// 뷰모델 생성 (아직 없다면)

	if (!SkillViewModel)

	{

		SkillViewModel = NewObject<URPGSkillViewModel>(this);

	}

	

	// 리스트 모듈 이벤트 연결

	if (SkillListModule)

	{

		SkillListModule->OnSkillSelected.AddDynamic(this, &URPGSkillWindow::HandleSkillSelection);

	}

}



void URPGSkillWindow::InitializeSkillWindow()

{

	if (!SkillViewModel)

	{

		SkillViewModel = NewObject<URPGSkillViewModel>(this);

	}



	// 1. 플레이어의 스킬 컴포넌트 찾기

	APlayerController* PC = GetOwningPlayer();

	if (!PC) return;



	APawn* Pawn = PC->GetPawn();

	if (!Pawn) return;



	URPGPlayerSkillComponent* SkillComp = Pawn->FindComponentByClass<URPGPlayerSkillComponent>();

	if (!SkillComp) return;



	// 2. 전체 스킬 정의 목록 가져오기

	// TODO: 실제 데이터 로드 로직 필요 (현재는 빈 배열)

	TArray<URPGSkillDefinition*> AllSkills; 

	// AllSkills = ...; 



	// 3. ViewModel 초기화

	SkillViewModel->InitializeSkillData(SkillComp, AllSkills);



	// 4. 리스트 모듈에 데이터 공급

	if (SkillListModule)
	{
		SkillListModule->InitSkillList(SkillViewModel->SkillSlots);
	}
}



	

void URPGSkillWindow::HandleSkillSelection(URPGSkillSlotViewModel* SelectedSlotVM)
{
	if (SkillDetailModule)
	{
		SkillDetailModule->SetSelectedSkill(SelectedSlotVM);
	}
}

	