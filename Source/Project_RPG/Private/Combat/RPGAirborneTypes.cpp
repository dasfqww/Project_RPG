#include "Combat/RPGAirborneTypes.h"
#include "NativeGameplayTags.h"
namespace
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Sequence, "Shared.Status.Control.KnockdownSequence");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Airborne, "Shared.Status.Control.Airborne");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Downed, "Shared.Status.Control.Downed");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GettingUp, "Shared.Status.Control.GettingUp");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Immunity, "Shared.Immunity.Control.Airborne");
}
FGameplayTag RPGAirborne::SequenceTag() { return TAG_Sequence; }
FGameplayTag RPGAirborne::AirborneTag() { return TAG_Airborne; }
FGameplayTag RPGAirborne::DownedTag() { return TAG_Downed; }
FGameplayTag RPGAirborne::GettingUpTag() { return TAG_GettingUp; }
FGameplayTag RPGAirborne::ImmunityTag() { return TAG_Immunity; }
