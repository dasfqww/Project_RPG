#include "PlayerClass/RPGPlayerClassComponent.h"

#include "Ability/RPGGameplayAbility.h"
#include "Ability/Skill/ActivateIdentity.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Net/UnrealNetwork.h"
#include "PlayerClass/RPGPlayerClassDefinition.h"
#include "RPGGameplayTags.h"
#include "Security/RPGDungeonGameplayAuthority.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "Skill/RPGSkillDefinition.h"

URPGPlayerClassComponent::URPGPlayerClassComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void URPGPlayerClassComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ThisClass, ApplyState, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ThisClass, OwnedGrants, COND_OwnerOnly);
}

void URPGPlayerClassComponent::OnRegister()
{
	Super::OnRegister();
	BindProgression();
}

void URPGPlayerClassComponent::BeginPlay()
{
	Super::BeginPlay();
	BindProgression();
	RefreshAppliedClass();
}

void URPGPlayerClassComponent::OnUnregister()
{
	UnbindProgression();
	ReleaseClassAbilities();
	Super::OnUnregister();
}

void URPGPlayerClassComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	bEndingPlay = true;
	UnbindProgression();
	ReleaseClassAbilities();
	Super::EndPlay(Reason);
}

void URPGPlayerClassComponent::BindProgression()
{
	auto* Progression = GetOwner() ? GetOwner()->FindComponentByClass<URPGPlayerSkillComponent>() : nullptr;
	if (ObservedProgression.Get() == Progression) return;
	UnbindProgression();
	ObservedProgression = Progression;
	if (Progression) Progression->OnSkillDataChanged.AddUniqueDynamic(this, &ThisClass::HandleProgressionChanged);
}

void URPGPlayerClassComponent::UnbindProgression()
{
	if (auto* Progression = ObservedProgression.Get())
		Progression->OnSkillDataChanged.RemoveDynamic(this, &ThisClass::HandleProgressionChanged);
	ObservedProgression.Reset();
}

void URPGPlayerClassComponent::HandleProgressionChanged(FGameplayTag SkillTag)
{
	RefreshAppliedClass(); // No regrant for a new level, SP revision or tripod selection.
}

void URPGPlayerClassComponent::ReserveStartupIdentity(TSubclassOf<URPGGameplayAbility> AbilityClass)
{
	if (GetOwner() && GetOwner()->HasAuthority()) ReservedStartupIdentity = AbilityClass;
}

bool URPGPlayerClassComponent::RoutesAbilityToClass(const UClass* AbilityClass, FGameplayTag InputTag) const
{
	if (!AbilityClass) return false;
	if (AbilityClass->IsChildOf(URPGGameplayAbility_SkillContainer::StaticClass())
		|| AbilityClass->IsChildOf(UActivateIdentity::StaticClass())
		|| InputTag == RPGGameplayTags::InputTag_IdentitySkill
		|| ReservedStartupIdentity.Get() == AbilityClass) return true;
	const auto* Progression = GetOwner() ? GetOwner()->FindComponentByClass<URPGPlayerSkillComponent>() : nullptr;
	const auto* Definition = Progression ? Progression->GetPlayerClassDefinition() : nullptr;
	return Definition && Definition->IdentityAbility.Get() == AbilityClass;
}

bool URPGPlayerClassComponent::IsContextCurrent(const URPGAbilitySystemComponent* ASC,
	const URPGPlayerClassDefinition* Definition) const
{
	const AActor* Owner = GetOwner();
	const auto* Progression = Owner ? Owner->FindComponentByClass<URPGPlayerSkillComponent>() : nullptr;
	return !bEndingPlay && ApplyState != ERPGPlayerClassApplyState::Released && IsRegistered()
		&& IsValid(Owner) && !Owner->IsActorBeingDestroyed() && !IsDungeonGameplayRevoked(Owner)
		&& IsValid(ASC) && ASC->GetOwnerActor() == Owner && ASC->GetAvatarActor() == Owner
		&& Owner->FindComponentByClass<URPGAbilitySystemComponent>() == ASC
		&& IsValid(Definition) && Progression && Progression->GetPlayerClassDefinition() == Definition;
}

void URPGPlayerClassComponent::NotifyAbilitySystemReady(URPGAbilitySystemComponent& ASC, const int32 GrantLevel)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || bEndingPlay
		|| ApplyState == ERPGPlayerClassApplyState::Released || GrantLevel < 1) return;
	if (ReadyASC.IsValid() && (ReadyASC.Get() != &ASC || AbilityGrantLevel != GrantLevel))
	{
		FailApplication(FText::FromString(TEXT("A live Pawn cannot replace its class ASC or grant level.")));
		return;
	}
	ReadyASC = &ASC;
	AbilityGrantLevel = GrantLevel;
	BindProgression();
	RefreshAppliedClass();
	OnApplicationChanged.Broadcast();
}

