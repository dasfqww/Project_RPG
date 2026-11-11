#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGRidingSessionComponent.h"
#include "Riding/RPGRidingSession.h"
#include "Riding/RPGGameplayAbility_RidingEmote.h"
#include "Tests/RPGRidingDashMotionTests.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformTime.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
struct FRPGRidingEmoteTestAccess
{
    static void Unlock(URPGRidingSessionComponent& S){S.Runtime->EmoteReadyAt=0;S.PublishOwnerState();}
    static FRPGRidingEmoteTicket Ticket(const URPGRidingComponent& C){return C.ActiveEmote;}
    static bool Finish(URPGRidingComponent& C,const FRPGRidingEmoteTicket& T){return C.FinishEmote(T,ERPGRidingEndReason::Completed);}
    static void Expire(URPGRidingComponent& C){C.EmoteDeadline=FPlatformTime::Seconds()-1;C.ValidateServerMovementContext();}
    static void Configure(URPGRidingSessionComponent& S,float Duration,float Cooldown){S.Runtime->Policy.EmoteDuration=Duration;S.Runtime->Policy.EmoteCooldown=Cooldown;}
};
namespace
{
    constexpr auto Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
    struct FFixture
    {
        UWorld* World;
        APlayerController* PC;
        ARPGRidingDashTestPawn* Pawn;
        URPGRidingDashTestMovement* Move;
        URPGRidingComponent* Riding;
        URPGRidingSessionComponent* Session;
        uint64 Frame=0;
        explicit FFixture(bool Unlock=true)
        {
            const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true);
            World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            World->InitializeActorsForPlay(FURL());World->BeginPlay();World->GetWorldSettings()->NotifyBeginPlay();
            auto* Floor=World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Floor);Floor->SetRootComponent(Box);Floor->AddInstanceComponent(Box);
            Box->SetRelativeLocation(FVector(0,0,-50));Box->SetMobility(EComponentMobility::Static);Box->SetBoxExtent(FVector(3000,2000,50));
            Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Box->SetCollisionObjectType(ECC_WorldStatic);Box->SetCollisionResponseToAllChannels(ECR_Block);Box->RegisterComponent();
            PC=World->SpawnActor<APlayerController>();auto* PS=World->SpawnActor<APlayerState>();PC->SetAsLocalPlayerController();PC->SetPlayerState(PS);PS->SetOwner(PC);
            Session=NewObject<URPGRidingSessionComponent>(PS);Session->RegisterComponent();
            FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Pawn=World->SpawnActor<ARPGRidingDashTestPawn>(FVector(0,0,200),FRotator::ZeroRotator,P);PC->Possess(Pawn);
            Pawn->ASC->InitAbilityActorInfo(Pawn,Pawn);Pawn->ASC->AddAttributeSetSubobject(Pawn->Attributes.Get());
            Pawn->Attributes->CurrentHealth.SetBaseValue(100);Pawn->Attributes->CurrentHealth.SetCurrentValue(100);
            Move=CastChecked<URPGRidingDashTestMovement>(Pawn->GetCharacterMovement());Riding=Pawn->FindComponentByClass<URPGRidingComponent>();
            RPGPrepareRidingMovementTestSession(*Session,*Riding);RPGUnprotectRidingDashTestSession(*Session);
            if(Unlock)FRPGRidingEmoteTestAccess::Unlock(*Session);
            Move->SetMovementMode(MOVE_Falling);for(int32 I=0;I<40;++I)Tick();Riding->RequestSetMounted(true,{});
        }
        void Tick(){Frame=FMath::Max(Frame,GFrameCounter)+1;TGuardValue<uint64> Guard(GFrameCounter,Frame);World->Tick(LEVELTICK_All,1.f/60);}
        FGameplayAbilitySpecHandle Handle()const{for(const auto& S:Pawn->ASC->GetActivatableAbilities())if(S.Ability&&S.SourceObject.Get()==Riding&&S.Ability->GetClass()==URPGGameplayAbility_RidingEmote::StaticClass())return S.Handle;return {};}
        bool Active(FGameplayAbilitySpecHandle H)const{const auto* S=Pawn->ASC->FindAbilitySpecFromHandle(H);return S&&S->IsActive();}
        ~FFixture(){RPGTestWorld::Destroy(World);}
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingEmoteAdmission,"ProjectRPG.Riding.Emote.Admission",Flags)
bool FRidingEmoteAdmission::RunTest(const FString&)
{
    FFixture F(false);TestTrue(TEXT("Source-owned server ability granted only when mounted"),F.Handle().IsValid());
    TestFalse(TEXT("Initial two-second protection"),F.Riding->RequestEmote());FRPGRidingEmoteTestAccess::Unlock(*F.Session);
    const auto Before=F.Riding->GetRidingState();const auto Position=F.Pawn->GetActorLocation();
    if(!TestTrue(TEXT("Actual server GAS starts Emote"),F.Riding->RequestEmote()&&F.Active(F.Handle())))return false;
    const auto State=F.Riding->GetRidingState();TestTrue(TEXT("Canonical bounded cosmetic action"),RPGRiding::IsCanonical(State)&&State.Action==ERPGRidingAction::Emote&&State.ActionDuration==3&&State.ActionDirection.IsZero()&&State.Key.ActionSequence==Before.Key.ActionSequence+1);
    TestTrue(TEXT("No movement, security window or health change"),F.Move->Opened==0&&!F.Move->CurrentRootMotion.HasActiveRootMotionSources()&&F.Pawn->GetActorLocation()==Position&&F.Pawn->Attributes->CurrentHealth.GetCurrentValue()==100);
    TestTrue(TEXT("Cooldown reserved before caller returns"),F.Session->GetOwnerState().EmoteEndServerSeconds>F.World->GetTimeSeconds()+1.8);
    TestFalse(TEXT("Duplicate action rejected"),F.Riding->RequestEmote());
    FRPGRidingEmoteTestAccess::Expire(*F.Riding);TestTrue(TEXT("Deadline ends GAS and action"),!F.Active(F.Handle())&&F.Riding->GetRidingState().Action==ERPGRidingAction::None&&F.Riding->GetRidingState().LastEndReason==ERPGRidingEndReason::Completed);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingEmoteMovement,"ProjectRPG.Riding.Emote.Movement",Flags)
