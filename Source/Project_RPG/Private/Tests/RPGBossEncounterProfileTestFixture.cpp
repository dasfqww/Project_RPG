#include "Tests/RPGBossEncounterProfileTestFixture.h"
#include "Tests/RPGBossDamageTestFixture.h"
#include "Ability/RPGBossTelegraphedStrikeAbility.h"
#include "AbilitySystemComponent.h"
#include "AI/RPGBossDefinition.h"
#include "AI/RPGBossEncounterProfiles.h"
#include "AI/RPGBossThreatComponent.h"
#include "AI/RPGBossTelegraphPresentationComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Controller/RPGAIController.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NativeGameplayTags.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ProfileNetShow, "Tests.RaidBoss.Profile.Network.Show");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ProfileNetHide, "Tests.RaidBoss.Profile.Network.Hide");

ARPGBossProfileTestPawn::ARPGBossProfileTestPawn()
{
	Presentation = CreateDefaultSubobject<URPGBossTelegraphPresentationComponent>(TEXT("ProfilePresentation"));
	FRPGBossTelegraphStyle Style;
	Style.ActionTag = FGameplayTag::RequestGameplayTag(TEXT("Tests.RaidBoss.Network.Telegraph"), false);
	Style.Radius = 250.f;
	Style.Material = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Blueprints/Character/NPC/SwordMan/Presentation/M_RaidSwordManTelegraph.M_RaidSwordManTelegraph")));
	Presentation->Styles = {Style};
}
void ARPGBossProfileTestPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
	if (HasAuthority() || !FParse::Param(FCommandLine::Get(), TEXT("RPGBossProfileNetwork"))) return;
	const auto State = Boss->GetBossState(); const auto Telegraph = Boss->GetTelegraphState();
	if (Telegraph.bActive && Telegraph.EncounterId == State.EncounterId && State.EncounterState == ERPGBossEncounterState::InCombat)
	{
		const bool bExpectedShow = Telegraph.PatternId == TAG_ProfileNetShow;
		if (Telegraph.bShowIndicator != bExpectedShow || (!bExpectedShow && Presentation->IsIndicatorVisible()))
		{
			UE_LOG(LogTemp, Error, TEXT("PROFILE_NET_FAIL PolicyOrVisibility")); return;
		}
		if ((!bExpectedShow || Presentation->IsIndicatorVisible()) && ObservedSequence != Telegraph.Sequence)
		{
			ObservedSequence = Telegraph.Sequence;
			if (Boss->TryActivatePattern(Telegraph.PatternId, State.EncounterId, State.PhaseTag) ||
				Boss->BeginTelegraph(Telegraph.ActionTag, Telegraph.AimLocation, 1.f) != 0)
			{
				UE_LOG(LogTemp, Error, TEXT("PROFILE_NET_FAIL ClientAuthority")); return;
			}
			UE_LOG(LogTemp, Display, TEXT("PROFILE_NET_%s Sequence=%d Active=1 AuthorityDenied=1"),
				bExpectedShow ? TEXT("SHOWN") : TEXT("HIDDEN"), Telegraph.Sequence);
		}
		if (State.EncounterId > 1 && !bObservedRestart)
		{
			bObservedRestart = true; UE_LOG(LogTemp, Display, TEXT("PROFILE_NET_RESTARTED"));
		}
	}
	if (!Telegraph.bActive && !Presentation->IsIndicatorVisible())
	{
		if (State.EncounterState == ERPGBossEncounterState::Wiped && !bObservedWipe)
		{
			bObservedWipe = true; UE_LOG(LogTemp, Display, TEXT("PROFILE_NET_WIPED Cleared=1"));
		}
		if (State.EncounterState == ERPGBossEncounterState::Dead && !bObservedDeath)
		{
			bObservedDeath = true; UE_LOG(LogTemp, Display, TEXT("PROFILE_NET_DEAD Cleared=1"));
		}
	}
