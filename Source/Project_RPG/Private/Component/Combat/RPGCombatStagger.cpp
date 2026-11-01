#include "Component/Combat/RPGCombatControlComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Combat/RPGHitReactionProfile.h"
#include "Component/Combat/RPGCombatReactionComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "GameplayEffect/RPGStaggerEffect.h"

namespace
{
	double StaggerNow(const UWorld* World)
	{
		const auto* Clock=World?World->GetGameState():nullptr;
		return Clock?Clock->GetServerWorldTimeSeconds():World?World->GetTimeSeconds():0.;
	}
}
FActiveGameplayEffectHandle URPGCombatControlComponent::ApplyApprovedDamageStagger(AActor* Source, const FHitResult& Hit,
	const FGuid& Receipt, const FRPGStaggerSettings& Settings, const FVector& AwayDirection)
{
	auto* Owner=GetOwner();
	if(bEnding||bApplyingStagger||bClearingStaggers||!IsValid(Owner)||!Owner->HasAuthority()||!IsValid(Source)
		||!Source->HasAuthority()||Source==Owner||Source->GetWorld()!=GetWorld()||Hit.GetActor()!=Owner||!Receipt.IsValid()
		||!Settings.IsEnabled()||!IsLivingAvatar(Source)||!IsLivingAvatar(Owner)||AwayDirection.ContainsNaN())return {};
	auto* ASC=UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Owner);if(!ASC)return {};
	InitializeWithAbilitySystem(*ASC);
	const FRPGApprovedHitContext Lifetime(Source,Owner);
	if(bEnding||BoundASC.Get()!=ASC||!Lifetime.IsCurrent(Source,Owner)||RecentStaggerHits.Contains(Receipt))return {};
	if(RecentStaggerHits.Num()>=64)RecentStaggerHits.RemoveAt(0);RecentStaggerHits.Add(Receipt);
	if(IsImmuneTo(ERPGCombatEffectKind::Stagger)||RPGStagger::HasHigherControl(Owner)
		||HasUncancellableCombatAbility()||OwnedStaggers.Num()>=16)return {};
	TGuardValue<bool> Applying(bApplyingStagger,true);
	auto Context=ASC->MakeEffectContext();Context.AddInstigator(Source,Source);Context.AddSourceObject(this);Context.AddHitResult(Hit);
	FGameplayEffectSpec Spec(GetDefault<URPGStaggerEffect>(),Context,1);Spec.SetDuration(Settings.Duration,true);
	const auto Handle=ASC->ApplyGameplayEffectSpecToSelf(Spec);const auto* Active=ASC->GetActiveGameplayEffect(Handle);
	if(!Handle.IsValid()||!Active)return {};
	if(bEnding||BoundASC.Get()!=ASC||!Lifetime.IsCurrent(Source,Owner)||Active->bIsInhibited
		||RPGStagger::HasHigherControl(Owner)||HasUncancellableCombatAbility())
	{ASC->RemoveActiveGameplayEffect(Handle);return {};}
	OwnedStaggers.Add(Handle);
	if(auto* Removed=ASC->OnGameplayEffectRemoved_InfoDelegate(Handle))Removed->AddUObject(this,&ThisClass::HandleStaggerRemoved);
	if(auto* Inhibited=ASC->OnGameplayEffectInhibitionChangedDelegate(Handle))Inhibited->AddUObject(this,&ThisClass::HandleStaggerInhibited);
	StaggerState.StartServerTime=GetWorld()->GetTimeSeconds();
	StaggerState.Direction=RPGHitReaction::ResolveDirection(Owner->GetActorForwardVector(),-AwayDirection);
	if(++StaggerSequence==0)++StaggerSequence;StaggerState.ExecutionId=StaggerSequence;
	RefreshControl();return Handle;
}
bool URPGCombatControlComponent::RemoveOwnedStagger(FActiveGameplayEffectHandle Handle)
{
	return GetOwner()&&GetOwner()->HasAuthority()&&BoundASC.IsValid()&&OwnedStaggers.Contains(Handle)&&BoundASC->RemoveActiveGameplayEffect(Handle);
}
void URPGCombatControlComponent::ClearOwnedStaggers()
{
	if(!GetOwner()||!GetOwner()->HasAuthority()||bClearingStaggers)return;
	TGuardValue<bool> Clearing(bClearingStaggers,true);OwnedStaggers.Reset();
	if(BoundASC.IsValid())for(auto Handle:BoundASC->GetActiveEffects(FGameplayEffectQuery()))
	{
		const auto* Active=BoundASC->GetActiveGameplayEffect(Handle);
		if(Active&&Active->Spec.Def==GetDefault<URPGStaggerEffect>()&&Active->Spec.GetContext().GetSourceObject()==this)
			BoundASC->RemoveActiveGameplayEffect(Handle);
	}
}
void URPGCombatControlComponent::HandleStaggerRemoved(const FGameplayEffectRemovalInfo& Info)
{
	if(Info.ActiveEffect)OwnedStaggers.Remove(Info.ActiveEffect->Handle);RefreshControl();
}
void URPGCombatControlComponent::HandleStaggerInhibited(FActiveGameplayEffectHandle,bool){RefreshControl();}
void URPGCombatControlComponent::RefreshStaggerState()
{
	if(!GetOwner()->HasAuthority())return;
	const bool Active=IsLivingAvatar(GetOwner())&&RPGStagger::IsStaggered(GetOwner());double End=0.;
	if(Active&&BoundASC.IsValid())for(auto Handle:BoundASC->GetActiveEffects(FGameplayEffectQuery()))
	{
		const auto* Effect=BoundASC->GetActiveGameplayEffect(Handle);if(!Effect||Effect->bIsInhibited)continue;
		FGameplayTagContainer Tags;Effect->Spec.GetAllGrantedTags(Tags);
		if(Tags.HasTagExact(RPGStagger::StatusTag())&&Effect->GetDuration()>0.f)
			End=FMath::Max(End,double(GetWorld()->GetTimeSeconds()+Effect->GetTimeRemaining(GetWorld()->GetTimeSeconds())));
	}
	if(StaggerState.bActive!=Active||FMath::Abs(StaggerState.EndServerTime-End)>.005
		||StaggerState.RecoveryRevision!=RecoveryRevision()||StaggerPublishedSequence!=StaggerSequence)
	{
		StaggerState.bActive=Active;StaggerState.EndServerTime=End;StaggerState.RecoveryRevision=RecoveryRevision();
		StaggerPublishedSequence=StaggerSequence;++StaggerState.Revision;GetOwner()->ForceNetUpdate();OnStaggerStateChanged.Broadcast(StaggerState);
	}
}
void URPGCombatControlComponent::OnRep_StaggerState()
{
	OnStaggerStateChanged.Broadcast(StaggerState);
	SetComponentTickEnabled(StaggerState.bActive||StunState.bActive||KnockbackState.bActive||AirborneState.IsActive()
		||SlowState.bActive||BurnState.Stacks>0||HasMontageImmunityTracking() || !OwnedDefenseReductions.IsEmpty() || !OwnedDamageTakenIncreases.IsEmpty());UpdateStaggerPose();
}
void URPGCombatControlComponent::StopStaggerPose()
{
	auto* Anim=StaggerAnimInstance.Get();auto* Montage=StaggerMontage.Get();
	const auto* Instance=Anim&&Montage?Anim->GetActiveInstanceForMontage(Montage):nullptr;
	const bool Owned=Instance&&Instance->GetInstanceID()==StaggerMontageInstanceId;
	StaggerAnimInstance.Reset();StaggerMontage.Reset();StaggerMontageInstanceId=INDEX_NONE;
	if(Owned)Anim->Montage_Stop(.05f,Montage);
}
void URPGCombatControlComponent::UpdateStaggerPose()
{
	if(!GetWorld()||GetWorld()->GetNetMode()==NM_DedicatedServer)return;
	if(GetWorld()->GetNetMode()==NM_Client&&!GetWorld()->GetGameState()){StopStaggerPose();return;}
	const double Now=StaggerNow(GetWorld());
	if(bEnding||!StaggerState.bActive||!IsLivingAvatar(GetOwner())||RPGStagger::HasHigherControl(GetOwner())
		||StaggerState.RecoveryRevision!=RecoveryRevision()||StaggerState.EndServerTime<=Now)
	{StopStaggerPose();return;}
	auto* Character=Cast<ACharacter>(GetOwner());auto* Receiver=GetOwner()->FindComponentByClass<URPGCombatReactionComponent>();
	const auto* Profile=Receiver?Receiver->PresentationProfile.Get():nullptr;
	auto* Mesh=Character?Character->GetMesh():nullptr;auto* Anim=Mesh?Mesh->GetAnimInstance():nullptr;
	if(StaggerPoseExecution==StaggerState.ExecutionId)
	{
		if(StaggerAnimInstance.Get()!=Anim)StopStaggerPose();
		return;
	}
	StopStaggerPose();
	const auto* ASC=BoundASC.Get();
	if(!Profile||!Anim||!Mesh->GetSkeletalMeshAsset()||Character->IsPlayingRootMotion()
		||Anim->IsAnyMontagePlaying()||(ASC&&ASC->GetAnimatingAbility()))return;
	const auto* Entry=Profile->DirectionMontages.Find(StaggerState.Direction);const auto& Selected=Entry?*Entry:Profile->FallbackMontage;
	if(!URPGCombatReactionComponent::IsSafeCosmeticMontage(Selected.Montage,Selected.Section)
		||Mesh->GetSkeletalMeshAsset()->GetSkeleton()!=Selected.Montage->GetSkeleton())return;
	const int32 Section=Selected.Section.IsNone()?0:Selected.Montage->GetSectionIndex(Selected.Section);float Start=0,End=0;
	Selected.Montage->GetSectionStartAndEndTime(Section,Start,End);
	const float Rate=FMath::Clamp(float((End-Start)/FMath::Max(.05,StaggerState.EndServerTime-StaggerState.StartServerTime)),.1f,4.f);
	const float Offset=float(FMath::Max(0.,Now-StaggerState.StartServerTime))*Rate;
	if(Start+Offset>=End||Anim->Montage_Play(Selected.Montage,Rate,EMontagePlayReturnType::MontageLength,Start+Offset,false)<=0)return;
	Anim->Montage_SetNextSection(Selected.Montage->GetSectionName(Section),NAME_None,Selected.Montage);
	if(const auto* Instance=Anim->GetActiveInstanceForMontage(Selected.Montage))
	{StaggerPoseExecution=StaggerState.ExecutionId;StaggerAnimInstance=Anim;StaggerMontage=Selected.Montage;StaggerMontageInstanceId=Instance->GetInstanceID();}
}
