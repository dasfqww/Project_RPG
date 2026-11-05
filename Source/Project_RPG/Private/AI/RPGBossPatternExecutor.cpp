#include "AI/RPGBossPatternExecutor.h"
#include "AI/RPGBossAIComponent.h"
#include "AI/RPGBossDefinition.h"
#include "Ability/RPGBossGameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"

URPGBossAIComponent* URPGBossPatternExecutor::GetBoss() const { return GetTypedOuter<URPGBossAIComponent>(); }
UAbilitySystemComponent* URPGBossPatternExecutor::GetASC() const
{
	const URPGBossAIComponent* Boss = GetBoss();
	return Boss ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Boss->GetOwner()) : nullptr;
}
bool URPGBossPatternExecutor::IsCurrent(int32 Revision) const
{
	const URPGBossAIComponent* Boss = GetBoss();
	return Boss && IsValid(Boss->GetOwner()) && !Boss->GetOwner()->IsActorBeingDestroyed() &&
		Boss->GetOwner()->HasAuthority() && Boss->CanStartPattern(Revision);
}
const FRPGBossPatternDefinition* URPGBossPatternExecutor::Find(FGameplayTag PatternId) const
{
	const URPGBossAIComponent* Boss = GetBoss();
	const FRPGBossPhaseDefinition* Phase = Boss ? Boss->GetActivePhaseDefinition() : nullptr;
	if (!Phase || !PatternId.IsValid()) return nullptr;
	if (Phase->PhaseEntryPattern.PatternId == PatternId) return &Phase->PhaseEntryPattern;
	return Phase->Patterns.FindByPredicate([PatternId](const auto& Entry) { return Entry.PatternId == PatternId; });
}
const URPGBossPatternSettings* URPGBossPatternExecutor::GetPendingSettings(FGameplayAbilitySpecHandle Handle) const
{
	return (bQuerying || bLaunching) && PendingHandle == Handle && IsCurrent(PendingRevision) ? Pending.Settings.Get() : nullptr;
}
bool URPGBossPatternExecutor::Query(const FRPGBossPatternDefinition& Entry, int32 Revision, FGameplayAbilitySpecHandle& OutHandle)
{
	OutHandle = {};
	UAbilitySystemComponent* ASC = GetASC();
	if (bQuerying || bLaunching || ActiveHandle.IsValid() || !IsCurrent(Revision) || !ASC || !ASC->AbilityActorInfo.IsValid()) return false;
	const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromClass(Entry.AbilityClass);
	UGameplayAbility* Ability = Spec ? Spec->GetPrimaryInstance() : nullptr;
	if (!Cast<URPGBossGameplayAbility>(Ability) || Spec->IsActive()) return false;
	FText Error;
	if (!CastChecked<URPGBossGameplayAbility>(Ability)->ValidatePatternSettings(Entry.Settings, Error)) return false;
	TGuardValue<bool> QueryGuard(bQuerying, true);
	Pending = Entry;
	PendingRevision = Revision;
	PendingHandle = Spec->Handle;
	const FGameplayAbilitySpecHandle Handle = Spec->Handle;
	const bool bEligible = Ability->CanActivateAbility(Handle, ASC->AbilityActorInfo.Get());
	Pending = {}; PendingHandle = {}; PendingRevision = INDEX_NONE;
	if (!IsCurrent(Revision) || GetASC() != ASC) return false;
	const FGameplayAbilitySpec* Current = ASC->FindAbilitySpecFromHandle(Handle);
	if (!bEligible || !Current || Current->GetPrimaryInstance() != Ability || Current->IsActive()) return false;
	OutHandle = Handle;
	return true;
}
FGameplayAbilitySpecHandle URPGBossPatternExecutor::Select(FGameplayTag& OutPatternId)
{
	OutPatternId = {};
	const URPGBossAIComponent* Boss = GetBoss();
	const FRPGBossPhaseDefinition* Phase = Boss ? Boss->GetActivePhaseDefinition() : nullptr;
	if (!Phase || Phase->bSuspendCombatAI || Phase->Patterns.IsEmpty() || bQuerying || bLaunching || ActiveHandle.IsValid()) return {};
	const int32 Revision = Boss->GetBossState().Revision;
	const ERPGBossPatternSelection Mode = Phase->PatternSelection;
	FGameplayAbilitySpecHandle Selected;
	int32 Eligible = 0;
	for (int32 Index = 0; Index < Phase->Patterns.Num(); ++Index)
	{
		if (Mode == ERPGBossPatternSelection::Sequence && Index != NextSequenceIndex) continue;
		// Queries may invoke authored callbacks. Copy the entry and reject a changed phase afterwards.
		const FRPGBossPatternDefinition Entry = Phase->Patterns[Index];
		FGameplayAbilitySpecHandle Candidate;
		const bool bEligible = Query(Entry, Revision, Candidate);
		if (!IsCurrent(Revision)) { OutPatternId = {}; return {}; }
		if (!bEligible) continue;
		if (Mode != ERPGBossPatternSelection::Random || FMath::RandHelper(++Eligible) == 0)
		{
			Selected = Candidate; OutPatternId = Entry.PatternId;
		}
		if (Mode != ERPGBossPatternSelection::Random) break;
	}
	return Selected;
}
bool URPGBossPatternExecutor::CanActivate(FGameplayTag PatternId, int32 ExpectedRevision)
{
	const FRPGBossPatternDefinition* Entry = Find(PatternId);
	FGameplayAbilitySpecHandle Handle;
	return Entry && Query(*Entry, ExpectedRevision, Handle);
}
bool URPGBossPatternExecutor::TryActivate(FGameplayTag PatternId, int32 ExpectedRevision)
{
	const FRPGBossPatternDefinition* Found = Find(PatternId);
	if (!Found || !IsCurrent(ExpectedRevision)) return false;
	const FRPGBossPatternDefinition Entry = *Found;
	FGameplayAbilitySpecHandle Handle;
	if (!Query(Entry, ExpectedRevision, Handle)) return false;
	URPGBossAIComponent* Boss = GetBoss();
	const FRPGBossPhaseDefinition* Phase = Boss->GetActivePhaseDefinition();
	const int32 Index = Phase->Patterns.IndexOfByPredicate([PatternId](const auto& Item) { return Item.PatternId == PatternId; });
	if (Index != INDEX_NONE && (Phase->bSuspendCombatAI ||
		(Phase->PatternSelection == ERPGBossPatternSelection::Sequence && Index != NextSequenceIndex))) return false;
	UAbilitySystemComponent* ASC = GetASC();
	const FGameplayAbilitySpec* Spec = ASC ? ASC->FindAbilitySpecFromHandle(Handle) : nullptr;
	URPGBossGameplayAbility* Ability = Spec ? Cast<URPGBossGameplayAbility>(Spec->GetPrimaryInstance()) : nullptr;
	if (!Ability || Spec->IsActive() || Ability->GetBossExecutionSerial() == MAX_uint64) return false;
	TGuardValue<bool> LaunchGuard(bLaunching, true);
	Pending = Entry; PendingHandle = Handle; PendingRevision = ExpectedRevision; bConsumed = false;
	ActiveASC = ASC; ActiveAbility = Ability; ActiveHandle = Handle;
	ActiveSerial = Ability->GetBossExecutionSerial() + 1;
	ActiveEncounter = Boss->GetBossState().EncounterId; ActivePhase = Boss->GetBossState().PhaseIndex;
	ActiveSequenceIndex = Phase->PatternSelection == ERPGBossPatternSelection::Sequence ? Index : INDEX_NONE;
	EndedDelegate = ASC->OnAbilityEnded.AddUObject(this, &ThisClass::OnAbilityEnded);
	const bool bActivated = ASC->TryActivateAbility(Handle, false);
	Pending = {}; PendingHandle = {}; PendingRevision = INDEX_NONE;
	if (!bActivated)
	{
		ASC->OnAbilityEnded.Remove(EndedDelegate); EndedDelegate.Reset();
		ActiveASC.Reset(); ActiveAbility.Reset(); ActiveHandle = {};
	}
	return bActivated;
}
bool URPGBossPatternExecutor::CaptureExecution(FGameplayAbilitySpecHandle Handle, URPGBossGameplayAbility* Ability,
	FRPGBossPatternExecution& Out)
{
	Out = {};
	URPGBossAIComponent* Boss = GetBoss();
	const FRPGBossPhaseDefinition* Phase = Boss ? Boss->GetActivePhaseDefinition() : nullptr;
	if (!Phase || !Ability) return false;
	Out.bShowTelegraph = Boss->ShouldShowTelegraphsByDefault();
	if (bLaunching && !bConsumed && PendingHandle == Handle && ActiveAbility == Ability &&
		Ability->GetBossExecutionSerial() == ActiveSerial && IsCurrent(PendingRevision))
	{
		bConsumed = true;
		Out.PatternId = Pending.PatternId;
		Out.bShowTelegraph = Pending.ShouldShowTelegraph(Out.bShowTelegraph);
		Out.Settings = Pending.Settings ? DuplicateObject<URPGBossPatternSettings>(Pending.Settings, Ability) : nullptr;
		return true;
	}
	// Class-only/direct GAS callers cannot bypass the identity/policy of an entry-based attack.
	return Phase->Patterns.IsEmpty() && Phase->PhaseEntryPattern.AbilityClass != Ability->GetClass();
}
void URPGBossPatternExecutor::OnAbilityEnded(const FAbilityEndedData& Data)
{
	if (Data.AbilitySpecHandle != ActiveHandle || Data.AbilityThatEnded != ActiveAbility.Get() ||
		!ActiveAbility.IsValid() || ActiveAbility->GetBossExecutionSerial() != ActiveSerial) return;
	const URPGBossAIComponent* Boss = GetBoss();
	const FRPGBossPhaseDefinition* Phase = Boss ? Boss->GetActivePhaseDefinition() : nullptr;
	if (!Data.bWasCancelled && Phase && Boss->GetBossState().EncounterId == ActiveEncounter &&
		Boss->GetBossState().PhaseIndex == ActivePhase && Phase->Patterns.IsValidIndex(ActiveSequenceIndex))
	{
		NextSequenceIndex = (ActiveSequenceIndex + 1) % Phase->Patterns.Num();
	}
	if (ActiveASC.IsValid()) ActiveASC->OnAbilityEnded.Remove(EndedDelegate);
	EndedDelegate.Reset(); ActiveASC.Reset(); ActiveAbility.Reset(); ActiveHandle = {};
}
void URPGBossPatternExecutor::Reset()
{
	if (ActiveASC.IsValid()) ActiveASC->OnAbilityEnded.Remove(EndedDelegate);
	EndedDelegate.Reset(); ActiveASC.Reset(); ActiveAbility.Reset(); ActiveHandle = {};
	Pending = {}; PendingHandle = {}; PendingRevision = INDEX_NONE;
	NextSequenceIndex = 0; ActiveSequenceIndex = INDEX_NONE;
}
