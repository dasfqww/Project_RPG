#include "Security/RPGDungeonGameplayAuthority.h"

#include "Component/RPGAbilitySystemComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Player/RPGPlayerState.h"

bool IsDungeonGameplayRevoked(const AActor* Actor)
{
	const APawn* Pawn = Cast<APawn>(Actor);
	const AController* Controller = Cast<AController>(Actor);
	const ARPGPlayerState* State = Pawn ? Pawn->GetPlayerState<ARPGPlayerState>()
		: Controller ? Controller->GetPlayerState<ARPGPlayerState>() : Cast<ARPGPlayerState>(Actor);
	if (State && State->IsAuthenticatedIdentityRevoked()) return true;
	const auto* ASC = Pawn ? Pawn->FindComponentByClass<URPGAbilitySystemComponent>() : nullptr;
	return ASC && ASC->IsDungeonGameplayAuthorityRevoked();
}
