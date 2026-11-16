#include "Development/RPGSoloRaidSubsystem.h"
#include "Development/RPGSoloRaidBot.h"
#include "Development/RPGSoloRaidControl.h"
#include "Development/RPGSoloRaidPanel.h"
#include "AI/RPGBossAIComponent.h"
#include "AI/RPGBossSummonObjectiveComponent.h"
#include "AI/RPGBossSummonComponent.h"
#include "InputCoreTypes.h"

#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGBossCharacter.h"
#include "Character/RPGPlayer.h"
#include "Component/Combat/PlayerCombatComponent.h"
#include "Component/Equipment/RPGAuthoritativeEquipmentComponent.h"
#include "Components/CapsuleComponent.h"
#include "Controller/RPGPlayerController.h"
#include "DataAsset/Input/DataAsset_InputConfig.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "GameMode/RPGBossBattleGameMode.h"
#include "GameState/RPGBossBattleGameState.h"
#include "InputActionValue.h"
#include "Item/Definition/RPGItemDefinition.h"
#include "Item/Weapon/RPGPlayerWeapon.h"
#include "Manager/DataManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RPGGameplayTags.h"
#include "UObject/UnrealType.h"

namespace
{
    float Health(const AActor* Actor)
    {
        const auto* Player = Cast<ARPGBaseCharacter>(Actor);
        const auto* ASC = IsValid(Player) ? Player->GetAbilitySystemComponent() : nullptr;
        return ASC ? ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) : 0.f;
    }
}

bool URPGSoloRaidSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
    return false;
#else
    const UWorld* World = Cast<UWorld>(Outer);
    // Require both explicit flags, an isolated control file, and no backend assignment.
    FString Path;
    if (!World || World->WorldType != EWorldType::Game ||
        !FParse::Param(FCommandLine::Get(), TEXT("RPGSoloRaid")) ||
        !FParse::Param(FCommandLine::Get(), TEXT("RPGNetTestMode")) ||
        !FParse::Value(FCommandLine::Get(), TEXT("RPGSoloRaidControl="), Path) ||
        !FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_DUNGEON_SESSION_ID")).IsEmpty() ||
        !FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN")).IsEmpty()) return false;
    return FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Path),
        FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Saved/SoloRaid")));
#endif
}

void URPGSoloRaidSubsystem::OnWorldBeginPlay(UWorld& World)
{
    Super::OnWorldBeginPlay(World);
    FParse::Value(FCommandLine::Get(), TEXT("RPGSoloRaidControl="), ControlPath);
    FParse::Value(FCommandLine::Get(), TEXT("RPGSoloRaidBot="), BotIndex);
    FParse::Value(FCommandLine::Get(), TEXT("RPGSoloRaidSlot="), PlayerSlot);
    FParse::Value(FCommandLine::Get(), TEXT("RPGSoloRaidPlayers="), PartySize);
    PartySize = FMath::Clamp(PartySize, 1, 4);
    PlayerSlot = FMath::Clamp(PlayerSlot, 0, PartySize - 1);
    SlotStatuses.Init(TEXT("접속 대기"), PartySize);
    SlotPolicies.Init(TEXT("assist"), PartySize);
    bWorldStarted = true;
    if (auto* GameMode = World.GetAuthGameMode<ARPGBossBattleGameMode>())
    {
        // Instance-only: cold clients must not consume the authored 30-second timer.
        if (auto* Time = FindFProperty<FFloatProperty>(GameMode->GetClass(), TEXT("BattleTimeLimit")))
            Time->SetPropertyValue_InContainer(GameMode, 1800.f);
    }
    UE_LOG(LogTemp, Display, TEXT("SOLO_RAID_WORLD Bot=%d NetMode=%d"), BotIndex, int32(World.GetNetMode()));
}

void URPGSoloRaidSubsystem::ReleaseInput()
{
    if (Input.IsValid())
    {
        if (MoveAction.IsValid()) Input->StopContinuousInputInjectionForAction(MoveAction.Get());
        if (AttackAction.IsValid()) Input->StopContinuousInputInjectionForAction(AttackAction.Get());
    }
    Input.Reset(); MoveAction.Reset(); AttackAction.Reset();
}

void URPGSoloRaidSubsystem::Deinitialize()
{
    bWorldStarted = false;
    ClosePanel();
    ReleaseInput();
    Super::Deinitialize();
}

