#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "UI/Vitals/RPGPlayerShieldWidget.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGPlayer.h"
#include "Component/Combat/RPGShieldComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Components/TextBlock.h"
#include "DataAsset/StartUpData/DataAsset_StartUpDataBase.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Widgets/SWidget.h"

struct FRPGPlayerShieldWidgetTestAccess
{
	static void Attach(URPGPlayerShieldWidget& W) { W.NativeConstruct(); }
	static void Detach(URPGPlayerShieldWidget& W) { W.NativeDestruct(); }
	static void Tick(URPGPlayerShieldWidget& W) { W.NativeTick(FGeometry(), .1f); }
	static UTextBlock* Amount(URPGPlayerShieldWidget& W) { return W.ShieldText; }
	static UTextBlock* Time(URPGPlayerShieldWidget& W) { return W.ShieldTimeText; }
	static bool Listens(URPGShieldComponent& Shield, URPGPlayerShieldWidget& W)
	{ return Shield.OnShieldChanged.IsAlreadyBound(&W, &URPGPlayerShieldWidget::OnShieldChanged); }
};

namespace RPGShieldHUDTests
{
struct FFixture
{
	TStrongObjectPtr<UGameInstance> Instance;
	TStrongObjectPtr<UUserWidget> Root;
	UWorld* World = nullptr;
	UGameViewportClient* Viewport = nullptr;
	APlayerController* PC = nullptr;
	URPGPlayerShieldWidget* View = nullptr;
	TSharedPtr<SWidget> Slate;
	uint64 PreviousFrame = GFrameCounter;
	bool Initialize(bool bAuthoredOwner = false)
	{
		Instance.Reset(NewObject<UGameInstance>(GEngine));
		Instance->InitializeForMinimalNetRPC(FName(*FString::Printf(TEXT("/Temp/ShieldHUD_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));
		World = Instance->GetWorld(); if (!World) return false;
#if UE_SUPPORT_FOR_ACTOR_TICK_DISABLE
		World->EnableActorTickAndUserCallbacks(true);
#endif
		World->InitializeActorsForPlay(FURL()); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
		if (bAuthoredOwner)
		{
			Viewport = NewObject<UGameViewportClient>(GEngine);
			Instance->GetWorldContext()->GameViewport = Viewport; Viewport->Init(*Instance->GetWorldContext(), Instance.Get(), false);
			FString Error; auto* Local = Instance->CreateLocalPlayer(0, Error, false);
			PC = World->SpawnActor<APlayerController>(); if (!Local || !PC) return false;
			PC->SetPlayer(Local); World->AddController(PC);
		}
		UClass* Class = bAuthoredOwner ? LoadClass<UUserWidget>(nullptr, TEXT("/Game/Blueprints/UI/PlayerHUD/Modules/WBP_PlayerVitals.WBP_PlayerVitals_C"))
			: URPGPlayerShieldWidget::StaticClass();
		if (!Class) return false;
		Root.Reset(NewObject<UUserWidget>(World, Class));
		if (!Root->Initialize()) return false;
		if (PC) Root->SetOwningPlayer(PC);
		Slate = Root->TakeWidget();
		View = bAuthoredOwner ? Cast<URPGPlayerShieldWidget>(Root->GetWidgetFromName(TEXT("PlayerShield"))) : Cast<URPGPlayerShieldWidget>(Root.Get());
		if (View) FRPGPlayerShieldWidgetTestAccess::Attach(*View);
		return View != nullptr;
	}
	ARPGPlayer* Player()
	{
		auto* P = World->SpawnActor<ARPGPlayer>();
		if (!P) return nullptr;
		P->GetCharacterMovement()->SetMovementMode(MOVE_None);
		auto* Startup = FindFProperty<FSoftObjectProperty>(ARPGBaseCharacter::StaticClass(), TEXT("CharacterStartUpData"));
		if (!Startup) return nullptr;
		*Startup->ContainerPtrToValuePtr<TSoftObjectPtr<UDataAsset_StartUpDataBase>>(P) = NewObject<UDataAsset_StartUpDataBase>(P);
		auto* ASC = P->GetRPGAbilitySystemComponent(); ASC->InitAbilityActorInfo(P, P);
		ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 1000.f);
		ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 1000.f);
		return P;
	}
	void Advance(int32 Frames)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame) { ++GFrameCounter; World->Tick(LEVELTICK_All, 1.f / 60.f); }
	}
	~FFixture()
	{
		if (View) FRPGPlayerShieldWidgetTestAccess::Detach(*View);
		if (Root.IsValid()) Root->ReleaseSlateResources(true);
		Slate.Reset();
		if (Instance.IsValid()) Instance->Shutdown();
		if (Viewport) Viewport->DetachViewportClient();
		RPGTestWorld::Destroy(World); GFrameCounter = PreviousFrame;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGShieldHUDStateTest, "ProjectRPG.UI.PlayerShield.StateAndLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGShieldHUDStateTest::RunTest(const FString&)
{
	using namespace RPGShieldHUDTests;
	FFixture F; if (!TestTrue(TEXT("Fixture"), F.Initialize())) return false;
	auto* A = F.Player(); auto* B = F.Player();
	if (!TestNotNull(TEXT("Player A"), A) || !TestNotNull(TEXT("Player B"), B)) return false;
	auto* ShieldA = A->FindComponentByClass<URPGShieldComponent>(); auto* ShieldB = B->FindComponentByClass<URPGShieldComponent>();
	if (!TestNotNull(TEXT("Player shield exists before the first hit/HUD subscription"), ShieldA) || !TestNotNull(TEXT("Player B shield"), ShieldB)) return false;
	F.View->SetShieldSource(ShieldA);
	TestFalse(TEXT("Empty shield stays hidden"), F.View->GetDisplayedShield().bVisible);
	const FGuid Source = FGuid::NewGuid();
	TestTrue(TEXT("Grant"), ShieldA->GrantOrRefresh(Source, .05f, 3.f));
	TestTrue(TEXT("First grant wakes the collapsed widget through its event"), F.View->GetDisplayedShield().bVisible);
	TestEqual(TEXT("Amount"), F.View->GetDisplayedShield().Remaining, 50.f);
	if (!TestNotNull(TEXT("Native amount label"), FRPGPlayerShieldWidgetTestAccess::Amount(*F.View))) return false;
	TestTrue(TEXT("Actual label"), FRPGPlayerShieldWidgetTestAccess::Amount(*F.View)->GetText().ToString().Contains(TEXT("50")));
	ShieldA->AbsorbDamage(20.f);
	TestEqual(TEXT("Damage updates through the component delegate"), F.View->GetDisplayedShield().Remaining, 30.f);
	F.Advance(30); FRPGPlayerShieldWidgetTestAccess::Tick(*F.View);
	TestTrue(TEXT("Countdown advances without a new shield event"), F.View->GetDisplayedShield().SecondsRemaining < 2.6f);
	TestEqual(TEXT("Display never heals or damages health"), A->GetRPGAttributeSet()->GetCurrentHealth(), 1000.f);
	ShieldA->GrantOrRefresh(Source, .05f, 3.f);
	TestTrue(TEXT("Refresh uses the new server deadline"), F.View->GetDisplayedShield().SecondsRemaining > 2.9f);
	// Replication may deliver a positive amount whose absolute deadline is already past.
	auto* StateProperty = FindFProperty<FStructProperty>(URPGShieldComponent::StaticClass(), TEXT("State"));
	if (!TestNotNull(TEXT("Replicated shield state"), StateProperty)) return false;
	auto& State = *StateProperty->ContainerPtrToValuePtr<FRPGShieldPresentation>(ShieldA);
	State.ExpiresAt = F.World->GetTimeSeconds() - .01f;
	F.View->RefreshPresentation();
	TestFalse(TEXT("Late expired state cannot restart a countdown"), F.View->GetDisplayedShield().bVisible);
	TestEqual(TEXT("Presentation cannot clear server state"), ShieldA->GetShieldState().Remaining, 50.f);
	F.View->SetShieldSource(ShieldB); ShieldA->GrantOrRefresh(Source, .05f, 3.f);
	TestFalse(TEXT("Old source cannot wake the new view"), F.View->GetDisplayedShield().bVisible);
	ShieldB->GrantOrRefresh(FGuid::NewGuid(), .02f, 3.f);
	TestEqual(TEXT("New source"), F.View->GetDisplayedShield().Remaining, 20.f);
	B->GetRPGAbilitySystemComponent()->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 0.f);
	TestFalse(TEXT("Death hides the row"), F.View->GetDisplayedShield().bVisible);
	FRPGPlayerShieldWidgetTestAccess::Detach(*F.View);
	TestFalse(TEXT("Destruct removes shield subscription"), FRPGPlayerShieldWidgetTestAccess::Listens(*ShieldB, *F.View));
	TestFalse(TEXT("Destruct removes health subscription"), B->GetRPGAbilitySystemComponent()->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentHealthAttribute()).IsBoundToObject(F.View));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGShieldHUDOwnerTest, "ProjectRPG.UI.PlayerShield.AuthoredOwnerAndPawnChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGShieldHUDOwnerTest::RunTest(const FString&)
{
	using namespace RPGShieldHUDTests;
	FFixture F; if (!TestTrue(TEXT("Saved Vitals module and native child"), F.Initialize(true))) return false;
	auto* A = F.Player(); auto* B = F.Player();
	if (!TestNotNull(TEXT("Player A"), A) || !TestNotNull(TEXT("Player B"), B)) return false;
	auto* ShieldA = A->FindComponentByClass<URPGShieldComponent>(); auto* ShieldB = B->FindComponentByClass<URPGShieldComponent>();
	if (!TestNotNull(TEXT("Shield A"), ShieldA) || !TestNotNull(TEXT("Shield B"), ShieldB)) return false;
	F.PC->Possess(A); ShieldA->GrantOrRefresh(FGuid::NewGuid(), .05f, 3.f);
	TestEqual(TEXT("Authored child follows the owning Pawn"), F.View->GetDisplayedShield().Remaining, 50.f);
	F.View->SetShieldSource(ShieldB);
	const FGuid SourceB = FGuid::NewGuid();
	ShieldB->GrantOrRefresh(SourceB, .02f, 3.f);
	TestFalse(TEXT("An owned view rejects an explicit foreign Pawn"), F.View->GetDisplayedShield().bVisible);
	F.View->SetShieldSource(ShieldA);
	F.PC->Possess(B); ShieldB->GrantOrRefresh(SourceB, .03f, 3.f);
	TestEqual(TEXT("Pawn replacement follows the new source"), F.View->GetDisplayedShield().Remaining, 30.f);
	ShieldA->GrantOrRefresh(FGuid::NewGuid(), .09f, 3.f);
	TestEqual(TEXT("Late old-Pawn events cannot overwrite the current total"), F.View->GetDisplayedShield().Remaining, 30.f);
	F.PC->UnPossess();
	TestFalse(TEXT("Unpossess clears the authored child"), F.View->GetDisplayedShield().bVisible);
	return true;
}
#endif
