#include "Tests/RPGActionProgressPresentationTestWidget.h"
#include "UI/HUD/RPGHUD.h"
#include "EngineUtils.h"

bool URPGActionProgressPresentationTestWidget::bDestroyHUDOnConstruct = false;

void URPGActionProgressPresentationTestWidget::NativeConstruct()
{
	Super::NativeConstruct();
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	if (bDestroyHUDOnConstruct)
	{
		for (TActorIterator<ARPGHUD> It(GetWorld()); It; ++It)
			if (It->GetOwningPlayerController() == GetOwningPlayer()) It->Destroy();
	}
#endif
}
