#include "Tests/RPGBossMapE2EProbeComponent.h"

#include "Ability/RPGNPCGameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Attribute/RPGAttributeSet.h"
#include "BrainComponent.h"
#include "Character/RPGNonPlayerCharacter.h"
#include "Character/RPGPlayer.h"
#include "Component/Combat/PlayerCombatComponent.h"
#include "Component/Combat/RPGCombatReactionComponent.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DataAsset/Input/DataAsset_InputConfig.h"
#include "DataAsset/StartUpData/DataAsset_NPCStartUpData.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Item/Weapon/RPGPlayerWeapon.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RPGGameplayTags.h"
#include "RPGInputTags.h"
#include "UnrealClient.h"

namespace
{
	constexpr const TCHAR* AttackClass = TEXT("/Game/Blueprints/Character/Player/Abilities/DefaultAttack/GA_Player_DefaultAttack.GA_Player_DefaultAttack_C");
	constexpr const TCHAR* RollClass = TEXT("/Game/Blueprints/Character/Player/Abilities/GA_Player_Roll.GA_Player_Roll_C");
	UAbilitySystemComponent* PawnASC(APawn* Pawn)
	{
		auto* Character = Cast<ARPGBaseCharacter>(Pawn);
		return Character ? Character->GetAbilitySystemComponent() : nullptr;
	}
	FGameplayAbilitySpec* FindInputSpec(UAbilitySystemComponent* ASC, const TCHAR* Path)
	{
		return ASC ? ASC->GetActivatableAbilities().FindByPredicate([Path](const FGameplayAbilitySpec& Spec)
		{
			return Spec.Ability && Spec.Ability->GetClass()->GetPathName() == Path;
		}) : nullptr;
	}
	bool IsAttackStep(int32 Step) { return Step == 1 || Step == 2 || Step == 5; }
	bool IsRollStep(int32 Step) { return Step == 7 || Step == 8 || Step == 11; }
	bool StartsInput(int32 Step) { return Step == 1 || Step == 5 || Step == 7 || Step == 11; }
	bool HasBinding(const UEnhancedInputComponent* Input, const UInputAction* Action, ETriggerEvent Event)
	{
		if (!Input || !Action) return false;
		for (const auto& Binding : Input->GetActionEventBindings())
			if (Binding && Binding->GetAction() == Action && Binding->GetTriggerEvent() == Event) return true;
		return false;
	}
}

void URPGBossMapE2EProbeComponent::HandleLightHitInputEnded(const FAbilityEndedData& Data)
{
#if !UE_BUILD_SHIPPING
	if (Data.AbilitySpecHandle == LightHitPlayerAbility) { ++LightInputAttackEnds; bLightInputCancelled |= Data.bWasCancelled; }
	if (Data.AbilitySpecHandle == LightInputRollAbility) { ++LightInputRollEnds; bLightInputCancelled |= Data.bWasCancelled; }
#endif
}

void URPGBossMapE2EProbeComponent::CleanupLightHitInput()
{
	if (LightInputSubsystem.IsValid())
		for (const auto& Action : {LightInputAttackAction, LightInputRollAction, LightInputMoveAction})
			if (Action.IsValid()) LightInputSubsystem->StopContinuousInputInjectionForAction(Action.Get());
	if (LightInputASC.IsValid()) LightInputASC->OnAbilityEnded.Remove(LightInputEndedHandle);
	LightInputEndedHandle.Reset();
}

void URPGBossMapE2EProbeComponent::SendLightHitInputStep(int32 Step)
{
#if !UE_BUILD_SHIPPING
	LightHitStep = Step + 1; // Existing confirmation RPC admits only this current step.
	LightHitNextAt = FPlatformTime::Seconds() + 20.0;
	const uint32 Sequence = LightHitPlayer->FindComponentByClass<URPGCombatReactionComponent>()->ServerSequence;
	for (const auto& PC : ReadyClients)
		PC->FindComponentByClass<URPGBossMapE2EProbeComponent>()->ClientExpectLightHit(LightHitPlayer.Get(), Step, Sequence, NAME_None);
	UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_LIGHT_INPUT_STEP Step=%d Sequence=%u"), Step, Sequence);
#endif
}