bool FRidingEmoteMovement::RunTest(const FString&)
{
    FFixture F;F.Move->Velocity=FVector(100,0,0);TestFalse(TEXT("Moving start denied without cooldown"),F.Riding->RequestEmote());
    TestTrue(TEXT("Denied request did not spend cooldown"),F.Session->GetOwnerState().EmoteEndServerSeconds<=F.World->GetTimeSeconds());
    F.Move->Velocity=FVector::ZeroVector;if(!TestTrue(TEXT("Stationary action"),F.Riding->RequestEmote()))return false;
    const auto End=F.Session->GetOwnerState().EmoteEndServerSeconds;F.Move->Velocity=FVector(100,0,0);F.Riding->ValidateServerMovementContext();
    TestTrue(TEXT("Movement cancels only cosmetic action"),F.Riding->GetRidingState().Mode==ERPGRidingMode::Mounted&&F.Riding->GetRidingState().Action==ERPGRidingAction::None&&!F.Active(F.Handle())&&F.Move->Velocity.X==100);
    TestTrue(TEXT("Cancellation retains server cooldown"),F.Session->GetOwnerState().EmoteEndServerSeconds>=End-.02);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingEmoteDash,"ProjectRPG.Riding.Emote.DashPreemption",Flags)
bool FRidingEmoteDash::RunTest(const FString&)
{
    FFixture F;if(!TestTrue(TEXT("Emote active"),F.Riding->RequestEmote()))return false;
    const auto Old=FRPGRidingEmoteTestAccess::Ticket(*F.Riding);const auto Cooldown=F.Session->GetOwnerState().EmoteEndServerSeconds;
    if(!TestTrue(TEXT("Dash preempts Emote through actual GAS"),F.Riding->RequestDash()&&F.Riding->IsServerDashActive()))return false;
    const auto Dash=F.Riding->GetRidingState();TestTrue(TEXT("Single atomic action increment"),Dash.Key.ActionSequence==Old.Key.ActionSequence+1&&Dash.ExecutionId!=Old.ExecutionId&&!F.Active(F.Handle()));
    TestFalse(TEXT("Late old Emote completion cannot clear Dash"),FRPGRidingEmoteTestAccess::Finish(*F.Riding,Old));
    TestTrue(TEXT("Dash and both cooldowns retained"),F.Riding->IsServerDashActive()&&F.Move->Opened==1&&F.Session->GetOwnerState().EmoteEndServerSeconds>=Cooldown-.02&&F.Session->GetOwnerState().DashEndServerSeconds>F.World->GetTimeSeconds()+8);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingEmoteGuards,"ProjectRPG.Riding.Emote.Guards",Flags)
