using ProjectRpg.Backend.Application;

namespace ProjectRpg.Backend.Api;

public static class HealthApiEndpoints
{
    public static void MapHealthApi(this WebApplication app)
    {
        app.MapGet("/health", Liveness);
        app.MapGet("/health/live", Liveness);
        app.MapGet(
            "/health/ready",
            async (
                HttpContext context,
                IStorageReadinessProbe readinessProbe,
                CancellationToken cancellationToken) =>
            {
                context.Response.Headers.CacheControl = "no-store";
                StorageReadinessResult readiness =
                    await readinessProbe.CheckAsync(cancellationToken);
                var response = new
                {
                    status = readiness.IsReady ? "ready" : "not_ready",
                    storageProvider = readiness.Provider,
                    storageLatencyMilliseconds = readiness.LatencyMilliseconds,
                    failureCode = readiness.FailureCode
                };
                return Results.Json(
                    response,
                    statusCode: readiness.IsReady
                        ? StatusCodes.Status200OK
                        : StatusCodes.Status503ServiceUnavailable);
            });
    }

    private static IResult Liveness(HttpContext context)
    {
        context.Response.Headers.CacheControl = "no-store";
        return Results.Ok(new { status = "ok" });
    }
}
