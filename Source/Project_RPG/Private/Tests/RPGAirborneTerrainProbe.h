#pragma once
#if !UE_BUILD_SHIPPING
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

namespace RPGAirborneTerrainProbe
{
inline bool PositionNPC(ACharacter& Player, ACharacter& NPC)
{
	FVector Position = Player.GetActorLocation() + Player.GetActorForwardVector() * 180.f;
	FHitResult Floor; FCollisionQueryParams Query; Query.AddIgnoredActor(&Player); Query.AddIgnoredActor(&NPC);
	if (!Player.GetWorld()->LineTraceSingleByChannel(Floor, Position + FVector(0,0,500), Position - FVector(0,0,700), ECC_Visibility, Query)) return false;
	Position.Z = Floor.ImpactPoint.Z + NPC.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f;
	return NPC.SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics);
}

/** Runtime-only replicated geometry; never edits the bossmap or any asset. */
inline bool Prepare(ACharacter& Player, ACharacter& NPC, TWeakObjectPtr<AActor>& OutFloor)
{
	if (!Player.HasAuthority()) return false;
	auto* World = Player.GetWorld();
	auto* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	auto* Security = Player.FindComponentByClass<URPGSecurityValidationComponent>();
	if (!World || !Mesh || !Security || Security->IsMovementAuthorizationActive()) return false;
	const FVector Surface = Player.GetActorLocation() - FVector(0,0,Player.GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) + FVector(0,0,3000);
	const FVector Forward = Player.GetActorForwardVector(); const float Yaw = Player.GetActorRotation().Yaw;
	const auto Box = [World,Mesh](const TCHAR* Name, FVector Location, FRotator Rotation, FVector Extent)
	{
		FActorSpawnParameters Params; Params.Name = FName(Name); Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		auto* Actor = World->SpawnActor<AStaticMeshActor>(Location, Rotation, Params); if (!Actor) return Actor;
		auto* Component = Actor->GetStaticMeshComponent(); Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(Mesh); Component->SetCollisionProfileName(TEXT("BlockAll"));
		Actor->SetActorScale3D(Extent / 50.f); Actor->SetReplicates(true); Actor->SetReplicateMovement(true);
		Component->SetIsReplicated(true); Actor->bAlwaysRelevant = true; Actor->ForceNetUpdate();
		return Actor;
	};
	auto* Floor = Box(TEXT("RPG_AirborneTerrain_Floor"), Surface + Forward*300 - FVector(0,0,25), FRotator(0,Yaw,0), FVector(1100,1000,25));
	// The incoming NPC stands in front, so the player is launched backwards.
	auto* Ramp = Box(TEXT("RPG_AirborneTerrain_Ramp"), Surface - Forward*300 + FVector(0,0,65), FRotator(20,Yaw+180.f,0), FVector(300,800,20));
	if (!Floor || !Ramp) return false;
	const auto Lease = Security->BeginMovementAuthorization(.5f, 3100.f, TEXT("AirborneTerrainFixture")); if (!Lease.IsValid()) return false;
	const bool Moved = Player.SetActorLocation(Player.GetActorLocation() + FVector(0,0,3000), false, nullptr, ETeleportType::TeleportPhysics);
	Player.GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	Security->EndMovementAuthorization(Lease, true);
	if (!Moved || !PositionNPC(Player, NPC)) return false;
	if (auto* Controller = Cast<APlayerController>(Player.GetController())) Controller->ClientSetLocation(Player.GetActorLocation(), Player.GetActorRotation());
	OutFloor = Floor;
	return true;
}
}
#endif
