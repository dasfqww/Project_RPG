#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Project_RPGEditorUtilities.generated.h"

UCLASS()
class UProject_RPGEditorUtilities : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 특정 경로의 모든 블루프린트를 강제로 다시 컴파일합니다. */
	UFUNCTION(BlueprintCallable, Category = "ProjectRPG|Editor")
	static void CompileAllBlueprintsInPath(FString Path);

	/** 프로젝트 내의 모든 데이터 검증(Validation)을 실행합니다. */
	UFUNCTION(BlueprintCallable, Category = "ProjectRPG|Editor")
	static void RunAllValidations();
};
