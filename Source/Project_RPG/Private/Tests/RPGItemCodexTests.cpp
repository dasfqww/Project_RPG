#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/UserWidget.h"
#include "Controller/RPGPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Input/Events.h"
#include "Layout/Children.h"
#include "Misc/AutomationTest.h"
#include "UI/HUD/RPGHUD.h"
#include "UI/Inventory/RPGItemCodex.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Text/STextBlock.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGItemCodexCatalogTest, "ProjectRPG.Item.UI.Codex.AuthoredCatalog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGItemCodexCatalogTest::RunTest(const FString&)
{
	const auto Entries = RPGItemCodex::LoadEntries();
	TestEqual(TEXT("First boss items are listed once across difficulties"), Entries.Num(), 6);
	int32 EquipmentCount = 0;
	for (const auto& Entry : Entries)
	{
		TestTrue(TEXT("Entries have an authored identity and name"), Entry.Id.IsValid() && !Entry.Name.IsEmpty());
		TestTrue(TEXT("Default list and name search include the entry"), Entry.Matches(TEXT("")) && Entry.Matches(Entry.Name.ToString()));
		TestFalse(TEXT("Unrelated search does not match"), Entry.Matches(TEXT("__no_item__")));
		TestTrue(TEXT("Both difficulty sources are displayed"), Entry.Sources.ContainsByPredicate([](const FText& Text)
			{ return Text.ToString().Contains(TEXT("일반")); }) && Entry.Sources.ContainsByPredicate([](const FText& Text)
			{ return Text.ToString().Contains(TEXT("고난도")); }));
		if (Entry.Matches(TEXT("장비")))
		{
			++EquipmentCount;
			TestEqual(TEXT("Equipment has one personal drop source per difficulty"), Entry.Sources.Num(), 2);
			TestTrue(TEXT("Equipment shows individual drop chance"), Entry.Sources[0].ToString().Contains(TEXT("%")));
			TestTrue(TEXT("Equipment shows its crafting cost"), Entry.Usage.ToString().Contains(TEXT("8")));
			TestFalse(TEXT("Equipment shows its base stats and effects"), Entry.Description.IsEmpty());
		}
		else
		{
			TestTrue(TEXT("Materials can be found by category"), Entry.Matches(TEXT("재료")));
			TestTrue(TEXT("Ordinary reward source shows a guaranteed material drop"), Entry.Sources.ContainsByPredicate([](const FText& Text)
				{ return Text.ToString().Contains(TEXT("일반")) && Text.ToString().Contains(TEXT("확정")); }));
		}
	}
	TestEqual(TEXT("All four craftable rewards are equipment"), EquipmentCount, 4);
	return true;
}

namespace
{
	TSharedPtr<SWidget> FindSlateWidget(const TSharedPtr<SWidget>& Root, TFunctionRef<bool(const SWidget&)> Matches)
	{
		if (!Root) return nullptr;
		if (Matches(*Root)) return Root;
		FChildren* Children = Root->GetChildren();
		for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
		{
			if (auto Child = FindSlateWidget(Children->GetChildAt(Index), Matches))
				return Child;
		}
		return nullptr;
	}