#endif
}
ARPGBossProfileTestGameMode::ARPGBossProfileTestGameMode()
{
	PrimaryActorTick.bCanEverTick = true; DefaultPawnClass = nullptr; PlayerControllerClass = ARPGBossThreatProbeController::StaticClass();
}
void ARPGBossProfileTestGameMode::BeginPlay()
{
	AGameModeBase::BeginPlay();
#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("RPGBossProfileNetwork"))) return;
	SetGameDifficulty(ERPGGameDifficulty::Hard);
	Source = GetWorld()->SpawnActor<ARPGBossProfileTestPawn>(); Target = GetWorld()->SpawnActor<ARPGBossTestPawn>();
	auto* AI = GetWorld()->SpawnActor<ARPGAIController>(); AI->Possess(Source);
	AI->GetPerceptionComponent()->SetSenseEnabled(UAISense_Sight::StaticClass(), false);
	auto* TargetAI = GetWorld()->SpawnActor<AAIController>(); TargetAI->SetGenericTeamId(FGenericTeamId(0)); TargetAI->Possess(Target);
	Target->SetActorLocation(FVector(100.f, 0.f, 0.f));
	NewObject<URPGBossThreatComponent>(Source)->RegisterComponent();
	Source->ASC->SetNumericAttributeBase(URPGAttributeSet::GetCriticalChanceAttribute(), 0.f);
	auto* Profiles = NewObject<URPGBossEncounterProfiles>(Source);
	auto* Hard = ARPGBossTestPawn::MakeDefinition(Profiles); Hard->Phases.SetNum(1); Hard->bShowTelegraphsByDefault = false;
	FRPGBossPatternDefinition Shown; Shown.PatternId = TAG_ProfileNetShow; Shown.AbilityClass = URPGBossTelegraphedStrikeAbility::StaticClass(); Shown.TelegraphPolicy = ERPGBossTelegraphPolicy::Show;
	FRPGBossPatternDefinition Hidden = Shown; Hidden.PatternId = TAG_ProfileNetHide; Hidden.TelegraphPolicy = ERPGBossTelegraphPolicy::Inherit;
	Hard->Phases[0].Patterns = {Shown, Hidden}; Profiles->Profiles.Add(ERPGGameDifficulty::Hard, Hard);
	Source->Boss->EncounterProfiles = Profiles;
	Handle = Source->ASC->GiveAbility(FGameplayAbilitySpec(URPGBossTelegraphedStrikeAbility::StaticClass(), 1));
	Attack = CastChecked<URPGBossTelegraphedStrikeAbility>(Source->ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
	Attack->ActionTag = FGameplayTag::RequestGameplayTag(TEXT("Tests.RaidBoss.Network.Telegraph"));
	Attack->WindupSeconds = 120.f; Attack->Radius = 250.f; Attack->DamageEffectClass = URPGBossTestDamageEffect::StaticClass(); Attack->BaseDamage.Value = 31.f;
	if (!Source->Boss->StartEncounter()) { UE_LOG(LogTemp, Error, TEXT("PROFILE_NET_FAIL Start")); return; }
	UE_LOG(LogTemp, Display, TEXT("PROFILE_NET_READY HardProfile=1"));
#endif
}
bool ARPGBossProfileTestGameMode::Activate(bool bShow)
{
	const auto State = Source->Boss->GetBossState();
	Source->FindComponentByClass<URPGBossThreatComponent>()->AddThreat(Target, 10.f, State.EncounterId);
	const bool bStarted = Source->Boss->TryActivatePattern(bShow ? TAG_ProfileNetShow : TAG_ProfileNetHide, State.EncounterId, State.PhaseTag);
	if (!bStarted) UE_LOG(LogTemp, Error, TEXT("PROFILE_NET_FAIL Activate Stage=%d"), Stage);
	StageTime = GetWorld()->GetTimeSeconds(); return bStarted;
}
void ARPGBossProfileTestGameMode::Tick(float DeltaSeconds)
{
	AGameModeBase::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
	if (!Source) return;
	const double Elapsed = GetWorld()->GetTimeSeconds() - StageTime;
	FString Gate;
	FParse::Value(FCommandLine::Get(), TEXT("BossProfileGate="), Gate);
	const auto Ready = [&Gate](const TCHAR* Suffix) { return !Gate.IsEmpty() && FPaths::FileExists(Gate + Suffix); };
	if (Stage == 0 && Clients >= 1) { if (Activate(true)) Stage = 1; }
	else if (Stage == 1 && Elapsed > 2.0 && Ready(TEXT(".shown")))
	{
		Source->ASC->CancelAbilityHandle(Handle);
		if (Activate(false)) { Stage = 2; UE_LOG(LogTemp, Display, TEXT("PROFILE_NET_HIDDEN_READY LateJoinWindow=1")); }
	}
	else if (Stage == 2 && Clients >= 2 && Ready(TEXT(".latehidden")))
	{
		Source->ASC->CancelAbilityHandle(Handle); Attack->WindupSeconds = 2.f;
		if (Activate(false)) Stage = 3;
	}
	else if (Stage == 3 && Elapsed > 3.5)
	{
		if (Target->Attributes->GetCurrentHealth() != 69.f) { UE_LOG(LogTemp, Error, TEXT("PROFILE_NET_FAIL HiddenDamage")); Stage = 99; return; }
		UE_LOG(LogTemp, Display, TEXT("PROFILE_NET_DAMAGE Health=69 Hidden=1"));
		Attack->WindupSeconds = 120.f;
		if (Activate(true)) Stage = 4;
	}
	else if (Stage == 4 && Elapsed > 2.0 && Ready(TEXT(".shownagain")))
	{
		Source->Boss->MarkWiped(); Stage = 5; StageTime = GetWorld()->GetTimeSeconds();
	}
	else if (Stage == 5 && Elapsed > 3.0)
	{
		Source->Boss->BeginReset(); Source->Boss->CompleteReset(); Source->Boss->StartEncounter();
		if (Activate(false)) Stage = 6;
	}
	else if (Stage == 6 && Elapsed > 3.0)
	{
		Source->SetTestHealth(0.f); Stage = 7;
		UE_LOG(LogTemp, Display, TEXT("PROFILE_NET_DONE HiddenDamage=1 Reset=1 Death=1"));
	}
#endif
}
void ARPGBossProfileTestGameMode::PostLogin(APlayerController* NewPlayer)
{
	AGameModeBase::PostLogin(NewPlayer); ++Clients;
}
void ARPGBossProfileTestGameMode::Logout(AController* Exiting)
{
	--Clients; AGameModeBase::Logout(Exiting); UE_LOG(LogTemp, Display, TEXT("PROFILE_NET_DISCONNECTED"));
}
