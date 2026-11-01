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
	Panel->SetScreenOpen(false); TestFalse(TEXT("Closing clears local confirmation"),Panel->bConfirming);
	Widget->NativeDestruct(); Panel->NativeDestruct();
	return true;
}
#endif