bool URPGBossMapE2EProbeComponent::ApplyLightHitInputDamage()
{
#if !UE_BUILD_SHIPPING
	APawn* Victim = LightHitPlayer.Get();
	APawn* Source = LightHitNPC.Get();
	auto* TargetASC = PawnASC(Victim);
	auto* Component = Victim ? Victim->FindComponentByClass<URPGCombatReactionComponent>() : nullptr;
	FGameplayAbilitySpec* Spec = PawnASC(Source) ? PawnASC(Source)->FindAbilitySpecFromHandle(LightHitNPCAbility) : nullptr;
	UGameplayAbility* Ability = Spec ? Spec->GetPrimaryInstance() : nullptr;
	UFunction* Function = Ability ? Ability->FindFunction(TEXT("HandleApplyDamage")) : nullptr;
	if (!Victim || !Source || !TargetASC || !Component || !Function) { Fail(TEXT("LightInputDamageContextLost")); return false; }
	// Only the NPC fixture moves. Player motion still uses normal input/security.
	Source->SetActorLocation(Victim->GetActorLocation() + Victim->GetActorForwardVector() * 180.f);
	const float Before = TargetASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
	const uint32 Sequence = Component->ServerSequence;
	const bool bInvincible = TargetASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Invincible);
	FGameplayTagContainer Tags;
	Tags.AddTag(RPGGameplayTags::Shared_Event_HitReact);
	Tags.AddTag(RPGGameplayTags::Shared_Ability_HitReact);
	int32 LegacyEvents = 0;
	const FDelegateHandle Observer = TargetASC->AddGameplayEventTagContainerDelegate(Tags,
		FGameplayEventTagMulticastDelegate::FDelegate::CreateLambda([&](FGameplayTag, const FGameplayEventData*) { ++LegacyEvents; }));
	FGameplayEventData Payload;
	Payload.EventTag = RPGGameplayTags::Shared_Event_Hit;
	Payload.Instigator = Source;
	Payload.Target = Victim;
	Ability->ProcessEvent(Function, &Payload);
	TargetASC->RemoveGameplayEventTagContainerDelegate(Tags, Observer);
	const float After = TargetASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
	if (LegacyEvents || After <= 0.f || (bInvincible ? After != Before || Component->ServerSequence != Sequence
		: After >= Before || Component->ServerSequence != Sequence + 1))
	{ Fail(TEXT("LightInputDamageApprovalOrLegacyEvent")); return false; }
	UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_LIGHT_INPUT_DAMAGE HP=%g->%g Invincible=%d Sequence=%u LegacyEvents=0 IncomingServerEventFixture=1"),
		Before, After, bInvincible, Component->ServerSequence);
	return true;
#else
	return false;
#endif
}

