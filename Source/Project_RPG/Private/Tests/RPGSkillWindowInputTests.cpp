#if WITH_DEV_AUTOMATION_TESTS

#include "Input/Events.h"
#include "Misc/AutomationTest.h"
#include "UI/Skill/RPGSkillWindow.h"
#include "Widgets/SWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSkillWindowCloseInputTest,
	"ProjectRPG.Skill.UI.CloseInput", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillWindowCloseInputTest::RunTest(const FString& Parameters)
{
	UClass* Class = LoadClass<URPGSkillWindow>(nullptr,
		TEXT("/Game/Blueprints/UI/Skill/WBP_SkillWindow.WBP_SkillWindow_C"));
	if (!TestNotNull(TEXT("Authored skill window"), Class)) return false;
	URPGSkillWindow* Window = NewObject<URPGSkillWindow>(GetTransientPackage(), Class);
	if (!TestTrue(TEXT("Authored widget initializes"), Window->Initialize())) return false;
	Window->SetCloseKeys({EKeys::K});
	const TSharedRef<SWidget> SlateWindow = Window->TakeWidget();
	int32 CloseCount = 0;
	const FDelegateHandle Closed = Window->OnWindowClosed.AddLambda([&CloseCount]() { ++CloseCount; });
	const auto Press = [&SlateWindow](const FKey Key, bool bRepeat = false)
	{
		return SlateWindow->OnPreviewKeyDown(FGeometry(),
			FKeyEvent(Key, FModifierKeysState(), 0, bRepeat, 0, 0));
	};

	Window->ActivateWidget();
	TestTrue(TEXT("Holding the opening key consumes repeats"), Press(EKeys::K, true).IsEventHandled());
	TestTrue(TEXT("Holding the opening key keeps the window open"), Window->IsActivated());
	TestEqual(TEXT("Repeated key does not close"), CloseCount, 0);
	TestTrue(TEXT("A fresh toggle press closes"), Press(EKeys::K).IsEventHandled());
	TestFalse(TEXT("Toggle deactivates the window"), Window->IsActivated());
	TestEqual(TEXT("Close notification is delivered once"), CloseCount, 1);

	Window->SetCloseKeys({EKeys::J});
	Window->ActivateWidget();
	Press(EKeys::K);
	TestTrue(TEXT("Previous key no longer closes after rebinding"), Window->IsActivated());
	TestTrue(TEXT("Remapped key closes"), Press(EKeys::J).IsEventHandled());
	TestFalse(TEXT("Remapped key deactivates"), Window->IsActivated());
	Window->ActivateWidget();
	TestTrue(TEXT("Escape closes independently of the binding"), Press(EKeys::Escape).IsEventHandled());
	TestFalse(TEXT("Escape deactivates"), Window->IsActivated());
	Window->OnWindowClosed.Remove(Closed);
	return true;
}

#endif