bool FRidingEmoteGuards::RunTest(const FString&)
{
    FFixture F;FGameplayAbilitySpec Forged(URPGGameplayAbility_RidingEmote::StaticClass(),1,INDEX_NONE,F.Pawn);const auto H=F.Pawn->ASC->GiveAbility(Forged);
    TestFalse(TEXT("Another source cannot borrow the granted action"),F.Pawn->ASC->TryActivateAbility(H));F.Pawn->ASC->ClearAbility(H);
    F.Move->SetMovementMode(MOVE_Falling);TestFalse(TEXT("Airborne cannot Emote"),F.Riding->RequestEmote());
    F.Move->SetMovementMode(MOVE_Walking);F.Riding->RequestSetMounted(true,{});
    FRPGRidingEmoteTestAccess::Configure(*F.Session,1.25f,.5f);
    if(!TestTrue(TEXT("Server policy selected, no client profile"),F.Riding->RequestEmote()))return false;
    TestEqual(TEXT("Admitted duration"),F.Riding->GetRidingState().ActionDuration,1.25f);
    TestTrue(TEXT("Admitted cooldown"),F.Session->GetOwnerState().EmoteEndServerSeconds-F.World->GetTimeSeconds()<=.51);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingEmoteLifecycle,"ProjectRPG.Riding.Emote.Lifecycle",Flags)
bool FRidingEmoteLifecycle::RunTest(const FString&)
{
    FFixture F;const auto H=F.Handle();if(!TestTrue(TEXT("Active Emote"),F.Riding->RequestEmote()))return false;
    const auto Old=FRPGRidingEmoteTestAccess::Ticket(*F.Riding);const auto End=F.Session->GetOwnerState().EmoteEndServerSeconds;
    F.Riding->ForceDismount(ERPGRidingEndReason::Hit);
    TestTrue(TEXT("Hit ends action and revokes owned ability"),F.Riding->GetRidingState().Mode==ERPGRidingMode::OnFoot&&!F.Pawn->ASC->FindAbilitySpecFromHandle(H));
    F.Riding->RequestSetMounted(true,{});TestTrue(TEXT("Remount grants a fresh handle"),F.Handle().IsValid()&&F.Handle()!=H);
    TestFalse(TEXT("Remount cannot bypass common cooldown"),F.Riding->RequestEmote());
    TestTrue(TEXT("Cooldown survived"),F.Session->GetOwnerState().EmoteEndServerSeconds>=End-.02);
    TestFalse(TEXT("Old completion cannot affect remount"),FRPGRidingEmoteTestAccess::Finish(*F.Riding,Old));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingEmoteCallbacks,"ProjectRPG.Riding.Emote.Callbacks",Flags)
bool FRidingEmoteCallbacks::RunTest(const FString&)
{
    FFixture F;F.Riding->OnChanged.AddLambda([&](const auto& S){if(S.Action==ERPGRidingAction::Emote)F.Riding->ForceDismount(ERPGRidingEndReason::Manual);});
    F.Riding->RequestEmote();TestTrue(TEXT("Publication callback leaves no active action or ability"),F.Riding->GetRidingState().Mode==ERPGRidingMode::OnFoot&&!F.Handle().IsValid());
    TestTrue(TEXT("Approved callback cancellation never refunds"),F.Session->GetOwnerState().EmoteEndServerSeconds>F.World->GetTimeSeconds()+1.8);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingEmoteAvatar,"ProjectRPG.Riding.Emote.AvatarLifetime",Flags)
bool FRidingEmoteAvatar::RunTest(const FString&)
{
    FFixture F;const auto H=F.Handle();if(!TestTrue(TEXT("Action"),F.Riding->RequestEmote()))return false;
    F.Pawn->ASC->ClearActorInfo();F.Riding->ValidateServerMovementContext();F.Tick();
    TestTrue(TEXT("Avatar loss retires GAS and cosmetic action"),!F.Active(H)&&F.Riding->GetRidingState().Action==ERPGRidingAction::None);
    TestFalse(TEXT("Unpossessed source cannot restart"),F.Riding->RequestEmote());
    return true;
}
#endif