void URPGBossMapE2EProbeComponent::TickLightHitInputServer()
{
#if !UE_BUILD_SHIPPING
	if (!bLightInputPrepared)
	{
		if (!PreparePlayerAttackLoadouts() || !PrepareDodgeAbilities()) return;
		auto* Player = Cast<ARPGPlayer>(GetWorld()->GetFirstPlayerController()->GetPawn());
		auto* ASC = Player ? Player->GetAbilitySystemComponent() : nullptr;
		auto* Attack = FindInputSpec(ASC, AttackClass);
		auto* Roll = FindInputSpec(ASC, RollClass);
		auto* Security = Player ? Player->FindComponentByClass<URPGSecurityValidationComponent>() : nullptr;
		if (!Attack || !Roll || !Security) return;
		if (!LightHitNPC.IsValid())
		{
			UClass* Class = LoadClass<ARPGNonPlayerCharacter>(nullptr,
				TEXT("/Game/Blueprints/Character/NPC/Gruntling/Guardian/BP_Gruntling_Guardian.BP_Gruntling_Guardian_C"));
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			auto* NPC = Class ? GetWorld()->SpawnActor<ARPGNonPlayerCharacter>(Class,
				Player->GetActorLocation() + Player->GetActorForwardVector() * 180.f, FRotator::ZeroRotator, Params) : nullptr;
			if (!NPC) { Fail(TEXT("LightInputNPCSpawnFailed")); return; }
			LightHitNPC = NPC;
			LightHitPlayer = Player;
		}
		auto* NPC = Cast<ARPGNonPlayerCharacter>(LightHitNPC.Get());
		if (auto* AI = Cast<AAIController>(NPC->GetController()))
			if (AI->GetBrainComponent()) AI->GetBrainComponent()->StopLogic(TEXT("Light-hit input collision fixture"));
		if (!NPC->IsNPCStartUpDataReady()) return;
		NPC->GetCharacterMovement()->StopMovementImmediately();
		auto* Data = NewObject<UDataAsset_NPCStartUpData>(this);
		UClass* NPCClass = LoadClass<URPGNPCGameplayAbility>(nullptr,
			TEXT("/Game/Blueprints/Character/NPC/Gruntling/GameplayAbility/GA_Guardian_Melee1.GA_Guardian_Melee1_C"));
		Data->AttackHitReactionPolicies.Add(NPCClass, ERPGAttackHitReactionPolicy::Cosmetic);
		FGameplayAbilitySpec NPCSpec;
		if (!Data->BuildAttackAbilitySpec(NPCClass, 1, NPC, NPCSpec)) { Fail(TEXT("LightInputNPCGrantFailed")); return; }
		LightHitNPCAbility = PawnASC(NPC)->GiveAbility(NPCSpec);
		// Opted-in runtime fixture on the real equipment grant, without changing a CDO,
		// input binding, weapon source object, attack timing, collision or damage budget.
		RPGHitReaction::SetGrantedPolicy(*Attack, ERPGAttackHitReactionPolicy::Cosmetic);
		ASC->MarkAbilitySpecDirty(*Attack);
		LightHitPlayerAbility = Attack->Handle;
		LightInputRollAbility = Roll->Handle;
		LightInputASC = ASC;
		LightInputViolationBaseline = Security->GetTotalViolationCount();
		LightInputEndedHandle = ASC->OnAbilityEnded.AddUObject(this, &ThisClass::HandleLightHitInputEnded);
		for (APawn* Pawn : {LightHitPlayer.Get(), LightHitNPC.Get()})
		{
			PawnASC(Pawn)->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 10000.f);
			PawnASC(Pawn)->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 10000.f);
			Pawn->ForceNetUpdate();
		}
		for (const auto& PC : ReadyClients)
			PC->FindComponentByClass<URPGBossMapE2EProbeComponent>()->ClientSetLightHitSubjects(Player, NPC);
		bLightInputPrepared = true;
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_LIGHT_INPUT_PREPARED EquipmentGrant=1 IncomingServerEventFixture=1 DurabilityFixture=10000"));
		SendLightHitInputStep(0);
		return;
	}
	auto* Security = LightHitPlayer->FindComponentByClass<URPGSecurityValidationComponent>();
	if (bLightInputCancelled || !Security || Security->GetTotalViolationCount() != LightInputViolationBaseline)
	{ Fail(TEXT("LightInputCancelledOrMovementViolation")); return; }
	const int32 Current = LightHitStep - 1;
	if (FPlatformTime::Seconds() > LightHitNextAt)
	{
		UE_LOG(LogTemp, Error, TEXT("RAIDMAP_E2E_LIGHT_INPUT_TIMEOUT Step=%d Confirmations=%d"), Current, LightHitConfirmations.FindOrAdd(Current).Num());
		Fail(TEXT("LightInputStepTimeout")); return;
	}
	if (LightHitConfirmations.FindOrAdd(Current).Num() != 2) return;
	if ((Current == 3 && LightInputAttackEnds != 1) || (Current == 6 && LightInputAttackEnds != 2)
		|| (Current == 9 && LightInputRollEnds != 1) || (Current == 12 && LightInputRollEnds != 2)) return;
	if ((Current == 9 || Current == 12) && Security->IsMovementAuthorizationActive()) return;
	if (Current == 12)
	{
		if (PawnASC(LightHitNPC.Get())->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) >= 10000.f)
		{ Fail(TEXT("LightInputAttackProducedNoRealDamage")); return; }
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_LIGHT_INPUT_COMPLETE EnhancedInput=1 AttackNaturalEnds=2 RollNaturalEnds=2 Clients=2 SecurityViolations=0 OutgoingDamage=1"));
		CleanupLightHitInput();
		bFinished = true;
		SetComponentTickEnabled(false);
		return;
	}
	const int32 Next = Current + 1;
	if (Next == 8 && !Security->IsMovementAuthorizationActive()) { Fail(TEXT("LightInputRollAuthorizationMissing")); return; }
	if (Next == 2 || Next == 4 || Next == 8 || Next == 10)
		if (!ApplyLightHitInputDamage()) return;
	SendLightHitInputStep(Next);