bool URPGPlayerClassComponent::IsAbilitySystemReady() const
{
	const auto* ASC = ReadyASC.Get();
	const auto* Owner = GetOwner();
	return IsRegistered() && !bEndingPlay && ApplyState != ERPGPlayerClassApplyState::Released
		&& IsValid(Owner) && !Owner->IsActorBeingDestroyed() && !IsDungeonGameplayRevoked(Owner)
		&& ASC && AbilityGrantLevel > 0 && ASC->GetOwnerActor() == Owner && ASC->GetAvatarActor() == Owner;
}

bool URPGPlayerClassComponent::ValidateOwnedGrants(const URPGAbilitySystemComponent& ASC) const
{
	for (const FRPGPlayerClassAbilityGrant& Grant : OwnedGrants)
	{
		const auto* Spec = ASC.FindAbilitySpecFromHandle(Grant.Handle);
		if (!Spec || Spec->PendingRemove || Spec->RemoveAfterActivation || !Spec->Ability
			|| Spec->Ability->GetClass() != Grant.AbilityClass.Get() || Spec->SourceObject.Get() != GetOwner()) return false;
	}
	return true;
}

bool URPGPlayerClassComponent::OwnsAbilitySpec(FGameplayAbilitySpecHandle Handle, const UClass* AbilityClass) const
{
	const auto* ASC = GetOwner() ? GetOwner()->FindComponentByClass<URPGAbilitySystemComponent>() : nullptr;
	const auto* Grant = OwnedGrants.FindByPredicate([Handle, AbilityClass](const FRPGPlayerClassAbilityGrant& Entry)
	{
		return Entry.Handle == Handle && Entry.AbilityClass.Get() == AbilityClass;
	});
	const auto* Spec = Grant && ASC ? ASC->FindAbilitySpecFromHandle(Handle) : nullptr;
	return Spec && Spec->Ability && !Spec->PendingRemove && !Spec->RemoveAfterActivation
		&& Spec->Ability->GetClass() == AbilityClass && Spec->SourceObject.Get() == GetOwner();
}

bool URPGPlayerClassComponent::CanUseAbilitySpec(FGameplayAbilitySpecHandle Handle, const UClass* AbilityClass) const
{
	const auto* Progression = GetOwner() ? GetOwner()->FindComponentByClass<URPGPlayerSkillComponent>() : nullptr;
	const auto* Definition = Progression ? Progression->GetPlayerClassDefinition() : nullptr;
	const auto* ASC = GetOwner() ? GetOwner()->FindComponentByClass<URPGAbilitySystemComponent>() : nullptr;
	return ApplyState == ERPGPlayerClassApplyState::Applied && IsContextCurrent(ASC, Definition)
		&& OwnsAbilitySpec(Handle, AbilityClass);
}

