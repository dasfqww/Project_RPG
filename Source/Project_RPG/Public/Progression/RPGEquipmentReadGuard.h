#pragma once
#include "CoreMinimal.h"

/** A later read, command or publication supersedes an earlier external snapshot. */
class FRPGEquipmentReadGuard
{
public:
    uint64 Capture() { return ++Generation; }
    void Invalidate() { ++Generation; }
    bool Matches(uint64 Ticket) const { return Ticket!=0 && Ticket==Generation; }
    uint64 Current() const { return Generation; }
private:
    uint64 Generation=0;
};