#endif
}

void URPGBossMapE2EProbeComponent::TickLightHitInputClient()
{
#if !UE_BUILD_SHIPPING
	auto* PC = Cast<APlayerController>(GetOwner());
	auto* Player = Cast<ARPGPlayer>(LightHitPlayer.Get());
	auto* ASC = Player ? Player->GetAbilitySystemComponent() : nullptr;
	auto* Component = Player ? Player->FindComponentByClass<URPGCombatReactionComponent>() : nullptr;
	UAnimInstance* Anim = Player ? Player->GetMesh()->GetAnimInstance() : nullptr;
	if (!PC || !Player || !ASC || !Anim || !Component || !LightHitNPC.IsValid()) return;
	const bool bOwner = PC->GetPawn() == Player;
	const int32 Step = LightHitExpectedCheckpoint;
	const double Now = FPlatformTime::Seconds();
	if (bOwner && !bLightInputBound)
	{
		auto* Attack = FindInputSpec(ASC, AttackClass);
		auto* Roll = FindInputSpec(ASC, RollClass);
		auto* Weapon = Player->GetPlayerCombatComponent()->GetPlayerCurrentEquippedWeapon();
		const auto* Config = LoadObject<UDataAsset_InputConfig>(nullptr, TEXT("/Game/Blueprints/Character/Player/Input/DA_InputConfig.DA_InputConfig"));
		auto* Input = Cast<UEnhancedInputComponent>(PC->InputComponent);
		auto* Subsystem = PC->GetLocalPlayer() ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()) : nullptr;
		if (!Attack || !Roll || !Weapon || Attack->SourceObject.Get() != Weapon || !Config || !Input || !Subsystem
			|| !Subsystem->HasMappingContext(Weapon->PlayerWeaponData.WeaponInputMappingContext)) return;
		LightInputAttackAction = Config->FindAbilityInputActionByTag(RPGGameplayTags::InputTag_LightAttack_GreatSword);
		LightInputRollAction = Config->FindAbilityInputActionByTag(RPGGameplayTags::InputTag_Roll);
		LightInputMoveAction = Config->FindNativeInputActionByTag(RPGGameplayTags::InputTag_Move);
		for (const auto& Action : {LightInputAttackAction, LightInputRollAction})
			if (!Action.IsValid() || !HasBinding(Input, Action.Get(), ETriggerEvent::Started)
				|| !HasBinding(Input, Action.Get(), ETriggerEvent::Completed)) { Fail(TEXT("LightInputActionUnbound")); return; }
		if (!LightInputMoveAction.IsValid() || !HasBinding(Input, LightInputMoveAction.Get(), ETriggerEvent::Triggered))
		{ Fail(TEXT("LightInputMovementUnbound")); return; }
		LightHitPlayerAbility = Attack->Handle;
		LightInputRollAbility = Roll->Handle;
		LightInputASC = ASC;
		LightInputSubsystem = Subsystem;
		LightInputEndedHandle = ASC->OnAbilityEnded.AddUObject(this, &ThisClass::HandleLightHitInputEnded);
		bLightInputBound = true;
		FString CaptureDirectory;
		if (FParse::Value(FCommandLine::Get(), TEXT("RPGLightHitCaptureDir="), CaptureDirectory))
		{
			// Keep the captured subject visible when the two fixture players spawn
			// together. This affects only this diagnostic camera, never collisions.
			for (TActorIterator<ARPGPlayer> It(GetWorld()); It; ++It)
				if (*It != Player) PC->HiddenActors.AddUnique(*It);
		}
	}
	if (bLightInputCancelled) { Fail(TEXT("LightInputLocalAbilityCancelled")); return; }
	// Input release must keep ticking after this step has already been acknowledged.
	if (bOwner && bLightInputSent && Now - LightInputStartedAt > (IsRollStep(LightInputStartedStep) ? .4 : .25))
	{
		LightInputSubsystem->StopContinuousInputInjectionForAction(LightInputAttackAction.Get());
		LightInputSubsystem->StopContinuousInputInjectionForAction(LightInputRollAction.Get());
		LightInputSubsystem->StopContinuousInputInjectionForAction(LightInputMoveAction.Get());
	}
	if (Step == LightHitLastConfirmed) return;
	if (StartsInput(Step) && LightInputStartedStep != Step)
	{
		LightInputStartedStep = Step;
		LightInputMontageId = INDEX_NONE;
		LightInputStartedAt = Now;
		bLightInputSent = false;
		LightInputRollOrigin = Player->GetActorLocation();
		if (bOwner)
		{
			if ((Step == 5 || Step == 11) && (!Component->PresentationMontage.IsValid()
				|| !Anim->Montage_IsPlaying(Component->PresentationMontage.Get())))
			{ Fail(TEXT("LightInputPreemptionWindowMissed")); return; }
			FVector Toward = (LightHitNPC->GetActorLocation() - Player->GetActorLocation()).GetSafeNormal2D();
			if (IsRollStep(Step)) Toward *= -1.f;
			const FRotationMatrix Basis(FRotator(0.f, PC->GetControlRotation().Yaw, 0.f));
			const FVector2D Move(FVector::DotProduct(Toward, Basis.GetUnitAxis(EAxis::Y)), FVector::DotProduct(Toward, Basis.GetUnitAxis(EAxis::X)));
			LightInputSubsystem->StartContinuousInputInjectionForAction(LightInputMoveAction.Get(), FInputActionValue(Move), {}, {});
		}
	}
	if (bOwner && StartsInput(Step) && !bLightInputSent && Now - LightInputStartedAt >= ((Step == 5 || Step == 11) ? .035 : .1))
	{
		if ((Step == 5 || Step == 11) && (!Component->PresentationMontage.IsValid()
			|| !Anim->Montage_IsPlaying(Component->PresentationMontage.Get())))
		{ Fail(TEXT("LightInputPreemptionEndedBeforeInput")); return; }
		if (!IsRollStep(Step)) LightInputSubsystem->StopContinuousInputInjectionForAction(LightInputMoveAction.Get());
		LightInputSubsystem->StartContinuousInputInjectionForAction(IsRollStep(Step) ? LightInputRollAction.Get() : LightInputAttackAction.Get(), FInputActionValue(true), {}, {});
		bLightInputSent = true;
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_LIGHT_INPUT_INJECT Step=%d EnhancedInput=1 ManualActivation=0"), Step);
	}
	if (bOwner && StartsInput(Step) && bLightInputSent && Now - LightInputStartedAt > .6 && Now - LightInputStartedAt < .64)
	{
		const auto* Spec = FindInputSpec(ASC, IsRollStep(Step) ? RollClass : AttackClass);
		FGameplayTagContainer Failures, OwnedTags;
		ASC->GetOwnedGameplayTags(OwnedTags);
		const bool bCanActivate = Spec && Spec->Ability->CanActivateAbility(Spec->Handle, ASC->AbilityActorInfo.Get(), nullptr, nullptr, &Failures);
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_LIGHT_INPUT_DIAG Step=%d Active=%d CanActivate=%d Failures=%s Tags=%s"),
			Step, Spec && Spec->IsActive(), bCanActivate, *Failures.ToStringSimple(), *OwnedTags.ToStringSimple());
	}
	if (Component->bHasReceivedSequence ? Component->LastReceivedSequence != LightHitExpectedSequence : LightHitExpectedSequence != 0) return;
	UAnimMontage* Montage = ASC->GetCurrentMontage();
	if (IsAttackStep(Step) || IsRollStep(Step))
	{
		const TCHAR* Expected = IsRollStep(Step) ? TEXT("AM_Roll") : TEXT("AM_GreatSword_DefaultAttack");
		const auto* Instance = Montage ? Anim->GetActiveInstanceForMontage(Montage) : nullptr;
		if (!Montage || Montage->GetName() != Expected || !Instance || !Instance->IsPlaying()) return;
		if (Step == 2 || Step == 8)
		{
			if (Instance->GetInstanceID() != LightInputMontageId || Component->PresentationMontage.IsValid())
			{ Fail(TEXT("LightInputHitReplacedActiveMontage")); return; }
		}
		else LightInputMontageId = Instance->GetInstanceID();
		if ((Step == 5 || Step == 11) && Component->PresentationMontage.IsValid()) return;
	}
	else if (Step == 4 || Step == 10)
	{
		if (!Component->PresentationMontage.IsValid() || !Anim->Montage_IsPlaying(Component->PresentationMontage.Get())) return;
	}
	else
	{
		if (Anim->IsAnyMontagePlaying() || Component->PresentationMontage.IsValid()) return;
		if (bOwner && ((Step == 3 && LightInputAttackEnds != 1) || (Step == 6 && LightInputAttackEnds != 2)
			|| (Step == 9 && LightInputRollEnds != 1) || (Step == 12 && LightInputRollEnds != 2))) return;
		if ((Step == 9 || Step == 12) && FVector::Dist2D(Player->GetActorLocation(), LightInputRollOrigin) < 100.f) return;
	}
	if (bOwner && (Step == 4 || Step == 5 || Step == 11))
	{
		FString Directory;
		if (FParse::Value(FCommandLine::Get(), TEXT("RPGLightHitCaptureDir="), Directory))
		{
			UAnimMontage* Shot = Step == 4 ? Component->PresentationMontage.Get() : Montage;
			const FName Section = Shot ? Anim->Montage_GetCurrentSection(Shot) : NAME_None;
			const int32 SectionIndex = Shot ? Shot->GetSectionIndex(Section) : INDEX_NONE;
			float Start = 0.f, End = 0.f;
			if (!Shot || SectionIndex == INDEX_NONE) return;
			Shot->GetSectionStartAndEndTime(SectionIndex, Start, End);
			if (Anim->Montage_GetPosition(Shot) - Start < .08f) return;
			IFileManager::Get().MakeDirectory(*Directory, true);
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(Directory, FString::Printf(TEXT("Step-%02d.png"), Step)), false, false);
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_LIGHT_INPUT_CAPTURE Step=%d Target=%s Montage=%s Section=%s Position=%g ObserverPawnHidden=1"),
				Step, *Player->GetName(), *Shot->GetName(), *Section.ToString(), Anim->Montage_GetPosition(Shot));
		}
	}
	UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_LIGHT_INPUT_CLIENT Step=%d Owner=%d Sequence=%u MontageID=%d AttackEnds=%d RollEnds=%d"),
		Step, bOwner, LightHitExpectedSequence, LightInputMontageId, LightInputAttackEnds, LightInputRollEnds);
	LightHitLastConfirmed = Step;
	ServerConfirmLightHit(Step);
#endif
}
