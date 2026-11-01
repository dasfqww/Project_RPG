#pragma once

#include "Progression/RPGEquipmentMutationTypes.h"

/** Exact narrow API JSON and the existing backend's canonical receipt identity. */
class PROJECT_RPG_API FRPGEquipmentMutationCodec final
{
public:
	static constexpr int32 MaximumBodyBytes = 16 * 1024;
	static FString Route(ERPGEquipmentMutationOperation Operation);
	static FString OperationName(ERPGEquipmentMutationOperation Operation);
	static bool Encode(const FRPGEquipmentMutationCommand& Command, FString& OutBody, FString& OutFingerprint);
	/** Journal recovery accepts only an exact canonical body, not arbitrary endpoints/JSON. */
	static bool Decode(const FGuid& CharacterId, ERPGEquipmentMutationOperation Operation,
		const FString& FrozenBody, FRPGEquipmentMutationCommand& OutCommand);
	static bool AcceptReceipt(const FRPGEquipmentMutationCommand& Command, const FString& Body,
		FRPGItemBackendCommitResult& OutReceipt);
	/** Only post-receipt, immutable-operand failures; auth/lease/unknown errors remain pending. */
	static bool IsDefinitiveRejection(const FRPGItemBackendHttpResponse& Response);
	static bool Sha256(const FString& Text, FString& OutDigest);
};
