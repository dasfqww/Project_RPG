// Development-only observation of an authored attack. No damage/event injection.
#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerState.h"
#include "Character/RPGPlayer.h"
#include "Character/RPGNonPlayerCharacter.h"
#include "Controller/RPGPlayerController.h"
#include "Player/RPGPlayerState.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "Component/RPGCharacterMovementComponent.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include "Component/Combat/PawnCombatComponent.h"
#include "Component/Equipment/RPGPlayerWeaponLoadoutComponent.h"
#include "Item/Weapon/RPGWeaponBase.h"
#include "Combat/RPGAirbornePresentationProfile.h"
#include "Attribute/RPGAttributeSet.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/InterpToMovementComponent.h"
#include "GameFramework/RootMotionSource.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "RPGGameplayTags.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#if !UE_BUILD_SHIPPING
namespace RPGGuardianMelee2Network
{
static constexpr const TCHAR* PlayerClassPath=TEXT("/Game/Blueprints/Character/Player/BP_Player.BP_Player_C");
static constexpr const TCHAR* NPCClassPath=TEXT("/Game/Blueprints/Character/NPC/Gruntling/Guardian/BP_Gruntling_Guardian.BP_Gruntling_Guardian_C");
static constexpr const TCHAR* AbilityPath=TEXT("/Game/Blueprints/Character/NPC/Gruntling/GameplayAbility/GA_Guardian_Melee2.GA_Guardian_Melee2_C");
static constexpr const TCHAR* DefaultAttackPath=TEXT("/Game/Blueprints/Character/Player/Abilities/DefaultAttack/GA_Player_DefaultAttack.GA_Player_DefaultAttack_C");
static constexpr const TCHAR* AttackMontagePath=TEXT("/Game/Blueprints/Character/NPC/Gruntling/Anim/Montage/AM_Guardian_MeleeAttack.AM_Guardian_MeleeAttack");
struct FProbe
{
    FString Directory, Role;
    double Deadline=0., ReadyAt=0., AttackAt=0., CompleteAt=0., DiagnosticAt=0.;
    int32 Step=0, Phases=0, Poses=0;
    float InitialHP=0.f, InitialZ=0.f, MaximumZ=0.f, ObserverHP=0.f;
    TWeakObjectPtr<ARPGPlayer> Target;
    TWeakObjectPtr<ARPGNonPlayerCharacter> NPC;
    TWeakObjectPtr<ARPGPlayer> ObserverPawn;
    TArray<TSharedPtr<FJsonValue>> Samples;
    bool bDone=false, bFirstPlayerLogged=false;
    const bool bMovingPlatform=FParse::Param(FCommandLine::Get(),TEXT("RPGGuardianMelee2MovingPlatform"));
    const bool bGrounded=FParse::Param(FCommandLine::Get(),TEXT("RPGGuardianMelee2Grounded"));
    const FRPGKnockbackSettings GroundedSettings=[]{
        FRPGKnockbackSettings Settings;
        Settings.GroundFallDuration=.65f; Settings.DownDuration=1.2f; Settings.GetUpDuration=.8f;
        return Settings;
    }();
    FVector InitialLocation=FVector::ZeroVector;
    double MaximumGroundedDrift=0.;
    double GroundFallEnd=0.;
    int32 ExpectedDirection=INDEX_NONE;
    bool bDrawRequested=false;
    TMap<int32,FString> ObservedMontages;
    TStrongObjectPtr<UAnimMontage> WarmAttackMontage;
    TWeakObjectPtr<AStaticMeshActor> Platform;
    TWeakObjectPtr<UInterpToMovementComponent> PlatformMover;
    FVector PlatformOrigin=FVector::ZeroVector, RecoveryRelativeOrigin=FVector::ZeroVector;
    int32 GroundedRecoverySamples=0, DownBasedSamples=0, GetUpBasedSamples=0;
    float MaximumRecoveryRelativeDrift=0.f;
    bool bRecoveryRelativeBaseline=false, bRecoveryBaseLost=false;

    AStaticMeshActor* FindPlatform(UWorld* World)
    {
        // Dynamic actor names differ on clients. Entry contains one replicated
        // cube floor with this scale; wait for its mesh/scale replication.
        if (!Platform.IsValid()) for (TActorIterator<AStaticMeshActor> It(World);It;++It)
            if (It->GetActorScale3D().Equals(FVector(40,40,1))
                && It->GetStaticMeshComponent()->GetStaticMesh()
                && It->GetStaticMeshComponent()->GetStaticMesh()->GetPathName()==TEXT("/Engine/BasicShapes/Cube.Cube"))
            { Platform=*It; break; }
        return Platform.Get();
    }

