#include "Tests/RPGLootSourceTests.h"
#include "Item/Loot/RPGWorldLoot.h"
#include "Item/Loot/RPGTreasureChest.h"
#include "Item/Loot/RPGLuckyMonster.h"
#include "Item/Loot/RPGLootSourceComponent.h"
#include "Item/Loot/RPGCurrencyLootJournal.h"
#include "Item/PickUp/RPGLootPresentationComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/RPGItemCommandComponent.h"
#include "DataTable/DropItemData.h"
#include "DataAsset/StartUpData/DataAsset_StartUpDataBase.h"
#include "Player/RPGPlayerState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameplayEffectExtension.h"
#include "AIController.h"
#include "Components/SceneComponent.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include <limits>

ARPGLootTestCurrencyPickup::ARPGLootTestCurrencyPickup()
{
#if WITH_DEV_AUTOMATION_TESTS
    CurrencyCode = TEXT("RosterGold"); AmountPerUnit = 7;
#endif
}
ARPGLootTestPawn::ARPGLootTestPawn()
{
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    TestASC=CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("TestASC"));
}
UAbilitySystemComponent* ARPGLootTestPawn::GetAbilitySystemComponent() const { return TestASC; }

#if WITH_DEV_AUTOMATION_TESTS
struct FRPGLootSourceTestAccess
{
    static void Freeze(ARPGCurrencyPickup& P, FRPGEconomyTransactionRequest Request, TUniquePtr<FRPGCurrencyLootJournal> Journal)
    { P.Frozen=MoveTemp(Request); P.Journal=MoveTemp(Journal); P.bInFlight=true; }
    static void Receive(ARPGCurrencyPickup& P, FRPGEconomyCommitResult Result) { P.Receive(MoveTemp(Result)); }
    static bool Confirmed(const ARPGCurrencyPickup& P) { return P.bConfirmed; }
};
namespace RPGLootSourceTests
{
struct FFixture
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
    APlayerController* PC = nullptr; ARPGPlayerState* State = nullptr;
    FFixture()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        PC=World->SpawnActor<ARPGLootTestController>(); State=World->SpawnActor<ARPGPlayerState>(); PC->SetPlayerState(State);
        auto* Pawn=World->SpawnActor<ARPGLootTestPawn>(); PC->Possess(Pawn); Pawn->TestASC->InitAbilityActorInfo(Pawn,Pawn);
        auto* Commands=NewObject<URPGItemCommandComponent>(PC); PC->AddInstanceComponent(Commands); Commands->RegisterComponent();
        State->SetAuthenticatedIdentity(TEXT("10203040-5060-7080-90a0-b0c0d0e0f001"), TEXT("76561198000000001"),
            TEXT("10203040-5060-7080-90a0-b0c0d0e0f002"), 1);
    }
    ~FFixture() { RPGTestWorld::Destroy(World); }
    UDataTable* Table(int32 Quantity=3, float Chance=1)
    {
        auto* T=NewObject<UDataTable>(); T->RowStruct=FItemDropTable::StaticStruct();
        FItemDropTable R; FDropItem D; D.ItemClass=ARPGLootTestCurrencyPickup::StaticClass(); D.DropQuantity=Quantity; D.DropChance=Chance;
        R.DropItemList.Add(D); T->AddRow(TEXT("Loot"),R); return T;
    }
    int32 Count() const { int32 N=0; for(TActorIterator<ARPGCurrencyPickup> I(World);I;++I) if(IsValid(*I)) ++N; return N; }
};
FRPGEconomyTransactionRequest Request()
{
    FRPGEconomyTransactionRequest R; R.RequestId=FGuid::NewGuid(); R.CharacterId=TEXT("10203040-5060-7080-90a0-b0c0d0e0f001");
    R.DungeonSessionId=TEXT("10203040-5060-7080-90a0-b0c0d0e0f002"); R.Operation=TEXT("WorldLoot");
    R.CommandFingerprint=TEXT("world-loot:")+R.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower); R.Reason=TEXT("World.Loot.Currency");
    FRPGCurrencyChange C; C.CurrencyCode=TEXT("RosterGold"); C.Delta=21; R.Changes.Add(C); return R;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGLootChestTest,"ProjectRPG.Item.LootSources.ChestAuthorityAndReentry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGLootChestTest::RunTest(const FString&)
{
    using namespace RPGLootSourceTests; FFixture F;
    auto* Chest=F.World->SpawnActor<ARPGTreasureChest>(); auto* Source=Chest->LootSource.Get();
    Source->DropTable=F.Table(); Source->DropRow=TEXT("Loot");
    TestFalse(TEXT("Wrong identity cannot open"),Source->TryOpen(F.PC,FGuid::NewGuid()));
    APawn* Pawn=F.PC->GetPawn(); Pawn->SetActorLocation(FVector(10000,0,0));
    TestFalse(TEXT("Remote opener rejected"),Source->TryOpen(F.PC,Source->GetSourceId())); Pawn->SetActorLocation(FVector::ZeroVector);
    Chest->SetRole(ROLE_SimulatedProxy);
    TestFalse(TEXT("Client cannot consume source"),Source->TryOpen(F.PC,Source->GetSourceId())); Chest->SetRole(ROLE_Authority);
    bool ReentryAccepted=false;
    const auto Handle=F.World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([&](AActor* A)
    { if(A->IsA<ARPGCurrencyPickup>()) ReentryAccepted |= Source->TryOpen(F.PC,Source->GetSourceId()); }));
    TestTrue(TEXT("Authenticated opener emits"),Source->TryOpen(F.PC,Source->GetSourceId()));
    TestFalse(TEXT("Opened chest is unavailable for interaction"),Chest->CanOfferInteraction());
    F.World->RemoveOnActorSpawnedHandler(Handle);
    TestFalse(TEXT("Spawn callback cannot reopen"),ReentryAccepted);
    TestFalse(TEXT("Second party member/callback cannot reopen"),Source->TryOpen(F.PC,Source->GetSourceId()));
    TestEqual(TEXT("One event and one currency pickup"),Source->GetUsedEvents(),1); TestEqual(TEXT("One pickup"),F.Count(),1);
    for(TActorIterator<ARPGCurrencyPickup> I(F.World);I;++I)
    { TestEqual(TEXT("Amount set before callbacks"),I->Amount,21LL); TestFalse(TEXT("Flight cannot be claimed"),I->IsAvailableForPickup()); }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTimedLootExclusiveTest,"ProjectRPG.Item.LootSources.TimedPickupExclusive",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGTimedLootExclusiveTest::RunTest(const FString&)
{
    using namespace RPGLootSourceTests;
    FFixture F;
    auto* Other = F.World->SpawnActor<ARPGLootTestController>();
    Other->Possess(F.World->SpawnActor<ARPGLootTestPawn>());
    auto* Commands = NewObject<URPGItemCommandComponent>(Other);
    Other->AddInstanceComponent(Commands);
    Commands->RegisterComponent();
    auto* Pickup = F.World->SpawnActor<ARPGPickUpBase>();
    Pickup->InteractableData.InteractionDuration = 2.f;
    const FGuid DropId = Pickup->GetPickupDropId();
    TestTrue(TEXT("First player can reserve nearby loot"), Pickup->TryBeginServerTimedInteraction(*F.PC));
    TestFalse(TEXT("Reserved pickup is excluded from discovery"), Pickup->CanOfferInteraction());
    TestFalse(TEXT("Second player cannot start looting"), Pickup->TryBeginServerTimedInteraction(*Other));
    TestFalse(TEXT("Second player cannot submit a direct pickup"), Pickup->CanSubmitPickup(Other));
    TestFalse(TEXT("Reservation owner cannot complete early"), Pickup->CanSubmitPickup(F.PC));
    Pickup->EndServerTimedInteraction(FGuid::NewGuid(), false);
    TestFalse(TEXT("Stale drop identity cannot release reservation"), Pickup->CanOfferInteraction());
    Pickup->EndServerTimedInteraction(DropId, false);
    TestTrue(TEXT("Cancellation lets the second player reserve the drop"), Pickup->TryBeginServerTimedInteraction(*Other));
    Pickup->EndServerTimedInteraction(DropId, false);
    TestTrue(TEXT("Released pickup is discoverable again"), Pickup->CanOfferInteraction());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGLootValidationTest,"ProjectRPG.Item.LootSources.InvalidRowsAndNoReroll",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGLootValidationTest::RunTest(const FString&)
{
    using namespace RPGLootSourceTests; FFixture F; auto* C=F.World->SpawnActor<ARPGTreasureChest>(); auto* S=C->LootSource.Get();
    S->DropRow=TEXT("Loot"); S->DropTable=F.Table(0);
    TestFalse(TEXT("Invalid quantity does not consume event"),S->TryOpen(F.PC,S->GetSourceId())); TestEqual(TEXT("Budget unchanged"),S->GetUsedEvents(),0);
    S->DropTable=F.Table(1,std::numeric_limits<float>::quiet_NaN()); TestFalse(TEXT("NaN chance rejected"),S->TryOpen(F.PC,S->GetSourceId()));
    S->DropTable=F.Table(1,0); TestTrue(TEXT("All-miss is a consumed opening"),S->TryOpen(F.PC,S->GetSourceId()));
    S->DropTable=F.Table(); TestFalse(TEXT("Cannot reroll after table changes"),S->TryOpen(F.PC,S->GetSourceId())); TestEqual(TEXT("No miss payout"),F.Count(),0);
    auto* P=F.World->SpawnActor<ARPGLootTestCurrencyPickup>(); P->AmountPerUnit=MAX_int64;
    TestFalse(TEXT("Currency multiplication overflow rejected"),P->CanInitializeServerDropQuantity(2));
    P->AmountPerUnit=7; TestTrue(TEXT("Independent currency payload needs no item manifest"),P->CanInitializeServerDropQuantity(3));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGLootLuckyTest,"ProjectRPG.Item.LootSources.LuckyDamageBudget",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGLootLuckyTest::RunTest(const FString&)
{
    using namespace RPGLootSourceTests; FFixture F;
    auto* NPC=F.World->SpawnActor<ARPGLuckyMonster>(); auto* S=NPC->HitLoot.Get(); S->DropTable=F.Table(); S->DropRow=TEXT("Loot"); S->MaximumEvents=2;
    auto* Startup=NewObject<UDataAsset_StartUpDataBase>(GetTransientPackage());
    FindFProperty<FSoftObjectProperty>(NPC->GetClass(),TEXT("CharacterStartUpData"))->SetPropertyValue_InContainer(NPC,FSoftObjectPtr(Startup));
    auto* AI=F.World->SpawnActor<AAIController>(); AI->SetGenericTeamId(FGenericTeamId(1)); AI->Possess(NPC);
    auto* Source=CastChecked<ARPGLootTestPawn>(F.PC->GetPawn())->TestASC.Get();
    auto* Target=NPC->GetRPGAbilitySystemComponent(); Target->InitAbilityActorInfo(NPC,NPC);
    FGameplayEffectContextHandle Context=Source->MakeEffectContext(); Context.AddInstigator(F.PC->GetPawn(),F.PC->GetPawn());
    FGameplayEffectSpec Spec; Spec.SetContext(Context);
    FGameplayModifierEvaluatedData Eval(URPGAttributeSet::GetTakeDamageAttribute(),EGameplayModOp::Additive,5.f);
    FGameplayEffectModCallbackData Data(Spec,Eval,*Target);
    S->NotifyDamage(Data,10,10); S->NotifyDamage(Data,10,12); S->NotifyDamage(Data,0,0);
    TestEqual(TEXT("Heal/zero/dead never pay"),S->GetUsedEvents(),0);
    AI->SetGenericTeamId(FGenericTeamId(0)); S->NotifyDamage(Data,10,5);
    TestEqual(TEXT("Friendly damage never pays"),S->GetUsedEvents(),0); AI->SetGenericTeamId(FGenericTeamId(1));
    Target->AddAttributeSetSubobject(NPC->GetRPGAttributeSet()); NPC->GetRPGAttributeSet()->SetMaxHealth(100); NPC->GetRPGAttributeSet()->SetCurrentHealth(100);
    auto* Effect=NewObject<UGameplayEffect>(); Effect->DurationPolicy=EGameplayEffectDurationType::Instant;
    FGameplayModifierInfo M; M.Attribute=URPGAttributeSet::GetTakeDamageAttribute(); M.ModifierOp=EGameplayModOp::Override;
    M.ModifierMagnitude=FScalableFloat(5.f); Effect->Modifiers.Add(M);
    Target->ApplyGameplayEffectToSelf(Effect,1,Context);
    TestEqual(TEXT("Actual AttributeSet damage invokes loot"),S->GetUsedEvents(),1); TestEqual(TEXT("First hit pickup"),F.Count(),1);
    Target->ApplyGameplayEffectToSelf(Effect,1,Context); TestEqual(TEXT("Same time damage throttled"),S->GetUsedEvents(),1);
    F.World->TimeSeconds += .2; Target->ApplyGameplayEffectToSelf(Effect,1,Context); TestEqual(TEXT("Next valid hit pays"),S->GetUsedEvents(),2);
    F.World->TimeSeconds += .2; Target->ApplyGameplayEffectToSelf(Effect,1,Context); TestEqual(TEXT("Total budget cannot be exceeded"),F.Count(),2);
    NPC->GetRPGAttributeSet()->SetCurrentHealth(100); F.World->TimeSeconds += .2;
    Target->ApplyGameplayEffectToSelf(Effect,1,Context); TestEqual(TEXT("Healing cannot reset source budget"),F.Count(),2);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGLootJournalTest,"ProjectRPG.Item.LootSources.CurrencyWriteAheadAndReceipt",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGLootJournalTest::RunTest(const FString&)
{
    using namespace RPGLootSourceTests; FFixture F;
    const FString Root=FPaths::ProjectSavedDir()/TEXT("LootSourceTests")/FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto R=Request(); auto Journal=MakeUnique<FRPGCurrencyLootJournal>();
    TestTrue(TEXT("Original intent persisted before HTTP"),Journal->Create(Root,R));
    FRPGCurrencyLootJournal Concurrent; TestFalse(TEXT("Concurrent actor cannot adopt recipient"),Concurrent.Create(Root,R));
    FGuid Session; FGuid::Parse(R.DungeonSessionId,Session);
    const FString File=Root/Session.ToString(EGuidFormats::Digits)/(R.RequestId.ToString(EGuidFormats::Digits)+TEXT(".json"));
    FString Before; TestTrue(TEXT("Journal readable"),FFileHelper::LoadFileToString(Before,*File));
    auto* P=F.World->SpawnActor<ARPGLootTestCurrencyPickup>(); TestTrue(TEXT("Claim available resting pickup"),P->TryClaimPickup());
    FRPGLootSourceTestAccess::Freeze(*P,R,MoveTemp(Journal));
    FRPGEconomyCommitResult Unknown; Unknown.Status=ERPGEconomyBackendStatus::TransportError;
    FRPGLootSourceTestAccess::Receive(*P,Unknown); TestFalse(TEXT("Unknown response keeps actor locked"),P->IsAvailableForPickup());
    TestTrue(TEXT("Unknown keeps original file"),IFileManager::Get().FileExists(*File));
    FRPGEconomyCommitResult Ok; Ok.Status=ERPGEconomyBackendStatus::AlreadyApplied; Ok.HttpStatusCode=200; Ok.RequestId=R.RequestId;
    Ok.CharacterId=R.CharacterId; Ok.Operation=R.Operation; Ok.CommandFingerprint=R.CommandFingerprint; Ok.Reason=R.Reason;
    FRPGCurrencyChangeResult Change; Change.CurrencyCode=TEXT("RosterGold"); Change.Delta=21; Ok.Changes.Add(Change);
    auto Forged=Ok; Forged.RequestId=FGuid::NewGuid(); FRPGLootSourceTestAccess::Receive(*P,Forged);
    TestFalse(TEXT("Other receipt cannot confirm"),FRPGLootSourceTestAccess::Confirmed(*P));
    FRPGLootSourceTestAccess::Receive(*P,Ok); TestTrue(TEXT("Exact replay confirms"),FRPGLootSourceTestAccess::Confirmed(*P));
    TestTrue(TEXT("Only confirmed pickup removed"),P->IsActorBeingDestroyed()); TestFalse(TEXT("Resolved journal cleared"),IFileManager::Get().FileExists(*File));
    // Only this exact random test root; no links or shared journal state are created by this test.
    IFileManager::Get().DeleteDirectory(*Root,false,true);
    return true;
}
#endif