void URPGSoloRaidSubsystem::Tick(float DeltaTime)
{
    // Pending network-entry worlds tick before BeginPlay and have no party/config yet.
    if (!bWorldStarted) return;
    const double Now = FPlatformTime::Seconds();
    if (Now >= NextPoll)
    {
        NextPoll = Now + .2;
        FString Command, Verb;
        Policy = RPGSoloRaidControl::ReadPolicy(FPaths::GetPath(ControlPath), PlayerSlot);
        if (RPGSoloRaidControl::ReadCommand(ControlPath, Verb, Command))
        {
            Command.TrimStartAndEndInline();
            // Recover after a transient file read failure without replaying a restart.
            Mode = Verb;
            if (Command != LastCommand)
            {
                if (RPGSoloRaidControl::IsMode(Verb))
                {
                    LastCommand = Command;
                    Mode = Verb;
                    EvadeUntil = 0;
                    ReleaseInput();
                    if (Verb == TEXT("restart"))
                    {
                        if (auto* GM = GetWorld()->GetAuthGameMode<ARPGBossBattleGameMode>())
                        {
                            if (GM->GetCurrentBossBattleState() == EBossBattleState::InProgress) GM->Defeated();
                            // Local workbench retry starts the party at its recorded entry transforms.
                            // Retire owned participant pawns so the existing missing-pawn retry path
                            // performs normal RestartPlayerAtTransform, startup and equipment recovery.
                            // This also recovers a living pawn knocked off the authored floor.
                            if (GM->GetCurrentBossBattleState() == EBossBattleState::Defeated ||
                                GM->GetCurrentBossBattleState() == EBossBattleState::Victory)
                                for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
                                    if (auto* Pawn = Cast<ARPGPlayer>(It->Get()->GetPawn())) Pawn->Destroy();
                            GM->RestartBattle();
                            UE_LOG(LogTemp, Display, TEXT("SOLO_RAID_RESTART State=%d"), int32(GM->GetCurrentBossBattleState()));
                        }
                    }
                    UE_LOG(LogTemp, Display, TEXT("SOLO_RAID_MODE %s"), *Mode);
                }
            }
        }
        else { Mode = TEXT("prepare"); ReleaseInput(); }
    }
    if (GetWorld()->GetNetMode() != NM_Client) TickServer();
    else
    {
        TickPlayer();
        // Unhandled keys may reach the controller even when a result window takes focus.
        if (IsHumanLeader())
            if (auto* PC = GetWorld()->GetFirstPlayerController())
                for (const FKey Key : { EKeys::F5, EKeys::F6, EKeys::F7, EKeys::F8, EKeys::F9, EKeys::F10 })
                    if (PC->WasInputKeyJustPressed(Key)) HandleWorkbenchKey(Key);
        if (Now >= NextObservation) { NextObservation = Now + .25; UpdateObservation(); }
    }
}

void URPGSoloRaidSubsystem::TickServer()
{
    auto* State = GetWorld()->GetGameState<ARPGBossBattleGameState>();
    if (!State) return;
    auto* Boss = State->GetBattleSnapshot().Boss.Get();
    if (Boss != HeldBoss.Get()) { HeldBoss = Boss; bBrainHeld = false; }
    if (auto* AI = IsValid(Boss) ? Cast<AAIController>(Boss->GetController()) : nullptr)
    {
        if (auto* Brain = AI->GetBrainComponent())
        {
            const bool Hold = Mode == TEXT("prepare") || Mode == TEXT("restart");
            if (Hold && Brain->IsRunning()) { Brain->StopLogic(TEXT("Solo raid preparation")); bBrainHeld = true; }
            else if (!Hold && bBrainHeld) { Brain->RestartLogic(); bBrainHeld = false; }
        }
    }
    auto* Catalog = GetWorld()->GetGameInstance()->GetSubsystem<UDataManager>();
    const auto* Definition = LoadObject<URPGItemDefinition>(nullptr,
        TEXT("/Game/Blueprints/GameData/Item/Weapons/DA_Item_GreatSword.DA_Item_GreatSword"));
    if (!Catalog || !Definition) return;
    int32 Players = 0;
    for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        auto* Player = Cast<ARPGPlayer>(It->Get()->GetPawn());
        auto* Equipment = Player ? Player->FindComponentByClass<URPGAuthoritativeEquipmentComponent>() : nullptr;
        if (!Equipment) continue;
        ++Players;
        if (!Equipment->HasWeaponSnapshots() && Equipment->NumAppliedItems() == 0 && Health(Player) > 0)
        {
            FRPGItemOwnerRef Owner;
            Owner.Type = ERPGItemOwnerType::Character;
            Owner.OwnerId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
            FRPGItemLocation Location;
            Location.ContainerType = ERPGItemContainerType::Equipment;
            Location.ContainerId = Owner.OwnerId;
            Location.SlotIndex = int32(EEquipmentSlotType::Weapon_Primary_R);
            FRPGItemInstanceState Instance;
            FRPGItemRecord Record;
            FString Error;
            if (FRPGItemInstanceState::TryRestore(FGuid::NewGuid(), 0, 1, {}, {}, Instance) &&
                FRPGItemRecord::TryRestore(Definition->GetPrimaryAssetId(), Definition->GetDefinitionVersion(),
                    Owner, Location, Instance, 1, ERPGItemLifecycleState::Active, {}, Record) &&
                Equipment->Reconcile(Owner, {Record}, *Catalog, &Error))
                UE_LOG(LogTemp, Display, TEXT("SOLO_RAID_EQUIPPED %s MemoryOnly=1"), *Player->GetName());
        }
    }
    if (IsValid(Boss) && Boss->BossSummonObjectiveComponent && Boss->BossAIComponent)
    {
        const auto Objective = Boss->BossSummonObjectiveComponent->GetObjectiveState();
        const auto BossState = Boss->BossAIComponent->GetBossState();
        const FString Observation = FString::Printf(TEXT("Phase=%d Active=%d Kills=%d Required=%d TimedOut=%d Encounter=%d"),
            BossState.PhaseIndex, int32(Objective.bActive), Objective.DefeatedCount, Objective.RequiredCount,
            int32(BossState.bPhaseTimedOut), BossState.EncounterId);
        if (Observation != LastMechanic)
        {
            LastMechanic = Observation;
            UE_LOG(LogTemp, Display, TEXT("SOLO_RAID_MECHANIC %s"), *Observation);
        }
    }
    if (FPlatformTime::Seconds() >= NextReport)
    {
        NextReport = FPlatformTime::Seconds() + 5;
        UE_LOG(LogTemp, Display, TEXT("SOLO_RAID_STATUS Players=%d BossHP=%g State=%d"),
            Players, Health(Boss), int32(State->GetBattleSnapshot().BattleState));
    }
}

