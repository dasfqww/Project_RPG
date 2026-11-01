#include "Component/Combat/RPGCombatReactionComponent.h"
#include "Combat/RPGHitReactionProfile.h"
#include "Combat/RPGCombatControlTypes.h"
#include "Combat/RPGCombatImmunityTypes.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGBaseCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"
#include "RPGGameplayTags.h"
#include "Security/RPGDungeonGameplayAuthority.h"

URPGCombatReactionComponent::URPGCombatReactionComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

bool URPGCombatReactionComponent::IsLivingReceiver() const
{
	AActor* Owner = GetOwner();
	const auto* ASC = IsValid(Owner)
		? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Owner) : nullptr;
	const auto* Attributes = ASC ? ASC->GetSet<URPGAttributeSet>() : nullptr;
	return IsValid(Owner) && !Owner->IsActorBeingDestroyed() && Attributes && ASC->GetAvatarActor() == Owner
		&& FMath::IsFinite(Attributes->GetCurrentHealth()) && Attributes->GetCurrentHealth() > 0.f
		&& !ASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death)
		&& !IsDungeonGameplayRevoked(Owner);
}

uint32 URPGCombatReactionComponent::GetRecoveryRevision() const
{
	const auto* Character = Cast<ARPGBaseCharacter>(GetOwner());
	return Character ? Character->GetCombatPresentationRevision() : 0;
}

bool URPGCombatReactionComponent::ReadServerTime(double& OutTime) const
{
	const UWorld* World = GetWorld();
	if (!World || World->bIsTearingDown) return false;
	const AGameStateBase* State = World->GetGameState();
	if (World->GetNetMode() == NM_Client && !State) return false;
	OutTime = State ? State->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
	return FMath::IsFinite(OutTime);
}

bool URPGCombatReactionComponent::IsSafeCosmeticMontage(const UAnimMontage* Montage, FName Section)
{
	if (!IsValid(Montage) || !Montage->GetSkeleton() || Montage->HasRootMotion()
		|| !Montage->Notifies.IsEmpty() || Montage->SlotAnimTracks.IsEmpty()
		|| !FMath::IsFinite(Montage->GetPlayLength()) || Montage->GetPlayLength() <= 0.f
		|| Montage->GetPlayLength() > 10.f || Montage->GetNumSections() == 0
		|| (!Section.IsNone() && Montage->GetSectionIndex(Section) == INDEX_NONE)) return false;
	for (const FSlotAnimationTrack& Track : Montage->SlotAnimTracks)
	{
		if (Track.AnimTrack.AnimSegments.IsEmpty()) return false;
		for (const FAnimSegment& Segment : Track.AnimTrack.AnimSegments)
		{
			// Do not accept composites with unaudited nested notifies or root motion.
			const auto* Sequence = Cast<UAnimSequence>(Segment.GetAnimReference());
			if (!Sequence || Sequence->HasRootMotion() || !Sequence->Notifies.IsEmpty()) return false;
		}
	}
	return true;
}

ERPGHitReactionRoute URPGCombatReactionComponent::PresentApprovedDamageHit(
	AActor* Source, const FHitResult& ServerHit, const FGuid& ApprovedHitId)
{
	if (!PresentationProfile) return ERPGHitReactionRoute::Unsupported;
	AActor* Owner = GetOwner();
	const auto* SourceASC = IsValid(Source)
		? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Source) : nullptr;
	const auto* SourceAttributes = SourceASC ? SourceASC->GetSet<URPGAttributeSet>() : nullptr;
	if (!IsValid(Owner) || !Owner->HasAuthority() || !IsLivingReceiver()
		|| !IsValid(Source) || !Source->HasAuthority() || Source == Owner
		|| Source->GetWorld() != GetWorld() || Source->IsActorBeingDestroyed()
		|| !SourceAttributes || SourceASC->GetAvatarActor() != Source
		|| !FMath::IsFinite(SourceAttributes->GetCurrentHealth()) || SourceAttributes->GetCurrentHealth() <= 0.f
		|| SourceASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death)
		|| IsDungeonGameplayRevoked(Source) || ServerHit.GetActor() != Owner || !ApprovedHitId.IsValid())
		return ERPGHitReactionRoute::Suppressed;
	if (RecentApprovedHits.Contains(ApprovedHitId)) return ERPGHitReactionRoute::Duplicate;
	// Consume before any presentation callbacks. This bounds native redelivery state.
	if (RecentApprovedHits.Num() >= 64) RecentApprovedHits.RemoveAt(0);
	RecentApprovedHits.Add(ApprovedHitId);
	if (RPGCombatImmunity::IsImmune(Owner, ERPGCombatEffectKind::HitReaction)) return ERPGHitReactionRoute::Suppressed;
	const ERPGHitReactionDirection Direction = RPGHitReaction::ResolveDirection(
		Owner->GetActorForwardVector(), Source->GetActorLocation() - Owner->GetActorLocation());
	const URPGHitReactionProfile* Profile = PresentationProfile;
	if (!IsValid(Profile)) return ERPGHitReactionRoute::Suppressed;
	const FRPGHitReactionMontage* Configured = Profile->DirectionMontages.Find(Direction);
	const FRPGHitReactionMontage& Selected = Configured ? *Configured : Profile->FallbackMontage;
	if (!IsSafeCosmeticMontage(Selected.Montage, Selected.Section))
		return ERPGHitReactionRoute::Suppressed;
	double ServerNow = 0.0;
	const float MaximumPresentationAge = Profile->MaximumPresentationAge;
	if (!ReadServerTime(ServerNow) || !FMath::IsFinite(MaximumPresentationAge)
		|| MaximumPresentationAge < 0.05f || MaximumPresentationAge > 1.f)
		return ERPGHitReactionRoute::Suppressed;
	MulticastPresentHit(++ServerSequence, Direction, Selected.Montage, Selected.Section,
		ServerNow + MaximumPresentationAge, GetRecoveryRevision());
	return ERPGHitReactionRoute::Dispatched;
}

