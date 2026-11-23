#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

enum class ERPGTownChatRateChannel : uint8_t
{
    Region,
    Nearby
};

enum class ERPGTownChatRateReason : uint8_t
{
    Accepted,
    Duplicate,
    RateLimited
};

struct FRPGTownChatRateResult
{
    ERPGTownChatRateReason Reason;
    double RetryAfterSeconds = 0.0;
};

// One instance per server connection. The caller supplies a monotonic clock and
// comparison text normalized for whitespace, Unicode and case.
class FRPGTownChatRateLimiter
{
public:
    FRPGTownChatRateResult TryAccept(ERPGTownChatRateChannel Channel, std::u16string_view ComparisonText, double Now)
    {
        constexpr double Window = 10.0;
        constexpr double RegionRepeat = 20.0;
        constexpr int TotalLimit = 8;
        const bool bRegion = Channel == ERPGTownChatRateChannel::Region;
        const double RepeatWindow = bRegion ? RegionRepeat : 5.0;
        const int ChannelLimit = bRegion ? 3 : 6;

        std::erase_if(History, [Now](const FAcceptedMessage& Message)
        {
            return Now - Message.At >= RegionRepeat;
        });

        double RateWait = History.empty() ? 0.0 : 0.5 - (Now - History.back().At);
        double RepeatWait = 0.0;
        int TotalCount = 0;
        int ChannelCount = 0;

        // Count newest first so the limit-th send identifies when one slot reopens.
        for (auto It = History.rbegin(); It != History.rend(); ++It)
        {
            const double Age = Now - It->At;
            if (Age < Window && ++TotalCount == TotalLimit)
                RateWait = std::max(RateWait, Window - Age);

            if (It->Channel != Channel)
                continue;

            if (Age < Window && ++ChannelCount == ChannelLimit)
                RateWait = std::max(RateWait, Window - Age);
            if (Age < RepeatWindow && It->ComparisonText == ComparisonText)
                RepeatWait = std::max(RepeatWait, RepeatWindow - Age);
        }

        if (RepeatWait > 0.0 && RepeatWait >= RateWait)
            return {ERPGTownChatRateReason::Duplicate, RepeatWait};
        if (RateWait > 0.0)
            return {ERPGTownChatRateReason::RateLimited, RateWait};

        History.push_back({Now, Channel, std::u16string(ComparisonText)});
        return {ERPGTownChatRateReason::Accepted};
    }

private:
    struct FAcceptedMessage
    {
        double At;
        ERPGTownChatRateChannel Channel;
        std::u16string ComparisonText;
    };

    std::vector<FAcceptedMessage> History;
};