void URPGSoloRaidSubsystem::TickPlayer()
{
    bPlayerReady = false;
    auto* PC = Cast<ARPGPlayerController>(GetWorld()->GetFirstPlayerController());
    auto* Player = PC ? Cast<ARPGPlayer>(PC->GetPawn()) : nullptr;
    if (Player && LastPlayerPawn.Get() != Player)
    {
        LastPlayerPawn = Player;
        // ClientRestart resets the controller's input locks. Reopen our own
        // window after replacement possession so its counted locks match the PC.
        if (IsHumanLeader() && Panel && Panel->IsInViewport()) { ClosePanel(); TogglePanel(); }
    }
    auto* State = GetWorld()->GetGameState<ARPGBossBattleGameState>();
    auto* Boss = State ? State->GetBattleSnapshot().Boss.Get() : nullptr;
    auto* Weapon = Player ? Player->GetPlayerCombatComponent()->GetPlayerCurrentEquippedWeapon() : nullptr;
    const auto* Config = PC ? PC->GetInputConfig() : nullptr;
    auto* Subsystem = PC && PC->GetLocalPlayer() ?
        ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()) : nullptr;
    if (!IsValid(Boss) || !Weapon || !Config || !Subsystem ||
        !Subsystem->HasMappingContext(Weapon->PlayerWeaponData.WeaponInputMappingContext))
    { Activity = TEXT("준비 중"); ReleaseInput(); return; }
    bPlayerReady = true;
    if (!bReadyReported)
    {
        bReadyReported = true;
        UE_LOG(LogTemp, Display, TEXT("SOLO_RAID_CLIENT_READY Bot=%d Weapon=1 Mapping=1"), BotIndex);
        if (IsHumanLeader()) TogglePanel();
    }
    if (BotIndex < 0) { Activity = TEXT("직접 조작"); return; }
    if (Health(Player) <= 0 || Health(Boss) <= 0 ||
        State->GetBattleSnapshot().BattleState != EBossBattleState::InProgress)
    { Activity = Health(Player) <= 0 ? TEXT("전투 불능") : TEXT("전투 대기"); ReleaseInput(); return; }
    if (Policy == TEXT("hold") || (Mode != TEXT("fight") && Mode != TEXT("spread") && Mode != TEXT("gather")))
    { Activity = TEXT("지시 대기"); ReleaseInput(); return; }

    ARPGBaseCharacter* Target = RPGSoloRaidBot::SelectTarget(*Boss, *Player, Policy == TEXT("ignore-mechanic"));
    if (LastTarget.Get() != Target)
    {
        ReleaseInput(); LastTarget = Target;
        UE_LOG(LogTemp, Display, TEXT("SOLO_RAID_TARGET Slot=%d Target=%s HP=%g Policy=%s"),
            PlayerSlot, *GetNameSafe(Target), RPGSoloRaidBot::Health(Target), *Policy);
    }
    const auto* Move = Config->FindNativeInputActionByTag(RPGGameplayTags::InputTag_Move);
    const auto* Attack = Config->FindAbilityInputActionByTag(RPGGameplayTags::InputTag_LightAttack_GreatSword);
    if (!Move || !Attack) { Activity = TEXT("입력 매핑 대기"); ReleaseInput(); return; }
    if (Input.Get() != Subsystem || MoveAction.Get() != Move || AttackAction.Get() != Attack) ReleaseInput();
    Input = Subsystem; MoveAction = Move; AttackAction = Attack;
    const FVector Position = Player->GetActorLocation();
    const float Angle = PlayerSlot * 2.399963f;
    const FVector Offset(FMath::Cos(Angle), FMath::Sin(Angle), 0);
    const float Reach = Player->GetCapsuleComponent()->GetScaledCapsuleRadius() +
        (Target ? Target->GetCapsuleComponent()->GetScaledCapsuleRadius() : 40.f) + 15.f;
    FVector Goal = Target ? Target->GetActorLocation() : Position;
    float StopDistance = Reach;
    bool CanAttack = Mode == TEXT("fight") && Target;
    Activity = Target == Boss ? TEXT("보스 공격") : (Target ? TEXT("소환몹 공격") : TEXT("소환 준비 대기"));
    if (Mode == TEXT("spread"))
    { Goal = Boss->GetActorLocation() + Offset * 600; StopDistance = 50; CanAttack = false; Activity = TEXT("산개"); }
    else if (Mode == TEXT("gather"))
    {
        auto* Leader = RPGSoloRaidBot::FindLeader(*GetWorld(), LeaderPlayerId);
        Goal = Leader ? Leader->GetActorLocation() + Offset * 160 : Position;
        StopDistance = 50; CanAttack = false; Activity = Leader ? TEXT("내 캐릭터로 집결") : TEXT("파티장 복제 대기");
    }
    const float Now = GetWorld()->GetTimeSeconds();
    if (Policy != TEXT("no-avoid"))
    {
        const auto Dangers = RPGSoloRaidBot::ReadDangers(*Boss);
        FVector Escape;
        if (RPGSoloRaidBot::FindEscape(Dangers, Position, Player->GetCapsuleComponent()->GetScaledCapsuleRadius() + 15, Escape))
        { EvadeGoal = Escape; EvadeUntil = Now + .6; }
        if (EvadeUntil > Now)
        { Goal = EvadeGoal; StopDistance = 20; CanAttack = false; Activity = TEXT("위험 회피"); }
        else if (RPGSoloRaidBot::TouchesDanger(Dangers, Goal, 60))
        { Goal = Position; CanAttack = false; Activity = TEXT("위험 종료 대기"); }
    }
    const FVector Toward = (Goal - Position).GetSafeNormal2D();
    const float Distance = FVector::Dist2D(Goal, Position);
    const FVector Aim = Target && CanAttack ? (Target->GetActorLocation() - Position).GetSafeNormal2D() : Toward;
    if (!Aim.IsNearlyZero()) PC->SetControlRotation(FRotator(0, Aim.Rotation().Yaw, 0));
    const FRotationMatrix Basis(FRotator(0, PC->GetControlRotation().Yaw, 0));
    const FVector2D Value(FVector::DotProduct(Toward, Basis.GetUnitAxis(EAxis::Y)), FVector::DotProduct(Toward, Basis.GetUnitAxis(EAxis::X)));
    const bool NeedsFacing = CanAttack && FVector::DotProduct(Player->GetActorForwardVector(), Aim) < .8;
    FHitResult Ground;
    const FVector Step = Position + Toward * 100;
    FCollisionQueryParams GroundQuery(SCENE_QUERY_STAT(SoloRaidGround), false, Player);
    const bool HasGround = Toward.IsNearlyZero() || GetWorld()->LineTraceSingleByChannel(Ground,
        Step + FVector(0,0,50), Step - FVector(0,0,350), ECC_WorldStatic, GroundQuery);
    if ((Distance > StopDistance || NeedsFacing) && HasGround)
        Subsystem->StartContinuousInputInjectionForAction(Move, FInputActionValue(Value), {}, {});
    else Subsystem->StopContinuousInputInjectionForAction(Move);
    if (!HasGround && Distance > StopDistance) Activity = TEXT("낭떠러지 대기");
    const bool AttackNow = CanAttack && !NeedsFacing && Distance <= Reach + 35 &&
        FMath::Fmod(Now + PlayerSlot * .17f, 1.2f) < .25f;
    if (AttackNow) Subsystem->StartContinuousInputInjectionForAction(Attack, FInputActionValue(true), {}, {});
    else Subsystem->StopContinuousInputInjectionForAction(Attack);
    if (FPlatformTime::Seconds() >= NextReport)
    {
        NextReport = FPlatformTime::Seconds() + 2;
        UE_LOG(LogTemp, Display, TEXT("SOLO_RAID_BOT Index=%d Mode=%s Distance=%g HP=%g BossHP=%g Target=%s Activity=%s"),
            BotIndex, *Mode, Distance, Health(Player), Health(Boss), *GetNameSafe(Target), *Activity);
    }
}
