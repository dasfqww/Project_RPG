#if WITH_DEV_AUTOMATION_TESTS
#include "Online/RPGOnlineLobbySubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "IpConnection.h"
#include "IpNetDriver.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace
{
	struct FLobbyWorld
	{
		TStrongObjectPtr<UGameInstance> Instance{NewObject<UGameInstance>(GEngine)};
		UWorld* World = nullptr;
		APlayerController* Controller = nullptr;
		URPGOnlineLobbySubsystem* Lobby = nullptr;
		FString Directory;
		FLobbyWorld(const TCHAR* Leaf, EWorldType::Type Type)
		{
			Directory = FString::Printf(TEXT("/Temp/LobbyWorld_%s/"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
			Instance->InitializeForMinimalNetRPC(FName(*(Directory + Leaf)));
			World = Instance->GetWorld();
			if (!World) return;
			World->WorldType = Type;
			Controller = World->SpawnActor<APlayerController>();
			if (Controller) { Controller->SetAsLocalPlayerController(); World->AddController(Controller); }
			Lobby = Instance->GetSubsystem<URPGOnlineLobbySubsystem>();
			SetLobbyMap(Directory + TEXT("LobbyMap"));
		}
		void SetLobbyMap(const FString& Package) const
		{
			if (Lobby)
			{
				auto* Property = FindFProperty<FStrProperty>(Lobby->GetClass(), TEXT("LobbyMapPackage"));
				check(Property);
				Property->SetPropertyValue_InContainer(Lobby, Package);
			}
		}
		~FLobbyWorld()
		{
			if (World && World->GetNetDriver())
			{
				World->GetNetDriver()->ServerConnection = nullptr;
				World->GetNetDriver()->SetWorld(nullptr);
				World->SetNetDriver(nullptr);
			}
			Instance->Shutdown();
			if (World) { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGOnlineLobbyWorldPackageTest,
	"ProjectRPG.Online.DungeonLobbyWorld.ExactPackageAndPIE",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGOnlineLobbyWorldPackageTest::RunTest(const FString&)
{
	for (const auto Type : {EWorldType::Game, EWorldType::PIE})
	{
		FLobbyWorld F(TEXT("LobbyMap"), Type);
		if (!TestNotNull(TEXT("Local lobby subsystem"), F.Lobby) || !TestNotNull(TEXT("Local controller"), F.Controller)) return false;
		TestTrue(TEXT("The exact configured lobby remains accepted"), F.Lobby->IsLobbyController(F.Controller));
	}
	for (const TCHAR* Leaf : {TEXT("UEDPIE_0_LobbyMap"), TEXT("UEDPIE_7_LobbyMap")})
	{
		FLobbyWorld F(Leaf, EWorldType::PIE);
		TestTrue(TEXT("A genuine PIE world resolves its duplicated lobby package"), F.Lobby->IsLobbyController(F.Controller));
		F.SetLobbyMap(F.Directory + TEXT("Other/LobbyMap"));
		TestFalse(TEXT("Matching the leaf does not permit a different directory"), F.Lobby->IsLobbyController(F.Controller));
	}
	for (const auto Type : {EWorldType::Game, EWorldType::GamePreview})
	{
		FLobbyWorld F(TEXT("UEDPIE_0_LobbyMap"), Type);
		TestFalse(TEXT("An ordinary game/preview cannot borrow a PIE-looking name"), F.Lobby->IsLobbyController(F.Controller));
	}
	for (const TCHAR* Leaf : {TEXT("UEDPIE_0_bossmap"), TEXT("UEDPIE_0_L_ProgressionTraining"), TEXT("UEDPIE_0_lobbymap")})
	{
		FLobbyWorld F(Leaf, EWorldType::PIE);
		TestFalse(TEXT("Dungeon and case-mismatched packages remain excluded"), F.Lobby->IsLobbyController(F.Controller));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGOnlineLobbyWorldOwnershipTest,
	"ProjectRPG.Online.DungeonLobbyWorld.OwnershipAndNetwork",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGOnlineLobbyWorldOwnershipTest::RunTest(const FString&)
{
	FLobbyWorld F(TEXT("UEDPIE_0_LobbyMap"), EWorldType::PIE);
	if (!TestNotNull(TEXT("Owned controller"), F.Controller) || !TestNotNull(TEXT("Owned lobby"), F.Lobby)) return false;
	TestTrue(TEXT("Standalone PIE starts with an eligible local controller"), F.Lobby->IsLobbyController(F.Controller));
	TestFalse(TEXT("Missing controller is excluded"), F.Lobby->IsLobbyController(nullptr));
	{
		FLobbyWorld Other(TEXT("LobbyMap"), EWorldType::Game);
		Other.SetLobbyMap(F.Directory + TEXT("LobbyMap"));
		TestFalse(TEXT("A matching package cannot cross game-instance ownership"), Other.Lobby->IsLobbyController(F.Controller));
	}
	F.World->WorldType = EWorldType::Editor;
	F.SetLobbyMap(F.World->GetOutermost()->GetName());
	TestFalse(TEXT("An editor world is excluded even on an exact package"), F.Lobby->IsLobbyController(F.Controller));
	F.World->WorldType = EWorldType::PIE;
	F.SetLobbyMap(F.Directory + TEXT("LobbyMap"));
	auto* Driver = NewObject<UIpNetDriver>(F.World);
	Driver->SetWorld(F.World);
	F.World->SetNetDriver(Driver);
	Driver->ServerConnection = NewObject<UIpConnection>(Driver);
	TestEqual(TEXT("The socket-free client fixture has client net mode"), F.World->GetNetMode(), NM_Client);
	TestFalse(TEXT("A network client cannot show the local frontend"), F.Lobby->IsLobbyController(F.Controller));
	Driver->ServerConnection = nullptr;
	TestEqual(TEXT("The socket-free server fixture has listen-server net mode"), F.World->GetNetMode(), NM_ListenServer);
	TestFalse(TEXT("A listen server cannot show the local frontend"), F.Lobby->IsLobbyController(F.Controller));
	return true;
}
#endif
