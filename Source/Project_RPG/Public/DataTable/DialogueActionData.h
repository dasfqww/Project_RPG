#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "DialogueActionData.generated.h"

class USoundCue;

USTRUCT(BlueprintType) 
struct FDialogueActionRow : public FTableRowBase 
{
	GENERATED_BODY(); 
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName NPCName; //  NPC 이름
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite) 
	FString DialogueText; // 대사 텍스트 

	UPROPERTY(EditAnywhere, BlueprintReadWrite) 
	TObjectPtr<USoundCue> SoundCue; // 음성 파일 경로 (옵션) 
};