void URPGPlayerClassComponent::RefreshAppliedClass()
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || bApplying || bEndingPlay
		|| ApplyState == ERPGPlayerClassApplyState::Released) return;
	BindProgression();
	auto* Progression = ObservedProgression.Get();
	auto* Definition = Progression ? Progression->GetPlayerClassDefinition() : nullptr;
	auto* ASC = ReadyASC.Get();
	if (bHasAppliedOnce)
	{
		if (ApplyState == ERPGPlayerClassApplyState::Applied && (AppliedDefinition.Get() != Definition
			|| !IsContextCurrent(ASC, Definition) || !ValidateOwnedGrants(*ASC)))
			FailApplication(FText::FromString(TEXT("Applied class grants lost their original Pawn/ASC context.")));
		return; // Never silently recreate a lost grant or reset a running identity.
	}
	if (!ASC || AbilityGrantLevel < 1 || !Definition) return;
	if (!IsContextCurrent(ASC, Definition)) return;
	TGuardValue<bool> Guard(bApplying, true);
	FText Error;
	if (!Definition->ValidateConfiguration(Error)) { FailApplication(Error); return; }

	TArray<FGameplayAbilitySpec> Plan;
	for (const FRPGPlayerClassSkill& Binding : Definition->Skills)
	{
		if (!Progression->IsSkillAcquired(Binding.Definition->SkillTag)) continue;
		FGameplayAbilitySpec& Spec = Plan.Emplace_GetRef(Binding.AbilityClass, AbilityGrantLevel);
		Spec.SourceObject = Owner; // Preserve existing effect-context/source semantics.
	}
	if (Definition->IdentityAbility && Progression->IsClassIdentityUnlocked())
	{
		FGameplayAbilitySpec& Spec = Plan.Emplace_GetRef(Definition->IdentityAbility, AbilityGrantLevel);
		Spec.SourceObject = Owner;
		if (Definition->IdentityInputTag.IsValid()) Spec.GetDynamicSpecSourceTags().AddTag(Definition->IdentityInputTag);
	}
	// A source owner must hand off explicitly. Never confiscate an arbitrary ASC handle by class name.
	for (const FGameplayAbilitySpec& Existing : ASC->GetActivatableAbilities())
	{
		if (!Existing.Ability) continue;
		const auto* Container = Cast<URPGGameplayAbility_SkillContainer>(Existing.Ability);
		const auto* ExistingDefinition = Container ? Container->GetSkillDefinition() : nullptr;
		for (const FGameplayAbilitySpec& Planned : Plan)
		{
			const auto* PlannedContainer = Cast<URPGGameplayAbility_SkillContainer>(Planned.Ability);
			const auto* PlannedDefinition = PlannedContainer ? PlannedContainer->GetSkillDefinition() : nullptr;
			if (Existing.Ability->GetClass() == Planned.Ability->GetClass()
				|| (ExistingDefinition && PlannedDefinition && ExistingDefinition->SkillTag == PlannedDefinition->SkillTag)
				|| Existing.GetDynamicSpecSourceTags().HasAnyExact(Planned.GetDynamicSpecSourceTags()))
			{
				FailApplication(FText::FromString(TEXT("A class grant conflicts with an existing source-owned ability.")));
				return;
			}
		}
	}
	ApplyState = ERPGPlayerClassApplyState::Applying;
	LastApplyError = FText::GetEmpty();
	for (const FGameplayAbilitySpec& Spec : Plan)
	{
		// Record before OnGive/OnAvatar callbacks, which can synchronously tear down the Pawn.
		FRPGPlayerClassAbilityGrant& Grant = OwnedGrants.AddDefaulted_GetRef();
		Grant.Handle = Spec.Handle;
		Grant.AbilityClass = Spec.Ability->GetClass();
		const auto Handle = ASC->GiveAbility(Spec);
		if (Handle != Spec.Handle || ApplyState != ERPGPlayerClassApplyState::Applying
			|| !IsContextCurrent(ASC, Definition) || !ValidateOwnedGrants(*ASC))
		{
			FailApplication(FText::FromString(TEXT("Class grant transaction was interrupted; owned grants were rolled back.")));
			return;
		}
	}
	AppliedDefinition = Definition;
	bHasAppliedOnce = true;
	ApplyState = ERPGPlayerClassApplyState::Applied;
	Owner->ForceNetUpdate();
	OnApplicationChanged.Broadcast();
}

void URPGPlayerClassComponent::FailApplication(const FText& Error)
{
	if (ApplyState == ERPGPlayerClassApplyState::Released) return;
	ApplyState = ERPGPlayerClassApplyState::Failed; // Block reentrant activation before cleanup callbacks.
	LastApplyError = Error;
	RemoveOwnedGrants();
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
		UE_LOG(LogTemp, Warning, TEXT("Class application failed on %s: %s"), *GetNameSafe(Owner), *Error.ToString());
	}
	OnApplicationChanged.Broadcast();
}

void URPGPlayerClassComponent::RemoveOwnedGrants()
{
	const auto Grants = MoveTemp(OwnedGrants);
	OwnedGrants.Reset();
	auto* ASC = ReadyASC.Get();
	if (!ASC || !GetOwner() || !GetOwner()->HasAuthority()) return;
	FScopedAbilityListLock Lock(*ASC);
	for (const FRPGPlayerClassAbilityGrant& Grant : Grants)
	{
		auto* Spec = ASC->FindAbilitySpecFromHandle(Grant.Handle, EConsiderPending::All);
		if (!Spec || !Spec->Ability || Spec->Ability->GetClass() != Grant.AbilityClass.Get()
			|| Spec->SourceObject.Get() != GetOwner()) continue;
		for (UGameplayAbility* Instance : Spec->GetAbilityInstances())
			if (Instance && Instance->IsActive()) Instance->SetCanBeCanceled(true);
		ASC->CancelAbilityHandle(Grant.Handle);
		ASC->ClearAbility(Grant.Handle);
	}
}

void URPGPlayerClassComponent::ReleaseClassAbilities()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	ApplyState = ERPGPlayerClassApplyState::Released;
	RemoveOwnedGrants();
	ReadyASC.Reset();
	AppliedDefinition.Reset();
	GetOwner()->ForceNetUpdate();
	OnApplicationChanged.Broadcast();
}