	UUserWidget* FindOpenCodex(const APlayerController& Controller)
	{
		for (TObjectIterator<UUserWidget> It; It; ++It)
		{
			if (It->GetOwningPlayer() == &Controller && It->IsInViewport() && RPGItemCodex::IsWindow(**It))
				return *It;
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGItemCodexGameplayTest, "ProjectRPG.Item.UI.Codex.Gameplay",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRPGItemCodexGameplayTest::RunTest(const FString&)
{
	UWorld* World = GEngine->GetCurrentPlayWorld();
	auto* Controller = World ? Cast<ARPGPlayerController>(World->GetFirstPlayerController()) : nullptr;
	const auto* HUD = Controller ? Cast<ARPGHUD>(Controller->GetHUD()) : nullptr;
	const auto* Root = HUD ? HUD->GetPlayerHUDWidget() : nullptr;
	if (!TestNotNull(TEXT("Gameplay HUD is active"), Root)) return false;
	auto* Host = Root->GetWidgetFromName(TEXT("ItemCodexButton"));
	if (!TestNotNull(TEXT("Gameplay HUD includes the item codex button"), Host)) return false;
	const auto Button = StaticCastSharedPtr<SButton>(FindSlateWidget(Host->GetCachedWidget(),
		[](const SWidget& Widget) { return Widget.GetType() == FName(TEXT("SButton")); }));
	if (!TestTrue(TEXT("HUD button has a Slate click target"), Button.IsValid())) return false;
	auto& Slate = FSlateApplication::Get();
	auto* Player = Controller->GetLocalPlayer();
	const auto User = Player ? Player->GetSlateUser() : nullptr;
	if (!TestTrue(TEXT("Gameplay HUD is owned by a Slate user"), User.IsValid())) return false;
	const bool bCursor = Controller->bShowMouseCursor;
	const bool bMoveIgnored = Controller->IsMoveInputIgnored();
	const bool bLookIgnored = Controller->IsLookInputIgnored();
	Button->SimulateClick();
	UUserWidget* Codex = FindOpenCodex(*Controller);
	if (!TestNotNull(TEXT("HUD click opens the gameplay codex"), Codex)) return false;
	TestTrue(TEXT("Codex owns local mouse and keyboard input"), Controller->bShowMouseCursor
		&& Controller->IsMoveInputIgnored() && Controller->IsLookInputIgnored() && World->GetGameViewport()->IgnoreInput());
	const auto CodexSlate = Codex->GetCachedWidget();
	const auto Search = StaticCastSharedPtr<SSearchBox>(FindSlateWidget(CodexSlate,
		[](const SWidget& Widget) { return Widget.GetType() == FName(TEXT("SSearchBox")); }));
	if (!TestTrue(TEXT("Codex has a search field"), Search.IsValid())) return false;
	const auto ContainsText = [&](const TCHAR* Text)
	{
		return FindSlateWidget(CodexSlate, [Text](const SWidget& Widget)
		{
			return Widget.GetType() == FName(TEXT("STextBlock"))
				&& static_cast<const STextBlock&>(Widget).GetText().ToString().Contains(Text);
		}).IsValid();
	};
	Search->SetText(FText::FromString(TEXT("__no_item__")));
	TestTrue(TEXT("Unmatched search renders the empty state"), ContainsText(TEXT("검색 결과가 없습니다.")));
	Search->SetText(FText::FromString(TEXT("재료")));
	TestTrue(TEXT("Material search renders guaranteed drops"), ContainsText(TEXT("확정")));
	TestFalse(TEXT("Material search hides equipment drops"), ContainsText(TEXT("개인 추첨")));
	Slate.ProcessKeyDownEvent(FKeyEvent(EKeys::Escape, FModifierKeysState(), User->GetUserIndex(), false, 0, 0));
	TestNull(TEXT("Escape closes the codex while search has focus"), FindOpenCodex(*Controller));
	TestTrue(TEXT("Closing restores local gameplay input"), Controller->bShowMouseCursor == bCursor
		&& Controller->IsMoveInputIgnored() == bMoveIgnored && Controller->IsLookInputIgnored() == bLookIgnored
		&& !World->GetGameViewport()->IgnoreInput());
	Button->SimulateClick();
	Codex = FindOpenCodex(*Controller);
	if (!TestNotNull(TEXT("Codex can reopen"), Codex)) return false;
	const auto CloseButton = StaticCastSharedPtr<SButton>(FindSlateWidget(Codex->GetCachedWidget(),
		[](const SWidget& Widget) { return Widget.GetType() == FName(TEXT("SButton")); }));
	if (!TestTrue(TEXT("Codex has a close button"), CloseButton.IsValid())) return false;
	CloseButton->SimulateClick();
	TestNull(TEXT("Close button removes the reopened codex"), FindOpenCodex(*Controller));
	return true;
}

#endif
