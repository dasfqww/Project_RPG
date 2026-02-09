#pragma once

#include "Editor/UnrealEdEngine.h"
#include "Project_RPGEditorEngine.generated.h"

/**
 * 프로젝트 전용 에디터 엔진 클래스 (Lyra의 ULyraEditorEngine 참고)
 */
UCLASS()
class PROJECT_RPGEDITOR_API UProject_RPGEditorEngine : public UUnrealEdEngine
{
	GENERATED_BODY()

public:
	virtual void Init(IEngineLoop* InEngineLoop) override;

protected:
	/** 특정 맵에서 네트워크 설정을 강제하는 로직 등을 추가할 수 있습니다. */
	virtual void Tick(float DeltaSeconds, bool bIdleMode) override;
};
