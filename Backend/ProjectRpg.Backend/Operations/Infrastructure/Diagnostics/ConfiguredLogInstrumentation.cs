using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Operations.Core.Diagnostics;

namespace ProjectRpg.Backend.Operations.Infrastructure.Diagnostics;

/// <summary>Reports host configuration only. Never infers historical collection or successful sink delivery.</summary>
public sealed class ConfiguredLogInstrumentation(OperationsOptions options, OperationsLogStore store, TimeProvider clock) : ILogInstrumentationReader
{
    private static IReadOnlyList<string> Values(params string[] values) => Array.AsReadOnly(values);
    private readonly IReadOnlyList<LogProducerConfiguration> producers = Array.AsReadOnly(new[]
    {
        new LogProducerConfiguration("game-http","게임 HTTP",options.GameApiDiagnosticsEnabled,
            Values("/api HTTP 요청 경계"),Values("game.http.started","game.http.completed")),
        new LogProducerConfiguration("game-transactions","게임 확정 거래",options.GameApiDiagnosticsEnabled,
            Values("일반 아이템 커밋","재화 커밋","원자 픽업","장비 제작","장비 분해","장비 강화","장비 전승"),
            Values("game.transaction.receipt.committed","game.transaction.receipt.replayed")),
        new LogProducerConfiguration("player-trade","개인 거래 명령 관측",options.GameApiDiagnosticsEnabled,
            Values("플레이어 거래의 저장된 명령 결과·완료 영수증 관측"),Values("game.trade.command.observed")),
        new LogProducerConfiguration("game-storage","게임 거래 저장소 호출",options.GameApiDiagnosticsEnabled,
            Values("일반 아이템 영수증 조회","일반 아이템 커밋 호출","재화 영수증 조회","재화 커밋 호출","던전 원자 픽업 호출"),
            Values("game.storage.returned","game.storage.failed","game.storage.canceled")),
        new LogProducerConfiguration("settlement","정산 worker 관측",true,
            Values("던전 보상 정산 시도·확정 지급 영수증"),Values("game.settlement.attempt.started","game.settlement.attempt.completed",
                "game.settlement.attempt.failed","game.settlement.attempt.requeued","game.reward.receipt.committed"))
    });
    public LogInstrumentationSnapshot Read() => new(clock.GetUtcNow(),options.EnvironmentId,options.SourceId,
        store.BootId,OperationsEventRecorder.BuildVersion,producers);
}