void URPGCombatReactionComponent::MulticastPresentHit_Implementation(uint32 Sequence,
	ERPGHitReactionDirection Direction, UAnimMontage* Montage, FName Section, double ExpiresAtServerTime,
	uint32 RecoveryRevision)
{
	if (bHasReceivedSequence && !RPGHitReaction::IsNewerSequence(Sequence, LastReceivedSequence)) return;
	bHasReceivedSequence = true;
	LastReceivedSequence = Sequence; // Never replay a skipped event after a mesh/ASC becomes ready.
	double ServerNow = 0.0;
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer || !IsLivingReceiver()
		|| RPGCombatControl::IsActionBlocked(GetOwner()) || RecoveryRevision != GetRecoveryRevision() || !ReadServerTime(ServerNow)
		|| !FMath::IsFinite(ExpiresAtServerTime) || ServerNow >= ExpiresAtServerTime
		|| ExpiresAtServerTime - ServerNow > 1.0 || !IsSafeCosmeticMontage(Montage, Section)) return;
	const auto* Character = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	const auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner());
	// Full-body cosmetic playback must never replace an attack/roll or its root motion.
	if (!Anim || !Mesh->GetSkeletalMeshAsset() || Character->IsPlayingRootMotion()
		|| Mesh->GetSkeletalMeshAsset()->GetSkeleton() != Montage->GetSkeleton()
		|| Anim->IsAnyMontagePlaying() || (ASC && ASC->GetAnimatingAbility())) return;
	const int32 SectionIndex = Section.IsNone() ? 0 : Montage->GetSectionIndex(Section);
	float StartTime = 0.f, EndTime = 0.f;
	Montage->GetSectionStartAndEndTime(SectionIndex, StartTime, EndTime);
	if (Anim->Montage_Play(Montage, 1.f, EMontagePlayReturnType::MontageLength, StartTime, false) <= 0.f) return;
	Anim->Montage_SetNextSection(Montage->GetSectionName(SectionIndex), NAME_None, Montage);
	if (const FAnimMontageInstance* Instance = Anim->GetActiveInstanceForMontage(Montage))
	{
		PresentationAnimInstance = Anim;
		PresentationMontage = Montage;
		PresentationInstanceId = Instance->GetInstanceID();
		PresentationRecoveryRevision = RecoveryRevision;
		SetComponentTickEnabled(true);
	}
	(void)Direction; // Frozen server direction selected the montage/section; clients do not recompute it.
}

void URPGCombatReactionComponent::StopOwnedPresentation()
{
	UAnimInstance* Anim = PresentationAnimInstance.Get();
	UAnimMontage* Montage = PresentationMontage.Get();
	const FAnimMontageInstance* Instance = Anim && Montage ? Anim->GetActiveInstanceForMontage(Montage) : nullptr;
	const bool bOwnsInstance = Instance && Instance->GetInstanceID() == PresentationInstanceId;
	PresentationAnimInstance.Reset();
	PresentationMontage.Reset();
	PresentationInstanceId = INDEX_NONE;
	SetComponentTickEnabled(false);
	if (bOwnsInstance) Anim->Montage_Stop(0.05f, Montage);
}

void URPGCombatReactionComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UAnimInstance* Anim = PresentationAnimInstance.Get();
	const FAnimMontageInstance* Instance = Anim && PresentationMontage.IsValid()
		? Anim->GetActiveInstanceForMontage(PresentationMontage.Get()) : nullptr;
	if (!Instance || Instance->GetInstanceID() != PresentationInstanceId)
	{
		PresentationAnimInstance.Reset();
		PresentationMontage.Reset();
		PresentationInstanceId = INDEX_NONE;
		SetComponentTickEnabled(false);
		return;
	}
	const auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner());
	const auto* Character = Cast<ACharacter>(GetOwner());
	if (!IsLivingReceiver() || RPGCombatControl::IsActionBlocked(GetOwner()) || PresentationRecoveryRevision != GetRecoveryRevision()
		|| !Character || Character->GetMesh()->GetAnimInstance() != Anim
		|| (ASC && ASC->GetAnimatingAbility())) StopOwnedPresentation();
}

void URPGCombatReactionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopOwnedPresentation();
	RecentApprovedHits.Reset();
	Super::EndPlay(EndPlayReason);
}
