// Opt-in, privately linked test fixture. Original BP pawns; native approved damage
// stimulus and in-memory grants/stats, not physical input or authored animations.
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
#include "GameFramework/CharacterMovementComponent.h"
#include "Character/RPGPlayer.h"
#include "Character/RPGBossCharacter.h"
#include "AI/RPGBossAIComponent.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "GameplayEffect/RPGDefenseReductionEffect.h"
#include "GameplayEffect/RPGDamageTakenIncreaseEffect.h"
#include "Tests/RPGCombatControlTestFixture.h"
#include "Tests/RPGSkillDamageSecurityTestFixture.h"
#include "RPGGameplayTags.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
namespace RPGCombatDebuffNetwork
{
static const TCHAR* PlayerPath=TEXT("/Game/Blueprints/Character/Player/BP_Player.BP_Player_C");
static const TCHAR* BossPath=TEXT("/Game/Blueprints/Character/NPC/SwordMan/BP_RaidSwordMan.BP_RaidSwordMan_C");
static constexpr uint32 AllStages=((1u<<14)-2)|(1u<<15);
static UAbilitySystemComponent* ASC(ARPGBaseCharacter* P) { return P?P->GetAbilitySystemComponent():nullptr; }
static float Health(ARPGBaseCharacter* P) { return ASC(P)->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()); }
static float Defense(ARPGBaseCharacter* P) { return ASC(P)->GetNumericAttribute(URPGAttributeSet::GetDefenseAttribute()); }
static bool Near(double A,double B) { return FMath::IsFinite(A)&&FMath::IsFinite(B)&&FMath::Abs(A-B)<.001; }

struct FProbe
{
    FString Directory,Role,URL;
    double Deadline=FPlatformTime::Seconds()+360.,WaitAt=0.,DiagnosticAt=0.;
    int32 Stage=0,Observed=0;
    uint32 Mask=0;
    bool bConfigured=false,bBoot=false,bTravelling=false,bPrepared=false,bDone=false;
    TWeakObjectPtr<ARPGPlayer> Player;
    TWeakObjectPtr<ARPGBossCharacter> Boss;
    FGameplayAbilitySpecHandle PlayerAbility,BossAbility;
    TArray<TSharedPtr<FJsonValue>> Observations;
    TArray<TSharedPtr<FJsonValue>> Hits;

