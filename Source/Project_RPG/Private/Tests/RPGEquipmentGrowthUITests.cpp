#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "UI/Lobby/RPGEquipmentGrowthModel.h"
#include "UI/Lobby/RPGEquipmentGrowthWidget.h"
#include "UI/Lobby/RPGOnlineLobbyWidget.h"
#include "Item/Projection/RPGInventoryProjectionMapper.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

namespace RPGEquipmentGrowthTests
{
	const FDateTime Now(2026,11,2);
	const FGuid Character(1,2,3,4), Sword(2,0,0,1), Target(2,0,0,2), OreA(3,0,0,1), OreB(3,0,0,2);
	FRPGItemOwnerRef Owner() { FRPGItemOwnerRef O; O.Type=ERPGItemOwnerType::Character; O.OwnerId=Character.ToString(EGuidFormats::DigitsWithHyphensLower); return O; }
	FRPGItemRecord Record(const TCHAR* Name, FGuid Id, int32 Slot, int32 Quantity=1, int32 Level=0,
		int64 Revision=1, bool Explicit=true, bool Locked=false, FDateTime Expires=FDateTime())
	{
		FRPGEquipmentProgressionState Growth;
		if (Explicit) { Growth.GrowthBandId=TEXT("FirstSlice"); Growth.RuleVersion=1; Growth.Level=Level; }
		FRPGItemInstanceState State; FRPGItemInstanceState::TryRestore(Id,0,Quantity,{}, {},State,Growth);
		FRPGItemLocation Location; Location.ContainerType=ERPGItemContainerType::Inventory; Location.ContainerId=Owner().OwnerId; Location.SlotIndex=Slot;
		FRPGItemRecordMetadata Metadata; Metadata.BindState=ERPGItemBindState::CharacterBound; Metadata.bLocked=Locked; Metadata.ExpiresAtUtc=Expires;
		FRPGItemRecord R; FRPGItemRecord::TryRestore(FPrimaryAssetId(TEXT("RPGItemDefinition"),FName(Name)),1,Owner(),Location,State,Revision,ERPGItemLifecycleState::Active,Metadata,R); return R;
	}
	FRPGEquipmentMutationView View(int64 SwordRevision=9007199254740993LL, int64 OreRevision=9007199254740995LL)
	{
		TArray<FRPGItemRecord> Records={
			Record(TEXT("GameItem.Equipment.Progression.Starter.GreatSword"),Sword,0,1,2,SwordRevision),
			Record(TEXT("GameItem.Equipment.Progression.Target.CirculationGreatSword"),Target,1),
			Record(TEXT("GameItem.Material.Progression.Ore"),OreB,2,4,0,OreRevision,false),
			Record(TEXT("GameItem.Material.Progression.Ore"),OreA,3,3,0,7,false)};
		FRPGEquipmentMutationView V; V.CharacterId=Character; V.ItemsLoadState=ERPGInventoryProjectionLoadState::Ready;
		FRPGInventoryProjectionMapper::BuildInventorySnapshot(Owner(),Records,V.Items); return V;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGEquipmentGrowthPreviewTest,"ProjectRPG.UI.EquipmentGrowth.PreviewAndStaleConfirmation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGEquipmentGrowthPreviewTest::RunTest(const FString&)
{
	using namespace RPGEquipmentGrowthTests;
	const auto V=View(); TestEqual(TEXT("Fixture maps four current records"),V.Items.Num(),4);
	TestEqual(TEXT("Only initialized allowlisted equipment is selectable"),RPGEquipmentGrowthUI::Choices(V,Now).Num(),2);
	const auto U=RPGEquipmentGrowthUI::Upgrade(V,Sword,Now);
	TestTrue(TEXT("Upgrade has a complete preview"),U.bValid); TestEqual(TEXT("+2 to +3 costs six ore"),U.OreCost,6);
	TestEqual(TEXT("Split stacks selected without changing quantity"),U.Costs.Num(),2);
	if (U.Costs.Num()==2) { TestEqual(TEXT("Costs use deterministic stable IDs"),U.Costs[0].ItemId,OreA); TestEqual(TEXT("Large int64 ore revision survives"),U.Costs[1].ExpectedRevision,9007199254740995LL); }
	TestEqual(TEXT("Large int64 equipment revision survives"),U.Source.ExpectedRevision,9007199254740993LL);
	TestTrue(TEXT("Unchanged inputs may be confirmed"),U.HasSameIntent(RPGEquipmentGrowthUI::Upgrade(V,Sword,Now)));
	TestFalse(TEXT("Equipment revision change cancels old confirmation"),U.HasSameIntent(RPGEquipmentGrowthUI::Upgrade(View(9007199254740994LL),Sword,Now)));
	TestFalse(TEXT("Material revision change cancels old confirmation"),U.HasSameIntent(RPGEquipmentGrowthUI::Upgrade(View(9007199254740993LL,9007199254740996LL),Sword,Now)));
	auto Short=V; Short.Items.RemoveAll([](const auto& I){return I.GetItemId()==OreA;});
	TestFalse(TEXT("Insufficient usable ore cannot prepare a payment"),RPGEquipmentGrowthUI::Upgrade(Short,Sword,Now).bValid);
	const auto T=RPGEquipmentGrowthUI::Transfer(V,Sword,Target,Now);
	TestTrue(TEXT("Same-slot +2 to +0 transfer is available"),T.bValid); TestEqual(TEXT("Transfer has no ore cost"),T.OreCost,0);
	TestEqual(TEXT("Transfer preserves the source expectation"),T.Source.ExpectedLevel,2); TestEqual(TEXT("Transfer requires zero target"),T.Target.ExpectedLevel,0);
	TestFalse(TEXT("An item cannot transfer to itself"),RPGEquipmentGrowthUI::Transfer(V,Sword,Sword,Now).bValid);
	TestFalse(TEXT("Zero-level source cannot transfer"),RPGEquipmentGrowthUI::Transfer(V,Target,Sword,Now).bValid);
	TestEqual(TEXT("Preview does not mutate queried equipment"),V.Items[0].GetEquipmentProgression().Level,2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGEquipmentGrowthSafetyTest,"ProjectRPG.UI.EquipmentGrowth.ReadinessAndEligibility",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGEquipmentGrowthSafetyTest::RunTest(const FString&)
{
	using namespace RPGEquipmentGrowthTests;
	auto V=View();
	for (const auto State : {ERPGEquipmentMutationState::AwaitingServer,ERPGEquipmentMutationState::RecoveryRequired,ERPGEquipmentMutationState::AuthenticationRequired,ERPGEquipmentMutationState::StorageBlocked,ERPGEquipmentMutationState::Unavailable})
	{ V.State=State; TestFalse(TEXT("Unresolved or unavailable states never prepare new commands"),RPGEquipmentGrowthUI::Upgrade(V,Sword,Now).bValid); }
	V=View(); V.State=ERPGEquipmentMutationState::RecoveryRequired;
	TestFalse(TEXT("No request ID means no invented recovery"),RPGEquipmentGrowthUI::CanRetry(V));
	V.RequestId=FGuid::NewGuid(); TestTrue(TEXT("Recovery exposes only the existing intent"),RPGEquipmentGrowthUI::CanRetry(V));
	V.State=ERPGEquipmentMutationState::StorageBlocked; TestFalse(TEXT("Broken journal is not retried as a new request"),RPGEquipmentGrowthUI::CanRetry(V));
	V=View(); V.State=ERPGEquipmentMutationState::Committed; V.bNeedsInventoryRefresh=true;
	TestFalse(TEXT("Receipt success requires a fresh owner read"),RPGEquipmentGrowthUI::Upgrade(V,Sword,Now).bValid);
	V.bNeedsInventoryRefresh=false; V.ItemsLoadState=ERPGInventoryProjectionLoadState::Failed;
	TestFalse(TEXT("Failed or incomplete read is not a current inventory"),RPGEquipmentGrowthUI::Upgrade(V,Sword,Now).bValid);
	V=View(); const auto Duplicate=V.Items[0]; V.Items.Add(Duplicate); TestEqual(TEXT("Ambiguous duplicate identity hides all choices"),RPGEquipmentGrowthUI::Choices(V,Now).Num(),0);
	for (const auto& R : {
		Record(TEXT("GameItem.Equipment.Progression.Starter.GreatSword"),Sword,0,1,2,1,false),
		Record(TEXT("GameItem.Equipment.Progression.Starter.GreatSword"),Sword,0,1,2,1,true,true),
		Record(TEXT("GameItem.Equipment.Other"),Sword,0,1,2),
		Record(TEXT("GameItem.Equipment.Progression.Starter.GreatSword"),Sword,0,1,2,1,true,false,Now-FTimespan::FromSeconds(1))})
	{
		V=View(); TArray<FRPGInventoryProjectionEntry> One; FRPGInventoryProjectionMapper::BuildInventorySnapshot(Owner(),{R},One);
		V.Items.RemoveAll([](const auto& I){return I.GetItemId()==Sword;}); V.Items.Append(One);
		TestFalse(TEXT("Legacy, locked, unsupported or expired equipment is excluded"),RPGEquipmentGrowthUI::Upgrade(V,Sword,Now).bValid);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGEquipmentGrowthWidgetTest,"ProjectRPG.UI.EquipmentGrowth.LobbyEntryAndFailClosedControls",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGEquipmentGrowthWidgetTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
	Instance->InitializeForMinimalNetRPC(FName(*FString::Printf(TEXT("/Temp/EquipmentGrowth_%s"),*FGuid::NewGuid().ToString(EGuidFormats::Digits))));
	UWorld* World=Instance->GetWorld(); if (!TestNotNull(TEXT("UI test world"),World)) return false;
	auto* Viewport=NewObject<UGameViewportClient>(GEngine); Instance->GetWorldContext()->GameViewport=Viewport; Viewport->Init(*Instance->GetWorldContext(),Instance.Get(),false);
	struct FRelease { UGameInstance* Instance; UGameViewportClient* Viewport; UWorld* World; ~FRelease(){ Instance->Shutdown(); Viewport->DetachViewportClient(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); } } Release{Instance.Get(),Viewport,World};
	FString Error; auto* Player=Instance->CreateLocalPlayer(0,Error,false); if (!TestNotNull(TEXT("Local player"),Player)) return false;
	auto* PC=World->SpawnActor<APlayerController>(); PC->SetPlayer(Player); World->AddController(PC);
	TStrongObjectPtr<URPGOnlineLobbyWidget> Widget(CreateWidget<URPGOnlineLobbyWidget>(PC)); const auto Slate=Widget->TakeWidget();
	TestNotNull(TEXT("Actual lobby menu has a growth entry"),Widget->Buttons.FindRef(TEXT("OpenEquipmentMenuButton")).Get());
	auto* Panel=Widget->GetEquipmentGrowthWidget(); if (!TestNotNull(TEXT("Actual lobby has the native growth panel"),Panel)) return false;
	TestNotNull(TEXT("Current-item source selector"),Panel->SourceBox.Get()); TestNotNull(TEXT("Transfer target selector"),Panel->TargetBox.Get());
	TestFalse(TEXT("No authenticated context cannot prepare"),Panel->PrepareButton->GetIsEnabled());
	Panel->OpenScreen(); Panel->Prepare(); Panel->Confirm(); Panel->Retry();
	TestFalse(TEXT("Calling handlers cannot bypass the context guard"),Panel->bConfirming);
	TestFalse(TEXT("No context cannot confirm or pay"),Panel->ConfirmButton->GetIsEnabled());
	TestFalse(TEXT("Read without authentication is blocked"),Panel->ReloadButton->GetIsEnabled());
	TestEqual(TEXT("Confirmation starts hidden"),Panel->ConfirmButton->GetVisibility(),ESlateVisibility::Collapsed);
	TestEqual(TEXT("Transfer target role is hidden in upgrade mode"),Panel->WidgetTree->FindWidget(TEXT("EquipmentTargetLabel"))->GetVisibility(),ESlateVisibility::Collapsed);
	Panel->OperationBox->SetSelectedIndex(1);
	TestEqual(TEXT("Transfer target role is visible in transfer mode"),Panel->WidgetTree->FindWidget(TEXT("EquipmentTargetLabel"))->GetVisibility(),ESlateVisibility::Visible);
	Panel->OperationBox->SetSelectedIndex(2);
	TestEqual(TEXT("Craft mode exposes exactly four server-supported choices"),Panel->CraftBox->GetOptionCount(),4);
	TestEqual(TEXT("Craft mode shows output selector"),Panel->CraftBox->GetVisibility(),ESlateVisibility::Visible);
	TestEqual(TEXT("Craft mode hides source input"),Panel->SourceBox->GetVisibility(),ESlateVisibility::Collapsed);
	Panel->CraftBox->SetSelectedIndex(0); Panel->Prepare(); Panel->Confirm();
	TestFalse(TEXT("Craft handlers still require authenticated fresh inventory"),Panel->bConfirming);
	Panel->OperationBox->SetSelectedIndex(3);
	TestEqual(TEXT("Dismantle mode shows its exact source"),Panel->SourceBox->GetVisibility(),ESlateVisibility::Visible);
	TestEqual(TEXT("Dismantle mode hides the craft selector"),Panel->CraftBox->GetVisibility(),ESlateVisibility::Collapsed);
	Panel->SetScreenOpen(false); TestFalse(TEXT("Closing clears local confirmation"),Panel->bConfirming);
	TestFalse(TEXT("Native growth entry guard exposes an actionable denial without a lobby context"),Widget->GetEquipmentGrowthUnavailableReason(true,false).IsEmpty());
	Widget->NativeDestruct(); Panel->NativeDestruct();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGEquipmentCraftPreviewTest,"ProjectRPG.UI.EquipmentGrowth.CraftPreviewAndCosts",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGEquipmentCraftPreviewTest::RunTest(const FString&)
{
	using namespace RPGEquipmentGrowthTests;
	const FGuid TokenA(4,0,0,1),TokenB(4,0,0,2);
	auto MakeView=[&](int64 Revision=9007199254740995LL,bool Locked=false,int32 Quantity=3,FDateTime Expires=FDateTime())
	{
		auto V=View(); TArray<FRPGInventoryProjectionEntry> Tokens;
		FRPGInventoryProjectionMapper::BuildInventorySnapshot(Owner(),{
			Record(TEXT("GameItem.Material.Progression.Token"),TokenB,5,5,0,2,false),
			Record(TEXT("GameItem.Material.Progression.Token"),TokenA,4,Quantity,0,Revision,false,Locked,Expires)},Tokens);
		V.Items.Append(Tokens); return V;
	};
	const auto Targets=RPGEquipmentGrowthUI::CraftTargets(); TestEqual(TEXT("Four exact target definitions"),Targets.Num(),4);
	const auto V=MakeView();
	for (const auto& Choice : Targets)
	{
		const auto P=RPGEquipmentGrowthUI::Craft(V,Choice.Key,Now);
		TestTrue(TEXT("Every supported target has a valid fixed-price preview"),P.bValid);
		TestEqual(TEXT("Craft cost is eight tokens"),P.TokenCost,8); TestEqual(TEXT("No ore charged"),P.OreCost,0);
		TestEqual(TEXT("Selected definition is preserved exactly"),P.TargetDefinitionName,Choice.Key);
		TestEqual(TEXT("Split token stacks fill the cost"),P.Costs.Num(),2);
		if (P.Costs.Num()==2) { TestEqual(TEXT("Stable candidate order"),P.Costs[0].ItemId,TokenA); TestEqual(TEXT("Int64 token revision is exact"),P.Costs[0].ExpectedRevision,9007199254740995LL); }
		TestTrue(TEXT("Same craft preview may be confirmed"),P.HasSameIntent(RPGEquipmentGrowthUI::Craft(V,Choice.Key,Now)));
		TestFalse(TEXT("A changed token revision cancels payment confirmation"),P.HasSameIntent(RPGEquipmentGrowthUI::Craft(MakeView(9007199254740996LL),Choice.Key,Now)));
	}
	if (Targets.Num()!=4) return false;
	const auto P=RPGEquipmentGrowthUI::Craft(V,Targets[0].Key,Now);
	TestFalse(TEXT("Changing the output is a different intent"),P.HasSameIntent(RPGEquipmentGrowthUI::Craft(V,Targets[1].Key,Now)));
	TestFalse(TEXT("Starting gear is not a craft target"),RPGEquipmentGrowthUI::Craft(V,TEXT("GameItem.Equipment.Progression.Starter.GreatSword"),Now).bValid);
	TestFalse(TEXT("Seven tokens cannot prepare a craft"),RPGEquipmentGrowthUI::Craft(MakeView(1,false,2),Targets[0].Key,Now).bValid);
	TestFalse(TEXT("Locked tokens are not usable"),RPGEquipmentGrowthUI::Craft(MakeView(1,true),Targets[0].Key,Now).bValid);
	TestFalse(TEXT("Expired tokens are not usable"),RPGEquipmentGrowthUI::Craft(MakeView(1,false,3,Now-FTimespan::FromSeconds(1)),Targets[0].Key,Now).bValid);
	TestFalse(TEXT("Invalid overfull token stacks are excluded"),RPGEquipmentGrowthUI::Craft(MakeView(1,false,1000),Targets[0].Key,Now).bValid);
	TestFalse(TEXT("Ore cannot pay for crafting"),RPGEquipmentGrowthUI::Craft(View(),Targets[0].Key,Now).bValid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGEquipmentDismantlePreviewTest,"ProjectRPG.UI.EquipmentGrowth.DismantleAndRecovery",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGEquipmentDismantlePreviewTest::RunTest(const FString&)
{
	using namespace RPGEquipmentGrowthTests;
	const auto V=View(); const auto P=RPGEquipmentGrowthUI::Dismantle(V,Target,Now);
	TestTrue(TEXT("An initialized +0 target may be dismantled"),P.bValid);
	TestEqual(TEXT("Dismantle names the exact source"),P.Source.ItemId,Target);
	TestEqual(TEXT("Dismantle has no payment candidates"),P.Costs.Num(),0);
	TestFalse(TEXT("Starting or enhanced gear cannot be dismantled"),RPGEquipmentGrowthUI::Dismantle(V,Sword,Now).bValid);
	for (const auto& R : {
		Record(TEXT("GameItem.Equipment.Progression.Target.CirculationGreatSword"),Target,1,1,1),
		Record(TEXT("GameItem.Equipment.Progression.Target.CirculationGreatSword"),Target,1,1,0,1,true,true),
		Record(TEXT("GameItem.Equipment.Progression.Target.CirculationGreatSword"),Target,1,1,0,1,false),
		Record(TEXT("GameItem.Equipment.Progression.Starter.GreatSword"),Target,1)})
	{
		auto Changed=V; Changed.Items.RemoveAll([](const auto& I){return I.GetItemId()==Target;});
		TArray<FRPGInventoryProjectionEntry> One; FRPGInventoryProjectionMapper::BuildInventorySnapshot(Owner(),{R},One); Changed.Items.Append(One);
		TestFalse(TEXT("Enhanced, locked, legacy and starter records are protected"),RPGEquipmentGrowthUI::Dismantle(Changed,Target,Now).bValid);
	}
	for (auto State : {ERPGEquipmentMutationState::AwaitingServer,ERPGEquipmentMutationState::RecoveryRequired,ERPGEquipmentMutationState::StorageBlocked})
	{
		auto Pending=V; Pending.State=State; Pending.RequestId=FGuid::NewGuid();
		TestFalse(TEXT("Pending intent blocks another dismantle"),RPGEquipmentGrowthUI::Dismantle(Pending,Target,Now).bValid);
		TestFalse(TEXT("Pending intent blocks crafting"),RPGEquipmentGrowthUI::Craft(Pending,RPGEquipmentGrowthUI::CraftTargets()[0].Key,Now).bValid);
	}
	auto Refresh=V; Refresh.bNeedsInventoryRefresh=true;
	TestFalse(TEXT("A historical receipt cannot authorize dismantling"),RPGEquipmentGrowthUI::Dismantle(Refresh,Target,Now).bValid);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGEquipmentAccessReasonTest,"ProjectRPG.UI.EquipmentGrowth.AccessReasonsAndRecovery",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGEquipmentAccessReasonTest::RunTest(const FString&)
{
	using Access=FRPGEquipmentGrowthAccess;
	Access Ready; Ready.bHome=Ready.bContextReady=true;
	auto Check=[&](const TCHAR* Label,const Access& A,bool Read,bool Write,bool Retry)
	{
		TestEqual(FString(Label)+TEXT(" read"),RPGEquipmentGrowthUI::EvaluateAccess(A,false,false).bAllowed,Read);
		TestEqual(FString(Label)+TEXT(" new command"),RPGEquipmentGrowthUI::EvaluateAccess(A,true,false).bAllowed,Write);
		TestEqual(FString(Label)+TEXT(" exact recovery"),RPGEquipmentGrowthUI::EvaluateAccess(A,true,true).bAllowed,Retry);
	};
	Check(TEXT("Ready"),Ready,true,true,true);
	for (auto Flag : {&Access::bActiveSession,&Access::bSteamParty,&Access::bTravelUnresolved,&Access::bUnsavedConfiguration})
	{
		auto A=Ready; A.*Flag=true;
		Check(TEXT("New-command restriction keeps read and original recovery"),A,true,false,true);
	}
	for (auto Flag : {&Access::bTutorialActive,&Access::bProgressionBusy,&Access::bConfigurationBusy,&Access::bConfigurationUnresolved})
	{
		auto A=Ready; A.*Flag=true; Check(TEXT("Common busy or unresolved work"),A,false,false,false);
	}
	auto A=Ready; A.bHome=false; Check(TEXT("Not in home"),A,false,false,false);
	A=Ready; A.bContextReady=false; Check(TEXT("Invalid context"),A,false,false,false);
	A=Ready; A.bActiveSession=A.bConfigurationUnresolved=true; Check(TEXT("A lease exception never bypasses an unresolved skill save"),A,false,false,false);
	A=Ready; A.bActiveSession=true;
	const FText SessionReason=RPGEquipmentGrowthUI::AccessReason(A,true,false);
	A=Ready; A.bUnsavedConfiguration=true;
	TestFalse(TEXT("Different causes give different guidance"),SessionReason.EqualTo(RPGEquipmentGrowthUI::AccessReason(A,true,false)));
	auto V=RPGEquipmentGrowthTests::View();
	TestTrue(TEXT("A current inventory must not say ready when the action is denied"),RPGEquipmentGrowthUI::Status(V,SessionReason).EqualTo(SessionReason));
	V.State=ERPGEquipmentMutationState::Committed;
	TestTrue(TEXT("A confirmed old operation does not hide a new-command restriction"),RPGEquipmentGrowthUI::Status(V,SessionReason).EqualTo(SessionReason));
	for (auto State : {ERPGEquipmentMutationState::AwaitingServer,ERPGEquipmentMutationState::RecoveryRequired,ERPGEquipmentMutationState::AuthenticationRequired,ERPGEquipmentMutationState::StorageBlocked,ERPGEquipmentMutationState::Unavailable})
	{
		V.State=State; TestFalse(TEXT("Mutation recovery and authentication messages retain priority"),RPGEquipmentGrowthUI::Status(V,SessionReason).EqualTo(SessionReason));
	}
	V=RPGEquipmentGrowthTests::View(); V.bNeedsInventoryRefresh=true;
	TestFalse(TEXT("A stale receipt still requires a fresh read first"),RPGEquipmentGrowthUI::Status(V,SessionReason).EqualTo(SessionReason));
	V.bNeedsInventoryRefresh=false; V.ItemsLoadState=ERPGInventoryProjectionLoadState::Loading;
	TestFalse(TEXT("An active read still reports loading first"),RPGEquipmentGrowthUI::Status(V,SessionReason).EqualTo(SessionReason));
	V=RPGEquipmentGrowthTests::View();
	TestTrue(TEXT("Empty access reason preserves the existing ready status"),RPGEquipmentGrowthUI::Status(V).EqualTo(RPGEquipmentGrowthUI::Status(V,FText::GetEmpty())));
	return true;
}
#endif