    void Write(const FString& Name,const TSharedRef<FJsonObject>& Object)
    {
        FString Text; auto Writer=TJsonWriterFactory<>::Create(&Text);
        FJsonSerializer::Serialize(Object,Writer);
        FFileHelper::SaveStringToFile(Text,*(Directory/Name),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }
    void Fail(const TCHAR* Reason)
    {
        UE_LOG(LogTemp,Error,TEXT("GUARDIAN_MELEE2_NETWORK_FAIL Role=%s Reason=%s Step=%d Phases=%d Poses=%d"),*Role,Reason,Step,Phases,Poses);
        auto Result=MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("success"),false); Result->SetStringField(TEXT("reason"),Reason);
        Result->SetArrayField(TEXT("samples"),Samples); Result->SetNumberField(TEXT("phases"),Phases); Result->SetNumberField(TEXT("poses"),Poses);
        Result->SetBoolField(TEXT("movingPlatform"),bMovingPlatform);
        Result->SetNumberField(TEXT("maximumRecoveryRelativeDrift"),MaximumRecoveryRelativeDrift);
        Result->SetNumberField(TEXT("downBasedSamples"),DownBasedSamples); Result->SetNumberField(TEXT("getUpBasedSamples"),GetUpBasedSamples);
        Result->SetBoolField(TEXT("recoveryBaseLost"),bRecoveryBaseLost);
        if (Target.IsValid())
        {
            Result->SetNumberField(TEXT("hp"),HP(Target.Get())); Result->SetNumberField(TEXT("z"),Target->GetActorLocation().Z);
            Result->SetNumberField(TEXT("endReason"),static_cast<int32>(Target->GetCombatControlComponent()->GetAirborneState().EndReason));
        }
        Write(Role+TEXT(".result.json"),Result); bDone=true; FPlatformMisc::RequestExitWithStatus(false,1);
    }
    float HP(ARPGPlayer* Player) const { return Player->GetRPGAbilitySystemComponent()->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()); }
    URPGAirbornePresentationProfile* Profile(URPGCombatControlComponent* Control) const
    {
        const auto* Property=FindFProperty<FObjectPropertyBase>(Control->GetClass(),TEXT("AirbornePresentationProfile"));
        return Property?Cast<URPGAirbornePresentationProfile>(Property->GetObjectPropertyValue_InContainer(Control)):nullptr;
    }
    void WarmMontage(UAnimMontage* Montage) const
    {
        if (!Montage) return;
        for (const auto& Track : Montage->SlotAnimTracks)
            for (const auto& Segment : Track.AnimTrack.AnimSegments)
                if (auto* Clip=Cast<UAnimSequence>(Segment.GetAnimReference())) Clip->CacheDerivedDataForCurrentPlatform();
    }
    bool Configure(UWorld* World)
    {
        auto* Mode=World->GetAuthGameMode(); if (!Mode) return false;
        if (World->GetMapName()!=TEXT("Entry") || Mode->GetClass()!=AGameModeBase::StaticClass()) { Fail(TEXT("DedicatedEntryGameModeRequired")); return false; }
        Mode->DefaultPawnClass=LoadClass<APawn>(nullptr,PlayerClassPath);
        Mode->PlayerControllerClass=ARPGPlayerController::StaticClass();
        Mode->PlayerStateClass=ARPGPlayerState::StaticClass();
        if (!Mode->DefaultPawnClass) { Fail(TEXT("OriginalPlayerMissing")); return false; }
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Params.Name=TEXT("GuardianMelee2Floor");
        auto* Floor=World->SpawnActor<AStaticMeshActor>(FVector(0,0,-50),FRotator::ZeroRotator,Params);
        Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
        Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
        Floor->SetActorScale3D(FVector(40,40,1)); Floor->SetReplicates(true); Floor->SetReplicateMovement(true);
        Floor->GetStaticMeshComponent()->SetIsReplicated(true); Floor->bAlwaysRelevant=true;
        if (bMovingPlatform)
        {
            Platform=Floor; Floor->SetNetUpdateFrequency(60.f); Floor->SetMinNetUpdateFrequency(30.f);
            auto* Mover=NewObject<UInterpToMovementComponent>(Floor);
            Floor->AddInstanceComponent(Mover); Mover->Duration=20.f; Mover->BehaviourType=EInterpToBehaviourType::OneShot;
            Mover->bSweep=false; Mover->SetUpdatedComponent(Floor->GetStaticMeshComponent());
            Mover->AddControlPointPosition(FVector::ZeroVector); Mover->AddControlPointPosition(FVector(12000,0,0));
            Mover->RegisterComponent(); Mover->FinaliseControlPoints(); Mover->SetComponentTickEnabled(false);
            PlatformMover=Mover;
        }
        Params.Name=NAME_None;
        for (float X : {0.f,700.f}) World->SpawnActor<APlayerStart>(FVector(X,0,150),FRotator::ZeroRotator,Params);
        UE_LOG(LogTemp,Display,TEXT("GUARDIAN_MELEE2_NETWORK_SERVER_READY OriginalPlayer=1 NaturalWeaponHit=1"));
        return true;
    }
    TArray<ARPGPlayer*> Players(UWorld* World)
    {
        TArray<ARPGPlayer*> Found;
        for (TActorIterator<ARPGPlayer> It(World);It;++It)
            if (It->GetPlayerState() && It->GetMesh()->GetAnimInstance() && HP(*It)>20.f) Found.Add(*It);
        Found.Sort([](const ARPGPlayer& A,const ARPGPlayer& B){return A.GetPlayerState()->GetPlayerId()<B.GetPlayerState()->GetPlayerId();});
        return Found;
    }
    void Observe(ARPGPlayer* Player)
    {
        auto* Control=Player->GetCombatControlComponent(); auto State=Control->GetAirborneState();
        const int32 Phase=static_cast<int32>(State.Phase); Phases|=1<<Phase;
        MaximumZ=FMath::Max(MaximumZ,static_cast<float>(Player->GetActorLocation().Z));
        if (bGrounded)
        {
            if (Phases==1) InitialLocation=Player->GetActorLocation();
            MaximumGroundedDrift=FMath::Max(MaximumGroundedDrift,FVector::Dist(InitialLocation,Player->GetActorLocation()));
            if (State.Phase==ERPGAirbornePhase::GroundFall) GroundFallEnd=State.PhaseEndServerTime;
        }
        auto* Presentation=Profile(Control); auto* Anim=Player->GetMesh()->GetAnimInstance();
        if (Phase>0 && Presentation && Anim)
        {
            const auto& Clip=Presentation->ResolveMontage(State.Phase,State.PresentationDirection);
            auto* Instance=Clip.Montage?Anim->GetActiveInstanceForMontage(Clip.Montage):nullptr;
            if (Instance && !Instance->IsStopped())
            {
                Poses|=1<<Phase;
                ObservedMontages.Add(Phase,Clip.Montage->GetPathName());
            }
        }
        const auto Position=Player->GetActorLocation();
        auto Sample=MakeShared<FJsonObject>(); Sample->SetNumberField(TEXT("serverTime"),WorldTime(Player->GetWorld()));
        Sample->SetNumberField(TEXT("phase"),Phase); Sample->SetNumberField(TEXT("hp"),HP(Player));
        Sample->SetNumberField(TEXT("x"),Position.X); Sample->SetNumberField(TEXT("y"),Position.Y); Sample->SetNumberField(TEXT("z"),Position.Z);
        Sample->SetNumberField(TEXT("poses"),Poses); Samples.Add(MakeShared<FJsonValueObject>(Sample));
        Sample->SetNumberField(TEXT("direction"),static_cast<int32>(State.PresentationDirection));
        auto* Movement=Player->GetCharacterMovement();
        Sample->SetBoolField(TEXT("actionBlocked"),RPGCombatControl::IsActionBlocked(Player));
        Sample->SetBoolField(TEXT("grounded"),Movement->IsMovingOnGround());
        if (ExpectedDirection!=INDEX_NONE && State.IsActive())
        {
            const auto* Mesh=Player->GetMesh();
            const FVector BodyAxis=Mesh->GetSocketLocation(TEXT("head"))-Mesh->GetSocketLocation(TEXT("pelvis"));
            Sample->SetNumberField(TEXT("bodyAxisAlongFlight"),
                FVector::DotProduct(BodyAxis.GetSafeNormal(),FRotator(0.f,State.Yaw,0.f).Vector()));
        }
        Sample->SetNumberField(TEXT("phaseElapsed"),WorldTime(Player->GetWorld())-State.PhaseStartServerTime);
        Sample->SetNumberField(TEXT("arcDuration"),State.ArcDuration);
        Sample->SetNumberField(TEXT("movementStartClientTime"),State.MovementStartClientTime);
        Sample->SetNumberField(TEXT("velocityZ"),Movement->Velocity.Z);
        if (bMovingPlatform)
        {
            auto* Floor=FindPlatform(Player->GetWorld());
            if (Floor)
            {
                if (Role==TEXT("server") && Phase==1 && PlatformMover.IsValid()) PlatformMover->SetComponentTickEnabled(true);
                const FVector Relative=Floor->GetActorTransform().InverseTransformPositionNoScale(Position);
                const bool bBased=Player->GetMovementBaseObject()==Floor->GetStaticMeshComponent() && Movement->IsMovingOnGround();
                Sample->SetNumberField(TEXT("platformX"),Floor->GetActorLocation().X);
                Sample->SetNumberField(TEXT("relativeX"),Relative.X); Sample->SetNumberField(TEXT("relativeY"),Relative.Y);
                Sample->SetBoolField(TEXT("onPlatform"),bBased);
                if (Phase==2 || Phase==3)
                {
                    if (bBased)
                    {
                        ++GroundedRecoverySamples;
                        if (Phase==2) ++DownBasedSamples; else ++GetUpBasedSamples;
                        // Let CMC finish establishing its grounded base before measuring drift.
                        if (GroundedRecoverySamples>=3 && !bRecoveryRelativeBaseline)
                        { RecoveryRelativeOrigin=Relative; bRecoveryRelativeBaseline=true; }
                        if (bRecoveryRelativeBaseline)
                            MaximumRecoveryRelativeDrift=FMath::Max(MaximumRecoveryRelativeDrift,float(FVector::Dist2D(RecoveryRelativeOrigin,Relative)));
                    }
                    else if (bRecoveryRelativeBaseline) bRecoveryBaseLost=true;
                }
            }
        }
        TArray<TSharedPtr<FJsonValue>> Sources;
        for (const auto& Source:Movement->CurrentRootMotion.RootMotionSources)
        {
            if (!Source.IsValid()) continue;
            auto Item=MakeShared<FJsonObject>(); Item->SetStringField(TEXT("name"),Source->InstanceName.ToString());
            Item->SetNumberField(TEXT("id"),Source->LocalID);Item->SetNumberField(TEXT("time"),Source->GetTime());
            Item->SetNumberField(TEXT("startTime"),Source->GetStartTime());
            Sources.Add(MakeShared<FJsonValueObject>(Item));
        }
        Sample->SetArrayField(TEXT("motionSources"),Sources);
    }
    double WorldTime(UWorld* World) const { return World->GetGameState()?World->GetGameState()->GetServerWorldTimeSeconds():World->GetTimeSeconds(); }
    void Complete(ARPGPlayer* Player,UNetDriver* Driver)
    {
        const auto State=Player->GetCombatControlComponent()->GetAirborneState();
        auto* Security=Player->FindComponentByClass<URPGSecurityValidationComponent>();
        const int32 ActiveMask=bGrounded?28:14;
        const bool bInvalidTravel=bGrounded?MaximumGroundedDrift>5.:MaximumZ<InitialZ+50;
        if (Phases!=(ActiveMask|1) || (Role!=TEXT("server") && Poses!=ActiveMask) || bInvalidTravel || HP(Player)>=InitialHP || HP(Player)<=0
            || State.EndReason!=ERPGAirborneEndReason::Completed || FMath::Abs(Player->GetActorLocation().Z-InitialZ)>8.f
            || (Security && (Security->GetTotalViolationCount()!=0 || Security->IsMovementAuthorizationActive())))
        { Fail(TEXT("HitPosePhysicsRecoveryMismatch")); return; }
        if (ObserverPawn.IsValid())
        {
            const auto* ObserverSecurity=ObserverPawn->FindComponentByClass<URPGSecurityValidationComponent>();
            if (HP(ObserverPawn.Get())!=ObserverHP || !ObserverSecurity || ObserverSecurity->GetTotalViolationCount()!=0 || ObserverSecurity->IsMovementAuthorizationActive())
            { Fail(TEXT("ObserverWasAffected")); return; }
        }
        if (bMovingPlatform)
        {
            auto* Floor=FindPlatform(Player->GetWorld());
            if (!Floor || FVector::Dist2D(PlatformOrigin,Floor->GetActorLocation())<500.f || !bRecoveryRelativeBaseline
                || DownBasedSamples==0 || GetUpBasedSamples==0 || bRecoveryBaseLost || MaximumRecoveryRelativeDrift>50.f
                || Player->GetMovementBaseObject()!=Floor->GetStaticMeshComponent() || !Player->GetCharacterMovement()->IsMovingOnGround())
            { Fail(TEXT("MovingPlatformRecoveryMismatch")); return; }
        }
        auto Result=MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("success"),true); Result->SetStringField(TEXT("role"),Role);
        if (ExpectedDirection != INDEX_NONE && RPGCombatControl::IsActionBlocked(Player))
        { Fail(TEXT("ActionStillBlockedAfterRecovery")); return; }
        auto Montages=MakeShared<FJsonObject>();
        for (const auto& Entry : ObservedMontages) Montages->SetStringField(FString::FromInt(Entry.Key),Entry.Value);
        Result->SetObjectField(TEXT("montages"),Montages);
        Result->SetNumberField(TEXT("phases"),Phases); Result->SetNumberField(TEXT("poses"),Poses);
        Result->SetNumberField(TEXT("initialHP"),InitialHP); Result->SetNumberField(TEXT("finalHP"),HP(Player));
        Result->SetNumberField(TEXT("rise"),MaximumZ-InitialZ); Result->SetNumberField(TEXT("initialZ"),InitialZ);
        Result->SetNumberField(TEXT("finalX"),Player->GetActorLocation().X); Result->SetNumberField(TEXT("finalY"),Player->GetActorLocation().Y);
        Result->SetNumberField(TEXT("finalZ"),Player->GetActorLocation().Z);
        Result->SetBoolField(TEXT("locallyControlledReceiver"),Player->IsLocallyControlled());
        Result->SetNumberField(TEXT("lagMin"),Driver->PacketSimulationSettings.PktLagMin);
        Result->SetNumberField(TEXT("lagMax"),Driver->PacketSimulationSettings.PktLagMax);
        Result->SetNumberField(TEXT("loss"),Driver->PacketSimulationSettings.PktLoss);
        Result->SetBoolField(TEXT("iris"),Driver->IsUsingIrisReplication());
        Result->SetStringField(TEXT("playerClass"),Player->GetClass()->GetPathName());
        Result->SetBoolField(TEXT("syntheticDamage"),false); Result->SetBoolField(TEXT("replacementGrant"),false);
        Result->SetNumberField(TEXT("securityViolations"),Security?Security->GetTotalViolationCount():-1);
        Result->SetBoolField(TEXT("movementAuthorizationReleased"),Security && !Security->IsMovementAuthorizationActive());
        Result->SetBoolField(TEXT("movingPlatform"),bMovingPlatform);
        Result->SetBoolField(TEXT("grounded"),bGrounded);
        Result->SetNumberField(TEXT("maximumGroundedDrift"),MaximumGroundedDrift);
        if (bMovingPlatform)
        {
            auto* Floor=FindPlatform(Player->GetWorld()); const FVector Relative=Floor->GetActorTransform().InverseTransformPositionNoScale(Player->GetActorLocation());
            Result->SetNumberField(TEXT("platformTravel"),FVector::Dist2D(PlatformOrigin,Floor->GetActorLocation()));
            Result->SetNumberField(TEXT("finalRelativeX"),Relative.X); Result->SetNumberField(TEXT("finalRelativeY"),Relative.Y);
            Result->SetNumberField(TEXT("maximumRecoveryRelativeDrift"),MaximumRecoveryRelativeDrift);
            Result->SetNumberField(TEXT("downBasedSamples"),DownBasedSamples); Result->SetNumberField(TEXT("getUpBasedSamples"),GetUpBasedSamples);
            Result->SetBoolField(TEXT("recoveryBaseLost"),bRecoveryBaseLost);
        }
        if (ObserverPawn.IsValid())
        {
            Result->SetNumberField(TEXT("observerInitialHP"),ObserverHP); Result->SetNumberField(TEXT("observerFinalHP"),HP(ObserverPawn.Get()));
            Result->SetBoolField(TEXT("observerUnaffected"),true);
        }
        Result->SetArrayField(TEXT("samples"),Samples); Write(Role+TEXT(".result.json"),Result);
        UE_LOG(LogTemp,Display,TEXT("GUARDIAN_MELEE2_NETWORK_COMPLETE Role=%s HP=%g->%g Rise=%g Phases=%d Poses=%d"),*Role,InitialHP,HP(Player),MaximumZ-InitialZ,Phases,Poses);
        bDone=true; // Runner waits for all three observations before graceful exit.
    }
    bool Tick(float)
    {
        if (bDone)
        {
            if (IFileManager::Get().FileExists(*(Directory/TEXT("exit")))) { FPlatformMisc::RequestExitWithStatus(false,0); return false; }
            return true;
        }
        UWorld* World=nullptr;
        if (GEngine) for (const auto& Context:GEngine->GetWorldContexts())
            if (Context.WorldType==EWorldType::Game && Context.World() && Context.World()->GetNetMode()!=NM_Standalone) { World=Context.World(); break; }
        if (!World) return true;
        const bool Server=World->GetNetMode()==NM_DedicatedServer;
        if (Role.IsEmpty())
        {
            FParse::Value(FCommandLine::Get(),TEXT("RPGGuardianMelee2Directory="),Directory);
            FParse::Value(FCommandLine::Get(),TEXT("RPGGuardianMelee2Direction="),ExpectedDirection);
            Deadline=FPlatformTime::Seconds()+(ExpectedDirection==INDEX_NONE?180.:600.);
            Role=Server?TEXT("server"):FParse::Param(FCommandLine::Get(),TEXT("RPGGuardianMelee2Owner"))?TEXT("owner"):TEXT("observer");
            if (Directory.IsEmpty()) { Fail(TEXT("OutputDirectoryMissing")); return false; }
            if (bGrounded && (ExpectedDirection<0 || ExpectedDirection>3 || bMovingPlatform))
            { Fail(TEXT("GroundedFixtureRequiresStationaryDirectionalCase")); return false; }
        }
        if (FPlatformTime::Seconds()>Deadline) { Fail(TEXT("Deadline")); return false; }
        auto* Driver=World->GetNetDriver(); if (!Driver || !Driver->IsUsingIrisReplication()) return true;
        if (Server && Step==0) { if (Configure(World)) Step=1; return true; }
        auto Found=Players(World);
        if (FPlatformTime::Seconds()>DiagnosticAt)
        {
            DiagnosticAt=FPlatformTime::Seconds()+5.;
            FString Details;
            for (TActorIterator<ARPGPlayer> It(World);It;++It)
                Details+=FString::Printf(TEXT(" [%s State=%d HP=%g NativeHP=%g Profile=%d Montage=%d]"),*It->GetName(),It->GetPlayerState()?It->GetPlayerState()->GetPlayerId():-1,
                    HP(*It),It->GetRPGAttributeSet()->GetCurrentHealth(),Profile(It->GetCombatControlComponent())!=nullptr,
                    It->GetMesh()->GetAnimInstance()?It->GetMesh()->GetAnimInstance()->IsAnyMontagePlaying():-1);
            for (TActorIterator<ARPGNonPlayerCharacter> It(World);It;++It) Details+=TEXT(" NPC=")+It->GetName();
            if (Server && NPC.IsValid() && ExpectedDirection!=INDEX_NONE)
            {
                const auto* Combat=Found.Num()>0?Found[0]->GetPawnCombatComponent():nullptr;
                Details+=FString::Printf(TEXT(" NPCReady=%d NPCWeapon=%d NPCMontage=%d GreatSword=%d DrawRequested=%d"),
                    NPC->IsNPCStartUpDataReady(), NPC->GetPawnCombatComponent()->GetCharacterCurrentEquippedWeapon()!=nullptr,
                    NPC->GetMesh()->GetAnimInstance()->IsAnyMontagePlaying(),
                    Combat && Combat->GetCharacterHasWeaponByTag(RPGGameplayTags::Player_Weapon_GreatSword), bDrawRequested);
            }
            UE_LOG(LogTemp,Display,TEXT("GUARDIAN_MELEE2_NETWORK_WAIT Role=%s Step=%d Players=%d%s"),*Role,Step,Found.Num(),*Details);
        }
        if (Server && Found.Num()==1 && !bFirstPlayerLogged)
        { UE_LOG(LogTemp,Display,TEXT("GUARDIAN_MELEE2_NETWORK_FIRST_PLAYER_READY")); bFirstPlayerLogged=true; }
        if (Found.Num()!=2) return true;
        auto* Player=Found[0]; auto* Control=Player->GetCombatControlComponent();
        Player->GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        if (!Target.IsValid())
        {
            if (bMovingPlatform && !FindPlatform(World)) return true;
            Target=Player; InitialHP=HP(Player); InitialZ=MaximumZ=Player->GetActorLocation().Z;
            if (bMovingPlatform) PlatformOrigin=Platform->GetActorLocation();
            if (Server)
            {
                if (bMovingPlatform) for (auto* Pawn:Found) Pawn->GetCharacterMovement()->AddTickPrerequisiteComponent(PlatformMover.Get());
                // Separate the observer before spawning an attacking NPC. The
                // stock start selection can place both players at nearby starts.
                ObserverPawn=Found[1]; ObserverHP=HP(Found[1]);
                auto* ObserverSecurity=Found[1]->FindComponentByClass<URPGSecurityValidationComponent>();
                if (!ObserverSecurity || !ObserverSecurity->BeginMovementAuthorization(2.f,1600.f,TEXT("GuardianMelee2.ObserverSetup")).IsValid())
                { Fail(TEXT("ObserverSetupAuthorization")); return false; }
                Found[1]->SetActorLocation(Player->GetActorLocation()+FVector(0,1200,0),false,nullptr,ETeleportType::TeleportPhysics);
                Found[1]->GetCharacterMovement()->StopMovementImmediately();
                FActorSpawnParameters Params; Params.Name=TEXT("GuardianMelee2Attacker"); Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                const float SpawnOffset=ExpectedDirection==INDEX_NONE?-160.f:-650.f;
                NPC=World->SpawnActor<ARPGNonPlayerCharacter>(LoadClass<ARPGNonPlayerCharacter>(nullptr,NPCClassPath),Player->GetActorLocation()+FVector(SpawnOffset,0,6),FRotator::ZeroRotator,Params);
                if (!NPC.IsValid()) { Fail(TEXT("GuardianSpawn")); return false; }
                if (bMovingPlatform) NPC->GetCharacterMovement()->AddTickPrerequisiteComponent(PlatformMover.Get());
                NPC->bAlwaysRelevant=true;
            }
        }
        if (Server && NPC.IsValid())
        {
            auto* AI=Cast<AAIController>(NPC->GetController());
            if (AI) { if (AI->GetBrainComponent()) AI->GetBrainComponent()->StopLogic(TEXT("Authored Melee2 network observation")); AI->StopMovement(); }
            NPC->GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        }
        if (!Server && Step==0)
        {
            ARPGNonPlayerCharacter* Attacker=nullptr;
            for (TActorIterator<ARPGNonPlayerCharacter> It(World);It;++It) if (It->GetClass()->GetPathName()==NPCClassPath) Attacker=*It;
            if (!Attacker || !Profile(Control) || Player->GetMesh()->GetAnimInstance()->IsAnyMontagePlaying()) return true;
            if (Player->IsLocallyControlled()!=(Role==TEXT("owner"))) { Fail(TEXT("ReceiverOwnership")); return false; }
            if (!ReadyAt)
            {
                if (ExpectedDirection!=INDEX_NONE)
                {
                    // An uncached replicated attack montage can stall a client during the first hit.
                    WarmAttackMontage.Reset(LoadObject<UAnimMontage>(nullptr,AttackMontagePath));
                    WarmMontage(WarmAttackMontage.Get());
                    const auto Direction=static_cast<ERPGHitReactionDirection>(ExpectedDirection);
                    for (const auto Phase : {bGrounded?ERPGAirbornePhase::GroundFall:ERPGAirbornePhase::Airborne,
                        ERPGAirbornePhase::Downed,ERPGAirbornePhase::GettingUp})
                        WarmMontage(Profile(Control)->ResolveMontage(Phase,Direction).Montage);
                }
                ReadyAt=FPlatformTime::Seconds()+1.;
            }
            if (FPlatformTime::Seconds()<ReadyAt) return true;
            auto Ready=MakeShared<FJsonObject>(); Ready->SetBoolField(TEXT("ready"),true); Write(Role+TEXT(".ready.json"),Ready); Step=1;
            InitialHP=HP(Player); InitialZ=MaximumZ=Player->GetActorLocation().Z;
            InitialLocation=Player->GetActorLocation();
        }
        if (Server && Step==1)
        {
            if (!NPC->IsNPCStartUpDataReady() || !NPC->GetPawnCombatComponent()->GetCharacterCurrentEquippedWeapon()
                || NPC->GetMesh()->GetAnimInstance()->IsAnyMontagePlaying()
                || !IFileManager::Get().FileExists(*(Directory/TEXT("owner.ready.json")))
                || !IFileManager::Get().FileExists(*(Directory/TEXT("observer.ready.json")))) return true;
            if (!ReadyAt) ReadyAt=FPlatformTime::Seconds()+1.;
            if (FPlatformTime::Seconds()<ReadyAt) return true;
            if (ExpectedDirection!=INDEX_NONE)
            {
                auto* Combat=Player->GetPawnCombatComponent();
                auto* Weapon=Combat->GetCharacterHasWeaponByTag(RPGGameplayTags::Player_Weapon_GreatSword);
                if (!Weapon) return true;
                if (!bDrawRequested)
                {
                    if (auto* Loadout=Player->FindComponentByClass<URPGPlayerWeaponLoadoutComponent>()) Loadout->RefreshLoadout();
                    bDrawRequested=true;
                    if (!Player->GetRPGAbilitySystemComponent()->TryActivateAbilityByClass(LoadClass<UGameplayAbility>(nullptr,DefaultAttackPath),true))
                    { Fail(TEXT("GreatSwordDefaultAttackActivation")); return false; }
                    UE_LOG(LogTemp, Display, TEXT("GUARDIAN_MELEE2_NETWORK_GREAT_SWORD_DRAW_REQUESTED"));
                    return true;
                }
                if (Combat->GetCharacterCurrentEquippedWeapon()!=Weapon || Weapon->GetAttachParentSocketName()!=TEXT("WeaponHandSocket")
                    || Player->GetMesh()->GetAnimInstance()->IsAnyMontagePlaying()) return true;
            }
            InitialHP=HP(Player); InitialZ=MaximumZ=Player->GetActorLocation().Z;
            InitialLocation=Player->GetActorLocation();
            const int32 Side=ExpectedDirection==INDEX_NONE?2:ExpectedDirection;
            const FVector TowardAttacker=Player->GetActorForwardVector().RotateAngleAxis(90.f*Side,FVector::UpVector);
            NPC->SetActorLocation(Player->GetActorLocation()+160.f*TowardAttacker+FVector(0,0,6),false,nullptr,ETeleportType::TeleportPhysics);
            NPC->SetActorRotation((-TowardAttacker).Rotation());
            auto* ASC=NPC->GetRPGAbilitySystemComponent();
            const auto Ability=LoadClass<UGameplayAbility>(nullptr,AbilityPath);
            if (bGrounded)
            {
                // Exercise a grounded attack configuration through the authored weapon and damage path.
                auto* Spec=ASC->FindAbilitySpecFromClass(Ability);
                if (!Spec || !RPGKnockback::SetGrantedSettings(*Spec,GroundedSettings))
                { Fail(TEXT("GroundedAttackMetadata")); return false; }
            }
            if (!ASC->TryActivateAbilityByClass(Ability,false)) { Fail(TEXT("OriginalGrantActivation")); return false; }
            AttackAt=FPlatformTime::Seconds(); Step=2;
            UE_LOG(LogTemp,Display,TEXT("GUARDIAN_MELEE2_NETWORK_ACTIVATED NaturalWeaponHit=1 OriginalGrant=1"));
        }
        if ((Server && Step==2) || (!Server && Step==1))
        {
            Observe(Player); const auto State=Control->GetAirborneState();
            if (ExpectedDirection!=INDEX_NONE && State.IsActive()
                && static_cast<int32>(State.PresentationDirection)!=ExpectedDirection)
            { Fail(TEXT("UnexpectedApprovedHitDirection")); return false; }
            if (bGrounded && State.IsActive())
            {
                if (!Player->GetCharacterMovement()->IsMovingOnGround() || State.bMotionActive || Control->HasAirborneMotion()
                    || (State.Phase==ERPGAirbornePhase::Downed && (GroundFallEnd<=0.
                        || !FMath::IsNearlyEqual(GroundFallEnd, State.PhaseStartServerTime, .07))))
                { Fail(TEXT("GroundedFallMotionOrContactTime")); return false; }
            }
            if (bMovingPlatform && Player->IsLocallyControlled() && (State.Phase==ERPGAirbornePhase::Downed || State.Phase==ERPGAirbornePhase::GettingUp))
                Player->AddMovementInput(FVector::RightVector,1.f,true);
            if (State.Phase==ERPGAirbornePhase::Airborne && (!FMath::IsNearlyEqual(State.Distance,240.f) || !FMath::IsNearlyEqual(State.Height,180.f)
                || !FMath::IsWithinInclusive(State.DownDuration,1.f,1.6f) || !FMath::IsNearlyEqual(State.GetUpDuration,.8f))) { Fail(TEXT("AuthoredSpecChanged")); return false; }
            if ((Phases&(bGrounded?16:2)) && State.Phase==ERPGAirbornePhase::None)
            {
                if (!CompleteAt) CompleteAt=FPlatformTime::Seconds()+.5;
                if (FPlatformTime::Seconds()>=CompleteAt) Complete(Player,Driver);
            }
            if (Server && FPlatformTime::Seconds()-AttackAt>12.) Fail(TEXT("NaturalHitRecoveryTimeout"));
        }
        return true;
    }
};
static const FDelegateHandle Registration = FCoreDelegates::GetOnPostEngineInit().AddLambda([]()
{
    if (FParse::Param(FCommandLine::Get(), TEXT("RPGGuardianMelee2Network")))
    {
        auto Probe = MakeShared<FProbe>();
        FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateLambda([Probe](float Delta) { return Probe->Tick(Delta); }), .02f);
    }
});
}
#endif
