#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "UI/Debuffs/RPGDebuffPresentation.h"
#include "UI/Debuffs/RPGPlayerDebuffBarWidget.h"
#include "AbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Combat/RPGSlowTypes.h"
#include "Combat/RPGDefenseReductionTypes.h"
#include "GameplayEffect/RPGSlowEffect.h"
#include "GameplayEffect/RPGBurnEffect.h"
#include "GameplayEffect/RPGDefenseReductionEffect.h"
#include "GameplayEffect/RPGDamageTakenIncreaseEffect.h"
#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SWidget.h"

namespace
{
struct FDebuffFixture
{
	TStrongObjectPtr<UGameInstance> Instance;
	UWorld* World = nullptr;
	TStrongObjectPtr<URPGPlayerDebuffBarWidget> View;
	TSharedPtr<SWidget> Slate;
	bool Init()
	{
		Instance.Reset(NewObject<UGameInstance>(GEngine));
		Instance->InitializeForMinimalNetRPC(FName(*FString::Printf(TEXT("/Temp/DebuffHUD_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));
		World = Instance->GetWorld(); if (!World) return false;
#if UE_SUPPORT_FOR_ACTOR_TICK_DISABLE
		World->EnableActorTickAndUserCallbacks(true);
#endif
		World->InitializeActorsForPlay(FURL());
		return true;
	}
	UAbilitySystemComponent* Source()
	{
		auto* Pawn = World->SpawnActor<APawn>();
		auto* ASC = NewObject<UAbilitySystemComponent>(Pawn); Pawn->AddInstanceComponent(ASC); ASC->RegisterComponent();
		auto* Attributes = NewObject<URPGAttributeSet>(Pawn);
		Attributes->InitCurrentHealth(100); Attributes->InitMaxHealth(100); Attributes->InitDefense(10); Attributes->InitMoveSpeed(1);
		ASC->AddAttributeSetSubobject(Attributes); ASC->InitAbilityActorInfo(Pawn,Pawn); Pawn->DispatchBeginPlay(); return ASC;
	}
	FActiveGameplayEffectHandle Add(UAbilitySystemComponent& ASC, TSubclassOf<UGameplayEffect> Type, float Duration)
	{
		auto Spec = ASC.MakeOutgoingSpec(Type,1,ASC.MakeEffectContext()); Spec.Data->SetDuration(Duration,true);
		Spec.Data->SetSetByCallerMagnitude(RPGSlow::MagnitudeTag(), .6f);
		Spec.Data->SetSetByCallerMagnitude(RPGDefenseReduction::MagnitudeTag(), .5f);
		return ASC.ApplyGameplayEffectSpecToSelf(*Spec.Data);
	}
	~FDebuffFixture()
	{
		if (View.IsValid()) { View->SetDebuffSource(nullptr); View->ReleaseSlateResources(true); }
		Slate.Reset(); View.Reset();
		if (Instance.IsValid()) Instance->Shutdown();
		RPGTestWorld::Destroy(World);
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDebuffHUDClockTest, "ProjectRPG.UI.PlayerDebuffs.ActiveEffectsAndClock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDebuffHUDClockTest::RunTest(const FString&)
{
	FDebuffFixture F; if (!TestTrue(TEXT("World"),F.Init())) return false;
	auto* ASC = F.Source();
	const auto Short = F.Add(*ASC,URPGBurnEffect::StaticClass(),3);
	auto Long = F.Add(*ASC,URPGBurnEffect::StaticClass(),8);
	const auto Armor = F.Add(*ASC,URPGDefenseReductionEffect::StaticClass(),6);
	const auto Taken = F.Add(*ASC,URPGDamageTakenIncreaseEffect::StaticClass(),5);
	F.Add(*ASC,URPGSlowEffect::StaticClass(),4);
	const double Now = F.World->GetTimeSeconds();
	const auto First = RPGDebuffPresentation::Read(ASC,Now);
	TestEqual(TEXT("Four types, burn layers share one icon"),First.Num(),4);
	const auto* Burn = First.FindByPredicate([](const FRPGHUDDebuff& Item){return Item.Kind==ERPGHUDDebuffKind::Burn;});
	if (!TestNotNull(TEXT("Burn"),Burn)) return false;
	TestTrue(TEXT("Longest active layer supplies the remaining time"),FMath::Abs(Burn->RemainingSeconds-8.)<.02);
	const auto Later = RPGDebuffPresentation::Read(ASC,Now+2.25);
	const auto* LaterBurn = Later.FindByPredicate([](const FRPGHUDDebuff& Item){return Item.Kind==ERPGHUDDebuffKind::Burn;});
	TestTrue(TEXT("Time counts down without a new hit"),LaterBurn && FMath::Abs(LaterBurn->RemainingSeconds-5.75)<.02);
	TestEqual(TEXT("Elapsed UI time cannot remove gameplay effects"),ASC->GetActiveEffects(FGameplayEffectQuery()).Num(),5);
	TestEqual(TEXT("Expired clocks disappear from the projection"),RPGDebuffPresentation::Read(ASC,Now+9.).Num(),0);
	Long = ASC->SetActiveGameplayEffectInhibit(MoveTemp(Long),true,false);
	const auto Inhibited = RPGDebuffPresentation::Read(ASC,Now);
	const auto* Fallback = Inhibited.FindByPredicate([](const FRPGHUDDebuff& Item){return Item.Kind==ERPGHUDDebuffKind::Burn;});
	TestTrue(TEXT("Inhibited layer excluded; shorter active layer remains"),Fallback && FMath::Abs(Fallback->RemainingSeconds-3.)<.02);
	ASC->RemoveActiveGameplayEffect(Short);
	TestEqual(TEXT("Cleanse/removal hides last visible layer"),RPGDebuffPresentation::Read(ASC,Now).Num(),3);
	ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(),0);
	TestEqual(TEXT("Death clears the read-only HUD"),RPGDebuffPresentation::Read(ASC,Now).Num(),0);
	TestTrue(TEXT("UI did not remove unrelated gameplay handles"),ASC->GetActiveGameplayEffect(Armor) && ASC->GetActiveGameplayEffect(Taken));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDebuffHUDWidgetTest, "ProjectRPG.UI.PlayerDebuffs.SlotsAndSourceLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDebuffHUDWidgetTest::RunTest(const FString&)
{
	FDebuffFixture F; if (!TestTrue(TEXT("World"),F.Init())) return false;
	auto* A = F.Source(); auto* B = F.Source();
	F.Add(*A,URPGBurnEffect::StaticClass(),8);
	F.View.Reset(NewObject<URPGPlayerDebuffBarWidget>(F.World));
	if (!TestTrue(TEXT("Initialize"),F.View->Initialize())) return false;
	F.View->SetDebuffSource(A); F.Slate=F.View->TakeWidget(); F.View->RefreshPresentation();
	TestEqual(TEXT("Late HUD construction reads the current status"),F.View->GetDisplayedDebuffs().Num(),1);
	F.Add(*A,URPGSlowEffect::StaticClass(),3); F.View->RefreshPresentation();
	TestEqual(TEXT("A newly observed type appends, without moving the previous icon"),F.View->GetDisplayedDebuffs()[0].Kind,ERPGHUDDebuffKind::Burn);
	auto* Row = Cast<UHorizontalBox>(F.View->WidgetTree->FindWidget(TEXT("DebuffList")));
	if (!TestNotNull(TEXT("Real UMG array"),Row)) return false;
	TestEqual(TEXT("One slot per type"),Row->GetChildrenCount(),2);
	TestNotNull(TEXT("Icon exists above its timer"),F.View->WidgetTree->FindWidget(TEXT("DebuffIcon_5")));
	auto* Time = Cast<UTextBlock>(F.View->WidgetTree->FindWidget(TEXT("DebuffTime_5")));
	TestTrue(TEXT("Timer text exists"),Time && !Time->GetText().IsEmpty());
	F.View->SetDebuffSource(B);
	TestEqual(TEXT("Pawn/source replacement clears previous icons"),Row->GetChildrenCount(),0);
	F.Add(*A,URPGDamageTakenIncreaseEffect::StaticClass(),4); F.View->RefreshPresentation();
	TestEqual(TEXT("Old source cannot repopulate the current view"),Row->GetChildrenCount(),0);
	F.Add(*B,URPGDefenseReductionEffect::StaticClass(),5); F.View->RefreshPresentation();
	TestEqual(TEXT("New source status appears"),Row->GetChildrenCount(),1);
	B->GetAvatarActor()->Destroy(); F.View->RefreshPresentation();
	TestEqual(TEXT("Destroyed Pawn leaves no stale icon"),Row->GetChildrenCount(),0);
	F.View->SetDebuffSource(nullptr);
	return true;
}
#endif
