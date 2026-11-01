#if WITH_DEV_AUTOMATION_TESTS
#include "UI/Lobby/RPGOnlineLobbyWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/VerticalBox.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Misc/AutomationTest.h"
#include "Online/RPGOnlineLobbySubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Widgets/SWidget.h"

namespace
{
	struct FBackInputWorld
	{
		TStrongObjectPtr<UGameInstance> Instance{NewObject<UGameInstance>(GEngine)};
		UWorld* World = nullptr;
		UGameViewportClient* Viewport = nullptr;
		APlayerController* Controller = nullptr;
		URPGOnlineLobbyWidget* Widget = nullptr;
		FBackInputWorld()
		{
			Instance->InitializeForMinimalNetRPC(FName(*FString::Printf(TEXT("/Temp/LobbyBackInput_%s"),
				*FGuid::NewGuid().ToString(EGuidFormats::Digits))));
			World = Instance->GetWorld();
			if (!World) return;
			Viewport = NewObject<UGameViewportClient>(GEngine);
			Instance->GetWorldContext()->GameViewport = Viewport;
			Viewport->Init(*Instance->GetWorldContext(), Instance.Get(), false);
			FString Error;
			auto* Player = Instance->CreateLocalPlayer(0, Error, false);
			if (!Player) return;
			Controller = World->SpawnActor<APlayerController>();
			if (!Controller) return;
			Controller->SetPlayer(Player);
			World->AddController(Controller);
			auto* Lobby = Instance->GetSubsystem<URPGOnlineLobbySubsystem>();
			auto* Map = FindFProperty<FStrProperty>(URPGOnlineLobbySubsystem::StaticClass(), TEXT("LobbyMapPackage"));
			if (!Lobby || !Map) return;
			Map->SetPropertyValue_InContainer(Lobby, World->GetOutermost()->GetName());
			Widget = CreateWidget<URPGOnlineLobbyWidget>(Controller, URPGOnlineLobbyWidget::StaticClass());
		}
		~FBackInputWorld()
		{
			if (Widget) Widget->RemoveFromParent();
			Instance->Shutdown();
			if (Viewport) Viewport->DetachViewportClient();
			if (World) { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGOnlineLobbyBackInputTest,
	"ProjectRPG.Online.DungeonLobbyBackInput.Preparation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGOnlineLobbyBackInputTest::RunTest(const FString&)
{
	if (!TestTrue(TEXT("Slate is initialized"), FSlateApplication::IsInitialized())) return false;
	FBackInputWorld F;
	if (!TestNotNull(TEXT("Owned local lobby widget"), F.Widget)) return false;
	auto* Widget = F.Widget;
	const auto Slate = Widget->TakeWidget();
	// Presentation-only credential/selection. No authentication, HTTP, save,
	// membership command, map travel or real user-focus assignment is performed.
	Widget->Web->SetAccessToken(TEXT("back-input-presentation-fixture"));
	Widget->bServerSelected = true; // This fixture starts after explicit server choice.
	Widget->Flow = NewObject<URPGDungeonSessionSubsystem>(F.Instance.Get());
	FRPGBackendCharacter Character;
	Character.CharacterId = TEXT("back-input-character"); Character.Name = TEXT("Owned hero");
	Widget->OwnedCharacters = {Character};
	Widget->bHasLoadedCharacterList = true;
	// Do not auto-load progression in a UI input test. Its existing dirty/save
	// guards remain in the production ChangeCharacter button policy unchanged.
	Widget->ConfigurationPanel = nullptr;
	if (!TestTrue(TEXT("The fixture selects one identity through the existing API"),
		Widget->Flow->SelectCharacter(Character.CharacterId))) return false;
	auto Event = [](FKey Key, bool bRepeat = false) { return FKeyEvent(Key, FModifierKeysState(), 0, bRepeat, 0, 0); };
	auto Dispatch = [&](FKey Key, bool bRepeat = false) { return Slate->OnKeyDown(FGeometry(), Event(Key, bRepeat)).IsEventHandled(); };
	auto* Change = Cast<UButton>(Widget->GetWidgetFromName(TEXT("ChangeCharacterButton")));
	if (!TestNotNull(TEXT("The original change-character action exists"), Change)) return false;
	for (const auto Key : {EKeys::Escape, EKeys::Virtual_Gamepad_Back.GetVirtualKey()})
	{
		Widget->bPreparationOpen = true; Widget->RefreshPresentation();
		if (!TestTrue(TEXT("The existing action freshly permits leaving preparation"), Change->GetIsEnabled())) return false;
		TestFalse(TEXT("Ordinary back is not stolen in preview before child input"),
			Widget->NativeOnPreviewKeyDown(FGeometry(), Event(Key)).IsEventHandled());
		TestFalse(TEXT("Accept does not become back"), Dispatch(EKeys::Enter));
		TestTrue(TEXT("A held back event is contained without another navigation action"), Dispatch(Key, true));
		TestTrue(TEXT("Held back preserves preparation"), Widget->bPreparationOpen);
		TestTrue(TEXT("The Slate object dispatches unhandled child back to the production override"), Dispatch(Key));
		TestFalse(TEXT("Fresh back uses the original action to return to character selection"), Widget->bPreparationOpen);
		TestEqual(TEXT("Page navigation preserves the selected identity"), Widget->Flow->GetSelectedCharacterId(), Character.CharacterId);
		TestTrue(TEXT("Page navigation does not log out"), Widget->Web->HasAccessToken());
		TestFalse(TEXT("Back on character selection has no implicit logout or further navigation"), Dispatch(Key));
	}
	Widget->bPreparationOpen = true; Widget->bLoadingCharacters = true; Widget->RefreshPresentation();
	TestFalse(TEXT("Outstanding request blocks the original button"), Change->GetIsEnabled());
	TestTrue(TEXT("Blocked preparation back is handled without bypassing the button"), Dispatch(EKeys::Escape));
	TestTrue(TEXT("Request lock keeps preparation open"), Widget->bPreparationOpen);
	Widget->bLoadingCharacters = false;
	auto* SessionProperty = FindFProperty<FStructProperty>(URPGDungeonSessionSubsystem::StaticClass(), TEXT("CurrentDungeonSession"));
	if (!TestNotNull(TEXT("Existing reflected session fixture storage"), SessionProperty)) return false;
	auto* Session = SessionProperty->ContainerPtrToValuePtr<FRPGDungeonSession>(Widget->Flow);
	Session->DungeonSessionId = TEXT("retained-session"); Session->State = TEXT("Waiting");
	Widget->RefreshPresentation();
	TestFalse(TEXT("Unfinished membership blocks the original button"), Change->GetIsEnabled());
	TestTrue(TEXT("Back does not turn membership into a local departure"), Dispatch(EKeys::Escape));
	TestTrue(TEXT("Unfinished preparation is retained"), Widget->bPreparationOpen);
	TestEqual(TEXT("Membership is not cleared"), Session->DungeonSessionId, FString(TEXT("retained-session")));
	*Session = FRPGDungeonSession(); Widget->RefreshPresentation();
	Widget->bAbandonmentConfirmationOpen = true;
	TestFalse(TEXT("Bubble handling does not also navigate the page while confirmation owns back"), Dispatch(EKeys::Escape));
	TestTrue(TEXT("Confirmation priority preserves the underlying preparation"), Widget->bPreparationOpen);
	Widget->bAbandonmentConfirmationOpen = false;
	Widget->Web->ClearAccessToken(); Widget->RefreshPresentation();
	TestFalse(TEXT("A hidden preparation page does not swallow login-screen back"), Dispatch(EKeys::Escape));
	TestTrue(TEXT("Authentication expiry does not discard retained preparation"), Widget->bPreparationOpen);
	return true;
}
#endif
