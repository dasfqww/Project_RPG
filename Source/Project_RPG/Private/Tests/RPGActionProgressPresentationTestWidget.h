#pragma once
#include "UI/Progress/RPGActionProgressWidget.h"
#include "RPGActionProgressPresentationTestWidget.generated.h"

/** Native stand-in for an authored widget with a Construct callback. Never saved as content. */
UCLASS(Transient, NotBlueprintable)
class URPGActionProgressPresentationTestWidget : public URPGActionProgressWidget
{
	GENERATED_BODY()
public:
	static bool bDestroyHUDOnConstruct;
protected:
	virtual void NativeConstruct() override;
};
