#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "UI/Vitals/RPGPlayerVitalsWidget.h"
#include "AbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SWidget.h"
#include <limits>

struct FRPGPlayerVitalsWidgetTestAccess
{
	static void Attach(URPGPlayerVitalsWidget& W) { W.NativeConstruct(); }
	static void Detach(URPGPlayerVitalsWidget& W) { W.NativeDestruct(); }
	static void Refresh(URPGPlayerVitalsWidget& W) { W.RefreshPresentation(); }
	static UProgressBar* Health(URPGPlayerVitalsWidget& W) { return W.HealthBar; }
	static UProgressBar* Mana(URPGPlayerVitalsWidget& W) { return W.ManaBar; }
	static UTextBlock* HealthLabel(URPGPlayerVitalsWidget& W) { return W.HealthText; }
	static UTextBlock* ManaLabel(URPGPlayerVitalsWidget& W) { return W.ManaText; }
};

namespace
{
using Access = FRPGPlayerVitalsWidgetTestAccess;
struct FVitalsSource
{
	APawn* Pawn = nullptr;
	UAbilitySystemComponent* ASC = nullptr;
	URPGAttributeSet* Attributes = nullptr;
};
struct FVitalsFixture
{
	TStrongObjectPtr<UGameInstance> Instance;
	UWorld* World = nullptr;
	UGameViewportClient* Viewport = nullptr;
	APlayerController* Controller = nullptr;
	URPGPlayerVitalsWidget* Widget = nullptr;
	TSharedPtr<SWidget> Slate;
	bool Initialize(const bool bLocalController = false)
	{
		Instance.Reset(NewObject<UGameInstance>(GEngine));
		Instance->InitializeForMinimalNetRPC(FName(*FString::Printf(TEXT("/Temp/PlayerVitals_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));
		World = Instance->GetWorld();
		if (!World) return false;
#if UE_SUPPORT_FOR_ACTOR_TICK_DISABLE
		World->EnableActorTickAndUserCallbacks(true);
#endif
		World->InitializeActorsForPlay(FURL());
		if (bLocalController)
		{
			Viewport = NewObject<UGameViewportClient>(GEngine);
			Instance->GetWorldContext()->GameViewport = Viewport;
			Viewport->Init(*Instance->GetWorldContext(), Instance.Get(), false);
			FString Error;
			auto* Player = Instance->CreateLocalPlayer(0, Error, false);
			Controller = World->SpawnActor<APlayerController>();
			if (!Player || !Controller) return false;
			Controller->SetPlayer(Player); World->AddController(Controller);
		}
		Widget = NewObject<URPGPlayerVitalsWidget>(World);
		if (!Widget->Initialize()) return false;
		if (Controller) Widget->SetOwningPlayer(Controller);
		return true;
	}
	FVitalsSource Source(const float Health, const float MaximumHealth, const float Mana, const float MaximumMana)
	{
		FVitalsSource S;
		S.Pawn = World->SpawnActor<APawn>();
		S.ASC = NewObject<UAbilitySystemComponent>(S.Pawn);
		S.Pawn->AddInstanceComponent(S.ASC); S.ASC->RegisterComponent();
		S.Attributes = NewObject<URPGAttributeSet>(S.Pawn);
		S.Attributes->InitCurrentHealth(Health); S.Attributes->InitMaxHealth(MaximumHealth);
		S.Attributes->InitCurrentMana(Mana); S.Attributes->InitMaxMana(MaximumMana);
		S.ASC->AddAttributeSetSubobject(S.Attributes);
		S.ASC->InitAbilityActorInfo(S.Pawn, S.Pawn);
		S.Pawn->DispatchBeginPlay();
		return S;
	}
	void Show() { Slate = Widget->TakeWidget(); }
	~FVitalsFixture()
	{
		if (Widget) { Access::Detach(*Widget); Widget->SetVitalsSource(nullptr); Widget->ReleaseSlateResources(true); }
		Slate.Reset();
		if (Instance.IsValid()) Instance->Shutdown();
		if (Viewport) Viewport->DetachViewportClient();
		RPGTestWorld::Destroy(World);
	}
};
bool HasBinding(UAbilitySystemComponent& ASC, URPGPlayerVitalsWidget& Widget)
{
	return ASC.GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentHealthAttribute()).IsBoundToObject(&Widget)
		|| ASC.GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetMaxHealthAttribute()).IsBoundToObject(&Widget)
		|| ASC.GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentManaAttribute()).IsBoundToObject(&Widget)
		|| ASC.GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetMaxManaAttribute()).IsBoundToObject(&Widget);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerVitalsInitialTest, "ProjectRPG.UI.PlayerVitals.InitialAndAttributeChanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPlayerVitalsInitialTest::RunTest(const FString&)
{
	FVitalsFixture F; if (!TestTrue(TEXT("Fixture"), F.Initialize())) return false;
	const auto S = F.Source(250.f, 500.f, 30.f, 60.f);
	F.Widget->SetVitalsSource(S.ASC); F.Show();
	if (!TestNotNull(TEXT("Native fallback HP bar"), Access::Health(*F.Widget)) || !TestNotNull(TEXT("Native fallback MP bar"), Access::Mana(*F.Widget))) return false;
	TestEqual(TEXT("Initial HP needs no new change event"), Access::Health(*F.Widget)->GetPercent(), .5f);
	TestEqual(TEXT("Initial MP"), Access::Mana(*F.Widget)->GetPercent(), .5f);
	TestEqual(TEXT("Initial HP text"), Access::HealthLabel(*F.Widget)->GetText().ToString(), FString(TEXT("250/500")));
	TestEqual(TEXT("Initial MP text"), Access::ManaLabel(*F.Widget)->GetText().ToString(), FString(TEXT("30/60")));
	S.ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 1000.f);
	TestEqual(TEXT("Max-only HP changes update ratio"), Access::Health(*F.Widget)->GetPercent(), .25f);
	S.ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxManaAttribute(), 120.f);
	TestEqual(TEXT("Max-only MP changes update ratio"), Access::Mana(*F.Widget)->GetPercent(), .25f);
	S.ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 750.f);
	S.ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentManaAttribute(), 15.f);
	TestEqual(TEXT("Current HP change"), Access::Health(*F.Widget)->GetPercent(), .75f);
	TestEqual(TEXT("Current MP change"), Access::Mana(*F.Widget)->GetPercent(), .125f);
	TestEqual(TEXT("Current/max snapshot"), F.Widget->GetDisplayedVitals().MaxHealth, 1000.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerVitalsLifetimeTest, "ProjectRPG.UI.PlayerVitals.SourceAndWidgetLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPlayerVitalsLifetimeTest::RunTest(const FString&)
{
	FVitalsFixture F; if (!TestTrue(TEXT("Fixture"), F.Initialize())) return false;
	const auto A = F.Source(25.f, 100.f, 10.f, 100.f), B = F.Source(80.f, 100.f, 60.f, 100.f);
	F.Widget->SetVitalsSource(A.ASC); F.Show();
	F.Widget->SetVitalsSource(B.ASC); F.Widget->SetVitalsSource(B.ASC);
	TestFalse(TEXT("Replacement removes every old attribute subscription"), HasBinding(*A.ASC, *F.Widget));
	TestEqual(TEXT("Replacement immediately reads new source"), F.Widget->GetDisplayedVitals().HealthFraction, .8f);
	A.ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 1.f);
	TestEqual(TEXT("Old Pawn cannot overwrite new display"), F.Widget->GetDisplayedVitals().HealthFraction, .8f);
	Access::Detach(*F.Widget);
	TestFalse(TEXT("Destruct removes all subscriptions"), HasBinding(*B.ASC, *F.Widget));
	TestFalse(TEXT("Destruct clears snapshot"), F.Widget->GetDisplayedVitals().bHasSource);
	B.ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 90.f);
	Access::Attach(*F.Widget);
	TestEqual(TEXT("Reconstruct reads changes while detached"), F.Widget->GetDisplayedVitals().HealthFraction, .9f);
	B.Pawn->Destroy();
	TestFalse(TEXT("Source EndPlay clears display"), F.Widget->GetDisplayedVitals().bHasSource);
	F.Widget->SetVitalsSource(A.ASC); F.Widget->SetVitalsSource(nullptr);
	TestFalse(TEXT("Null source unbinds"), HasBinding(*A.ASC, *F.Widget));
	TestTrue(TEXT("Null clears text"), Access::HealthLabel(*F.Widget)->GetText().IsEmpty());
	TestEqual(TEXT("Null collapses module"), F.Widget->GetVisibility(), ESlateVisibility::Collapsed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerVitalsPossessionTest, "ProjectRPG.UI.PlayerVitals.OwningPawnChanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPlayerVitalsPossessionTest::RunTest(const FString&)
{
	FVitalsFixture F; if (!TestTrue(TEXT("Local controller fixture"), F.Initialize(true))) return false;
	const auto A = F.Source(25.f, 100.f, 10.f, 100.f), B = F.Source(80.f, 100.f, 60.f, 100.f);
	F.Show();
	TestFalse(TEXT("Construct without Pawn is empty"), F.Widget->GetDisplayedVitals().bHasSource);
	F.Controller->Possess(A.Pawn);
	TestEqual(TEXT("Late possession automatically binds"), F.Widget->GetDisplayedVitals().HealthFraction, .25f);
	F.Controller->Possess(B.Pawn);
	TestEqual(TEXT("Possession replacement reads new HP"), F.Widget->GetDisplayedVitals().HealthFraction, .8f);
	TestFalse(TEXT("Possession replacement removes old ASC"), HasBinding(*A.ASC, *F.Widget));
	F.Controller->UnPossess();
	TestFalse(TEXT("Unpossess clears"), F.Widget->GetDisplayedVitals().bHasSource);
	TestFalse(TEXT("Unpossess unsubscribes"), HasBinding(*B.ASC, *F.Widget));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerVitalsInvalidTest, "ProjectRPG.UI.PlayerVitals.InvalidValuesAndMissingSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPlayerVitalsInvalidTest::RunTest(const FString&)
{
	FVitalsFixture F; if (!TestTrue(TEXT("Fixture"), F.Initialize())) return false;
	F.Show();
	TestFalse(TEXT("No source does not show fake full health"), F.Widget->GetDisplayedVitals().bHasSource);
	const auto S = F.Source(-10.f, 0.f, 200.f, 100.f);
	F.Widget->SetVitalsSource(S.ASC);
	TestEqual(TEXT("Zero max is finite zero"), Access::Health(*F.Widget)->GetPercent(), 0.f);
	TestEqual(TEXT("Over max clamps"), Access::Mana(*F.Widget)->GetPercent(), 1.f);
	S.Attributes->InitMaxHealth(std::numeric_limits<float>::infinity());
	S.Attributes->InitCurrentMana(std::numeric_limits<float>::quiet_NaN());
	Access::Refresh(*F.Widget);
	TestEqual(TEXT("Non-finite max sanitized"), F.Widget->GetDisplayedVitals().MaxHealth, 0.f);
	TestEqual(TEXT("Non-finite current sanitized"), F.Widget->GetDisplayedVitals().Mana, 0.f);
	TestTrue(TEXT("Text has safe numbers"), Access::ManaLabel(*F.Widget)->GetText().ToString() == TEXT("0/100"));
	return true;
}
#endif