    void Write(const FString& Name,const TSharedRef<FJsonObject>& Object)
    {
        FString Text;auto Writer=TJsonWriterFactory<>::Create(&Text);FJsonSerializer::Serialize(Object,Writer);
        const FString Path=Directory/Name,Temp=Path+TEXT(".tmp");
        if(!FFileHelper::SaveStringToFile(Text,*Temp,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
            || !IFileManager::Get().Move(*Path,*Temp,true,true))
        { UE_LOG(LogTemp,Error,TEXT("COMBAT_DEBUFF_NETWORK_IO_FAIL %s"),*Path);FPlatformMisc::RequestExitWithStatus(false,1); }
    }
    TSharedPtr<FJsonObject> Read(const FString& Name)
    {
        FString Text;TSharedPtr<FJsonObject> Object;
        if(FFileHelper::LoadFileToString(Text,*(Directory/Name)))
        { auto Reader=TJsonReaderFactory<>::Create(Text);FJsonSerializer::Deserialize(Reader,Object); }
        return Object;
    }
    bool Exists(const FString& Name) const { return IFileManager::Get().FileExists(*(Directory/Name)); }
    void Fail(const TCHAR* Reason)
    {
        if(bDone)return;
        UE_LOG(LogTemp,Error,TEXT("COMBAT_DEBUFF_NETWORK_FAIL Role=%s Stage=%d Reason=%s"),*Role,Stage,Reason);
        auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("success"),false);R->SetStringField(TEXT("reason"),Reason);
        R->SetNumberField(TEXT("stage"),Stage);Write(Role+TEXT(".result.json"),R);bDone=true;
        FPlatformMisc::RequestExitWithStatus(false,1);
    }
    TArray<ARPGPlayer*> Players(UWorld* World)
    {
        TArray<ARPGPlayer*> Found;
        for(TActorIterator<ARPGPlayer> It(World);It;++It)
            if(It->GetPlayerState()&&ASC(*It)&&ASC(*It)->GetAvatarActor()==*It&&Health(*It)>0)Found.Add(*It);
        Found.Sort([](const ARPGPlayer& A,const ARPGPlayer& B){return A.GetPlayerState()->GetPlayerId()<B.GetPlayerState()->GetPlayerId();});
        return Found;
    }
    bool Configure(UWorld* World)
    {
        auto* Mode=World->GetAuthGameMode();if(!Mode)return false;
        if(World->GetMapName()!=TEXT("Entry")||Mode->GetClass()!=AGameModeBase::StaticClass()){Fail(TEXT("EntryGameModeRequired"));return false;}
        Mode->DefaultPawnClass=LoadClass<APawn>(nullptr,PlayerPath);
        Mode->PlayerControllerClass=LoadClass<APlayerController>(nullptr,TEXT("/Script/Project_RPG.RPGPlayerController"));
        Mode->PlayerStateClass=LoadClass<APlayerState>(nullptr,TEXT("/Script/Project_RPG.RPGPlayerState"));
        if(!Mode->DefaultPawnClass||!Mode->PlayerControllerClass||!Mode->PlayerStateClass){Fail(TEXT("PlayerClassMissing"));return false;}
        FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Floor=World->SpawnActor<AStaticMeshActor>(FVector(0,0,-50),FRotator::ZeroRotator,P);
        Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
        Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));Floor->SetActorScale3D(FVector(40,40,1));
        Floor->SetReplicates(true);Floor->SetReplicateMovement(true);Floor->GetStaticMeshComponent()->SetIsReplicated(true);Floor->bAlwaysRelevant=true;
        for(float X:{0.f,800.f})World->SpawnActor<APlayerStart>(FVector(X,0,150),FRotator::ZeroRotator,P);
        UE_LOG(LogTemp,Display,TEXT("COMBAT_DEBUFF_NETWORK_SERVER_READY"));return true;
    }
    TSharedRef<FJsonObject> Sample(ARPGBaseCharacter* P)
    {
        auto O=MakeShared<FJsonObject>();auto* A=ASC(P);auto* C=P->GetCombatControlComponent();
        O->SetStringField(TEXT("class"),P->GetClass()->GetPathName());O->SetNumberField(TEXT("hp"),Health(P));O->SetNumberField(TEXT("defense"),Defense(P));
        O->SetBoolField(TEXT("defenseTag"),A->HasMatchingGameplayTag(RPGDefenseReduction::StatusTag()));
        O->SetBoolField(TEXT("takenTag"),A->HasMatchingGameplayTag(RPGDamageTakenIncrease::StatusTag()));
        O->SetBoolField(TEXT("authority"),P->HasAuthority());O->SetBoolField(TEXT("local"),P->IsLocallyControlled());
        O->SetBoolField(TEXT("actionBlocked"),RPGCombatControl::IsActionBlocked(P));O->SetNumberField(TEXT("multiplier"),C->GetDamageTakenMultiplier());
        O->SetBoolField(TEXT("hitImmune"),C->IsImmuneTo(ERPGCombatEffectKind::Stagger));O->SetBoolField(TEXT("stunImmune"),C->IsImmuneTo(ERPGCombatEffectKind::Stun));
        O->SetNumberField(TEXT("replicationMode"),int32(A->ReplicationMode));
        int32 Effects=0;
        for(auto H:A->GetActiveEffects(FGameplayEffectQuery()))
        { const auto* E=A->GetActiveGameplayEffect(H);if(E&&(E->Spec.Def==GetDefault<URPGDefenseReductionEffect>()||E->Spec.Def==GetDefault<URPGDamageTakenIncreaseEffect>()))++Effects; }
        O->SetNumberField(TEXT("statusEffects"),Effects);
        return O;
    }
    bool Matches(ARPGBaseCharacter* P,const TSharedPtr<FJsonObject>& Expected)
    {
        if(!P||!Expected||!ASC(P)||ASC(P)->GetAvatarActor()!=P)return false;
        auto* A=ASC(P);
        return P->GetClass()->GetPathName()==Expected->GetStringField(TEXT("class"))
            &&Near(Health(P),Expected->GetNumberField(TEXT("hp")))&&Near(Defense(P),Expected->GetNumberField(TEXT("defense")))
            &&A->HasMatchingGameplayTag(RPGDefenseReduction::StatusTag())==Expected->GetBoolField(TEXT("defenseTag"))
            &&A->HasMatchingGameplayTag(RPGDamageTakenIncrease::StatusTag())==Expected->GetBoolField(TEXT("takenTag"));
    }
    bool Check(ARPGBaseCharacter* P,float D,double M,bool Active)
    {
        auto* C=P->GetCombatControlComponent();auto* A=ASC(P);
        if(!Near(Defense(P),D)||!Near(C->GetDamageTakenMultiplier(),M)
            ||A->HasMatchingGameplayTag(RPGDefenseReduction::StatusTag())!=Active
            ||A->HasMatchingGameplayTag(RPGDamageTakenIncrease::StatusTag())!=Active){Fail(TEXT("ServerDerivedState"));return false;}
        return true;
    }
    void Publish(int32 Next)
    {
        Stage=Next;WaitAt=FPlatformTime::Seconds();Mask|=1u<<Next;
        auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("stage"),Stage);O->SetNumberField(TEXT("playerId"),Player->GetPlayerState()->GetPlayerId());
        O->SetObjectField(TEXT("player"),Sample(Player.Get()));O->SetObjectField(TEXT("boss"),Sample(Boss.Get()));
        O->SetNumberField(TEXT("encounterState"),int32(Boss->BossAIComponent->GetBossState().EncounterState));
        Write(TEXT("stage.json"),O);Observations.Add(MakeShared<FJsonValueObject>(O));
        UE_LOG(LogTemp,Display,TEXT("COMBAT_DEBUFF_NETWORK_STAGE Stage=%d PlayerHP=%g BossHP=%g BossDefense=%g"),Stage,Health(Player.Get()),Health(Boss.Get()),Defense(Boss.Get()));
    }
    bool Confirmed() { return Exists(FString::Printf(TEXT("owner.%d.ack.json"),Stage))&&Exists(FString::Printf(TEXT("observer.%d.ack.json"),Stage)); }
    bool Hit(bool PlayerSource,float Armor,float Taken,float Duration)
    {
        ARPGBaseCharacter* S=PlayerSource?static_cast<ARPGBaseCharacter*>(Player.Get()):Boss.Get();
        ARPGBaseCharacter* T=PlayerSource?static_cast<ARPGBaseCharacter*>(Boss.Get()):Player.Get();
        auto* A=ASC(S);auto* Spec=A->FindAbilitySpecFromHandle(PlayerSource?PlayerAbility:BossAbility);
        FRPGDefenseReductionSettings D;D.Percent=Armor;D.Duration=Duration;
        FRPGDamageTakenIncreaseSettings V;V.Percent=Taken;V.Duration=Duration;
        if(!Spec||!RPGDefenseReduction::SetGrantedSettings(*Spec,D)||!RPGDamageTakenIncrease::SetGrantedSettings(*Spec,V))
        {Fail(TEXT("GrantSettings"));return false;}
        A->MarkAbilitySpecDirty(*Spec);
        auto* Ability=Spec->GetPrimaryInstance();
        if(!Spec->IsActive()&&!A->TryActivateAbility(Spec->Handle)){Fail(TEXT("FixtureActivation"));return false;}
        UFunction* Function=Ability?Ability->FindFunction(TEXT("BP_ApplyEffectSpecHandleToTarget")):nullptr;
        if(!Function){Fail(TEXT("ApprovedFunctionMissing"));return false;}
        auto Damage=A->MakeOutgoingSpec(URPGSkillDamageSecurityTestEffect::StaticClass(),1,A->MakeEffectContext());
        Damage.Data->SetSetByCallerMagnitude(RPGGameplayTags::Shared_SetByCaller_BaseDamage,10.f);
        const float Before=Health(T);const double Expected=10.0/Defense(T)*T->GetCombatControlComponent()->GetDamageTakenMultiplier();
        FStructOnScope Params(Function);auto* Data=Params.GetStructMemory();
        auto* TargetProp=FindFProperty<FObjectPropertyBase>(Function,TEXT("TargetActor"));
        auto* DamageProp=FindFProperty<FStructProperty>(Function,TEXT("InSpecHandle"));
        auto* SuccessProp=FindFProperty<FProperty>(Function,TEXT("OutSuccessType"));
        if(!TargetProp||!DamageProp||!SuccessProp){Fail(TEXT("ApprovedFunctionShape"));return false;}
        TargetProp->SetObjectPropertyValue_InContainer(Data,T);
        DamageProp->CopyCompleteValue(DamageProp->ContainerPtrToValuePtr<void>(Data),&Damage);
        Ability->ProcessEvent(Function,Data);
        if(*SuccessProp->ContainerPtrToValuePtr<ERPGSuccessType>(Data)!=ERPGSuccessType::Successful
            ||!Near(Before-Health(T),int32(Expected))){Fail(TEXT("ApprovedDamageMismatch"));return false;}
        auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("playerSource"),PlayerSource);R->SetNumberField(TEXT("before"),Before);
        R->SetNumberField(TEXT("after"),Health(T));R->SetNumberField(TEXT("expectedDamage"),int32(Expected));
        R->SetNumberField(TEXT("armorPercent"),Armor);R->SetNumberField(TEXT("takenPercent"),Taken);Hits.Add(MakeShared<FJsonValueObject>(R));
        T->ForceNetUpdate();return true;
    }
    void Complete(UNetDriver* Driver)
    {
        if(Mask!=AllStages){Fail(TEXT("MissingStages"));return;}
        auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("success"),true);R->SetStringField(TEXT("role"),Role);
        R->SetNumberField(TEXT("stages"),Mask);R->SetBoolField(TEXT("iris"),Driver->IsUsingIrisReplication());
        R->SetNumberField(TEXT("lagMin"),Driver->PacketSimulationSettings.PktLagMin);R->SetNumberField(TEXT("lagMax"),Driver->PacketSimulationSettings.PktLagMax);
        R->SetNumberField(TEXT("loss"),Driver->PacketSimulationSettings.PktLoss);R->SetArrayField(TEXT("observations"),Observations);R->SetArrayField(TEXT("hits"),Hits);
        R->SetBoolField(TEXT("nativeApprovedStimulus"),true);R->SetBoolField(TEXT("fixtureGrant"),true);R->SetBoolField(TEXT("physicalInput"),false);
        R->SetStringField(TEXT("playerClass"),PlayerPath);R->SetStringField(TEXT("bossClass"),BossPath);
        Write(Role+TEXT(".result.json"),R);bDone=true;
        UE_LOG(LogTemp,Display,TEXT("COMBAT_DEBUFF_NETWORK_COMPLETE Role=%s Stages=%u"),*Role,Mask);
    }
    bool Prepare(UWorld* World)
    {
        auto Found=Players(World);if(Found.Num()!=1)return false;Player=Found[0];
        if(!Boss.IsValid())
        {
            FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Boss=World->SpawnActor<ARPGBossCharacter>(LoadClass<ARPGBossCharacter>(nullptr,BossPath),Player->GetActorLocation()+FVector(180,0,0),FRotator(0,180,0),P);
            if(!Boss.IsValid()){Fail(TEXT("OriginalBossMissing"));return false;}Boss->bAlwaysRelevant=true;
        }
        if(auto* AI=Cast<AAIController>(Boss->GetController()))
        {if(AI->GetBrainComponent()&&AI->GetBrainComponent()->IsRunning())AI->GetBrainComponent()->StopLogic(TEXT("Opt-in combat debuff fixture"));AI->StopMovement();}
        if(!Boss->IsNPCStartUpDataReady()||!Boss->GetController()||!Player->GetMesh()->GetAnimInstance()||!Boss->GetMesh()->GetAnimInstance())return false;
        if(Player->GetMesh()->GetAnimInstance()->IsAnyMontagePlaying()||Boss->GetMesh()->GetAnimInstance()->IsAnyMontagePlaying())return false;
        if(!WaitAt){WaitAt=FPlatformTime::Seconds()+1.5;return false;}if(FPlatformTime::Seconds()<WaitAt)return false;
        for(ARPGBaseCharacter* P:{static_cast<ARPGBaseCharacter*>(Player.Get()),static_cast<ARPGBaseCharacter*>(Boss.Get())})
        {
            auto* A=ASC(P);A->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(),10000);A->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(),10000);
            A->SetNumericAttributeBase(URPGAttributeSet::GetAttackAttribute(),1);A->SetNumericAttributeBase(URPGAttributeSet::GetDefenseAttribute(),2);
            A->SetNumericAttributeBase(URPGAttributeSet::GetCriticalChanceAttribute(),0);
            if(!Near(Defense(P),2)||!Near(A->GetNumericAttribute(URPGAttributeSet::GetAttackAttribute()),1)){Fail(TEXT("FixtureStatsNotStable"));return false;}
            FGameplayAbilitySpec Spec(URPGStunTestCombatAbility::StaticClass(),1);Spec.SourceObject=P;
            RPGHitReaction::SetGrantedPolicy(Spec,ERPGAttackHitReactionPolicy::None);
            auto Handle=A->GiveAbility(Spec);if(!A->TryActivateAbility(Handle)){Fail(TEXT("InitialFixtureActivation"));return false;}
            if(P==Player.Get())PlayerAbility=Handle;else BossAbility=Handle;P->ForceNetUpdate();
        }
        if(Boss->BossAIComponent->GetBossState().EncounterState==ERPGBossEncounterState::Idle&&!Boss->BossAIComponent->StartEncounter())
        {Fail(TEXT("BossEncounterStart"));return false;}
        auto* C=Boss->GetCombatControlComponent();
        if(!C->IsImmuneTo(ERPGCombatEffectKind::Stagger)||!C->IsImmuneTo(ERPGCombatEffectKind::Stun)
            ||C->IsImmuneTo(ERPGCombatEffectKind::DefenseReduction)||C->IsImmuneTo(ERPGCombatEffectKind::DamageTakenIncrease)
            ||ASC(Boss.Get())->ReplicationMode!=EGameplayEffectReplicationMode::Minimal){Fail(TEXT("BossDefaultPolicy"));return false;}
        return true;
    }
    void Server(UWorld* World,UNetDriver* Driver)
    {
        if(!bConfigured){bConfigured=Configure(World);return;}
        if(!bPrepared)
        {
            if(!Exists(TEXT("start")))return;
            if(!Prepare(World))return;bPrepared=true;
            if(!Hit(false,50,50,30)||!Check(Player.Get(),1,1.5,true))return;
            Publish(1);UE_LOG(LogTemp,Display,TEXT("COMBAT_DEBUFF_NETWORK_WAIT_LATE_JOIN ActiveStatuses=1"));return;
        }
        const auto Clean=[](ARPGBaseCharacter* P){P->GetCombatControlComponent()->CleanseDefenseReductions();P->GetCombatControlComponent()->CleanseDamageTakenIncreases();};
        if(Stage==1&&Confirmed()){Clean(Player.Get());if(Check(Player.Get(),2,1,false))Publish(2);return;}
        if(Stage==2&&Confirmed()){if(Hit(false,0,0,3)&&Check(Player.Get(),2,1,false))Publish(3);return;}
        if(Stage==3&&Confirmed()){if(Hit(true,50,50,15)&&Check(Boss.Get(),1,1.5,true))Publish(4);return;}
        if(Stage==4&&Confirmed()){if(Hit(true,20,30,3)&&Check(Boss.Get(),.8f,1.95,true))Publish(5);return;}
        if(Stage==5&&Confirmed()){if(Hit(true,0,0,3)&&Check(Boss.Get(),.8f,1.95,true))Publish(6);return;}
        if(Stage==6&&Confirmed()&&Near(Defense(Boss.Get()),1)&&Near(Boss->GetCombatControlComponent()->GetDamageTakenMultiplier(),1.5))
        {Publish(7);return;}
        if(Stage==7&&Confirmed()){Clean(Boss.Get());if(Check(Boss.Get(),2,1,false))Publish(8);return;}
        if(Stage==8&&Confirmed())
        {
            auto* C=Player->GetCombatControlComponent();C->SetDefaultDefenseReductionImmunity(true);C->SetDefaultDamageTakenIncreaseImmunity(true);
            const bool Applied=Hit(false,50,50,3);C->SetDefaultDefenseReductionImmunity(false);C->SetDefaultDamageTakenIncreaseImmunity(false);
            if(Applied&&Check(Player.Get(),2,1,false))Publish(9);return;
        }
        if(Stage==9&&Confirmed())
        {
            const auto Tag=FGameplayTag::RequestGameplayTag(TEXT("Shared.Immunity.StatusEffects"));ASC(Boss.Get())->AddLooseGameplayTag(Tag);
            const bool Applied=Hit(true,50,50,3);ASC(Boss.Get())->RemoveLooseGameplayTag(Tag);
            if(Applied&&Check(Boss.Get(),2,1,false))Publish(10);return;
        }
        if(Stage==10&&Confirmed()){if(Hit(true,50,50,3)&&Check(Boss.Get(),1,1.5,true))Publish(11);return;}
        if(Stage==11&&Confirmed()&&!ASC(Boss.Get())->HasMatchingGameplayTag(RPGDefenseReduction::StatusTag())
            &&!ASC(Boss.Get())->HasMatchingGameplayTag(RPGDamageTakenIncrease::StatusTag()))
        {if(Check(Boss.Get(),2,1,false))Publish(12);return;}
        if(Stage==12&&Confirmed())
        {
            if(!Hit(true,50,50,10)||!Check(Boss.Get(),1,1.5,true))return;
            // Active effects are confirmed on both roles before reset by the next stage.
            Publish(13);return;
        }
        if(Stage==13&&Confirmed())
        {
            if(!Boss->BossAIComponent->BeginReset()){Fail(TEXT("BossReset"));return;}
            Stage=14;WaitAt=FPlatformTime::Seconds();return;
        }
        if(Stage==14&&!ASC(Boss.Get())->HasMatchingGameplayTag(RPGDefenseReduction::StatusTag())
            &&!ASC(Boss.Get())->HasMatchingGameplayTag(RPGDamageTakenIncrease::StatusTag()))
        {
            if(!Check(Boss.Get(),2,1,false))return;
            Publish(15);return;
        }
        if(Stage==15&&Confirmed())Complete(Driver);
    }
    void Client(UWorld* World,UNetDriver* Driver)
    {
        auto State=Read(TEXT("stage.json"));if(!State)return;
        const int32 Next=int32(State->GetNumberField(TEXT("stage")));if(Next==Observed)return;
        if((Next!=Observed+1)&&!(Observed==13&&Next==15)){Fail(TEXT("ClientSkippedStage"));return;}
        ARPGPlayer* P=nullptr;for(auto* Candidate:Players(World))if(Candidate->GetPlayerState()->GetPlayerId()==int32(State->GetNumberField(TEXT("playerId"))))P=Candidate;
        ARPGBossCharacter* B=nullptr;for(TActorIterator<ARPGBossCharacter> It(World);It;++It)if(It->GetClass()->GetPathName()==BossPath)B=*It;
        if(!P||!B||!Matches(P,State->GetObjectField(TEXT("player")))||!Matches(B,State->GetObjectField(TEXT("boss"))))return;
        if(int32(B->BossAIComponent->GetBossState().EncounterState)!=int32(State->GetNumberField(TEXT("encounterState"))))return;
        if(P->HasAuthority()||B->HasAuthority()||P->IsLocallyControlled()!=(Role==TEXT("owner"))){Fail(TEXT("ClientAuthorityOrOwnership"));return;}
        if(!B->GetCombatControlComponent()->IsImmuneTo(ERPGCombatEffectKind::Stagger)||!B->GetCombatControlComponent()->IsImmuneTo(ERPGCombatEffectKind::Stun))
        {Fail(TEXT("ClientBossDefaultImmunity"));return;}
        if(RPGCombatControl::IsActionBlocked(P)||RPGCombatControl::IsActionBlocked(B)){Fail(TEXT("DebuffBlockedActions"));return;}
        if(!Near(P->GetCombatControlComponent()->GetDamageTakenMultiplier(),1)||!Near(B->GetCombatControlComponent()->GetDamageTakenMultiplier(),1))
        {Fail(TEXT("ClientAcquiredDamageAuthority"));return;}
        Player=P;Boss=B;Observed=Next;Stage=Next;Mask|=1u<<Next;
        auto Ack=MakeShared<FJsonObject>();Ack->SetBoolField(TEXT("success"),true);Ack->SetNumberField(TEXT("stage"),Next);
        Ack->SetNumberField(TEXT("encounterState"),int32(B->BossAIComponent->GetBossState().EncounterState));
        Ack->SetStringField(TEXT("role"),Role);Ack->SetObjectField(TEXT("player"),Sample(P));Ack->SetObjectField(TEXT("boss"),Sample(B));
        Observations.Add(MakeShared<FJsonValueObject>(Ack));Write(FString::Printf(TEXT("%s.%d.ack.json"),*Role,Next),Ack);
        UE_LOG(LogTemp,Display,TEXT("COMBAT_DEBUFF_NETWORK_OBSERVED Role=%s Stage=%d"),*Role,Next);
        if(Next==15)Complete(Driver);
    }
    bool Tick(float)
    {
        if(Role.IsEmpty())
        {
            FParse::Value(FCommandLine::Get(),TEXT("RPGCombatDebuffDirectory="),Directory);
            FParse::Value(FCommandLine::Get(),TEXT("RPGCombatDebuffRole="),Role);
            FParse::Value(FCommandLine::Get(),TEXT("RPGCombatDebuffURL="),URL);
            if(Directory.IsEmpty()||!(Role==TEXT("server")||Role==TEXT("owner")||Role==TEXT("observer"))){Fail(TEXT("Arguments"));return false;}
        }
        if(bDone){if(Exists(Role+TEXT(".exit"))){FPlatformMisc::RequestExitWithStatus(false,0);return false;}return true;}
        if(FPlatformTime::Seconds()>Deadline){Fail(TEXT("Deadline"));return false;}
        UWorld* World=nullptr;
        if(GEngine)for(const auto& C:GEngine->GetWorldContexts())
            if(C.WorldType==EWorldType::Game&&C.World())
            { if(C.World()->GetNetMode()!=NM_Standalone){World=C.World();break;}World=C.World(); }
        if(!World)return true;
        if(Role!=TEXT("server")&&World->GetNetMode()==NM_Standalone)
        {
            if(!bBoot){auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("ready"),true);Write(Role+TEXT(".boot.json"),R);bBoot=true;}
            if(!bTravelling&&Exists(Role+TEXT(".connect")))
            {if(auto* PC=World->GetFirstPlayerController()){PC->ClientTravel(URL,TRAVEL_Absolute);bTravelling=true;}}
            return true;
        }
        auto* Driver=World->GetNetDriver();if(!Driver)return true;
        if(!Driver->IsUsingIrisReplication()||Driver->PacketSimulationSettings.PktLagMin!=80||Driver->PacketSimulationSettings.PktLagMax!=160
            ||Driver->PacketSimulationSettings.PktLoss!=5){Fail(TEXT("IrisOrPacketSettings"));return false;}
        if(Role==TEXT("server"))
        {if(World->GetNetMode()!=NM_DedicatedServer){Fail(TEXT("ServerMode"));return false;}Server(World,Driver);}
        else
        {
            if(World->GetNetMode()!=NM_Client){Fail(TEXT("ClientMode"));return false;}
            if(!bBoot){auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("ready"),true);Write(Role+TEXT(".boot.json"),R);bBoot=true;}
            if(!Players(World).IsEmpty()&&!Exists(Role+TEXT(".connected.json")))
            {auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("ready"),true);Write(Role+TEXT(".connected.json"),R);}
            Client(World,Driver);
        }
        return true;
    }
};
static const auto Handle=[]()
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("RPGCombatDebuffNetwork")))return FTSTicker::FDelegateHandle();
    auto P=MakeShared<FProbe>();return FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([P](float Delta){return P->Tick(Delta);}),.03f);
}();
}
#endif
