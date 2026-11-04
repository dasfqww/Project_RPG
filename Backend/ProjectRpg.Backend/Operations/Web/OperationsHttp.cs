using System.Diagnostics;
using System.Globalization;
using System.Text.Json;
using System.Text.Json.Serialization;
using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Operations.Core.Common;

namespace ProjectRpg.Backend.Operations.Web;

public static class OperationsHttp
{
    public const string RequestIdKey = "Operations.HttpRequestId";
    public static readonly JsonSerializerOptions Json = CreateJson();
    private static JsonSerializerOptions CreateJson()
    {
        JsonSerializerOptions options = new(JsonSerializerDefaults.Web) { UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow };
        options.Converters.Add(new JsonStringEnumConverter());
        options.Converters.Add(new LongStringConverter());
        options.Converters.Add(new UtcConverter());
        return options;
    }
    public static Guid RequestId(HttpContext context) => context.Items[RequestIdKey] is Guid id ? id : Guid.Empty;
    public static object Meta(HttpContext context, OperationsOptions options, TimeProvider clock) =>
        new { contractVersion = 1, environment = options.EnvironmentId, observedAtUtc = clock.GetUtcNow(),
            httpRequestId = RequestId(context), traceId = Activity.Current?.TraceId.ToString() };
    public static IResult Success(HttpContext context, object data, OperationsOptions options, TimeProvider clock) =>
        Results.Json(new { data, meta = Meta(context, options, clock) }, Json);
    public static int Status(Failure failure) => failure.Kind switch { FailureKind.InvalidInput => 400, FailureKind.Unauthenticated => 401,
        FailureKind.Forbidden => 403, FailureKind.NotFound => 404, FailureKind.CursorExpired => 410, _ => 503 };
    public static IResult Error(HttpContext context, Failure failure) => Error(context, Status(failure), failure.Code);
    public static IResult Error(HttpContext context, int status, string code) =>
        Results.Json(new { type = "about:blank", title = code, status, code, httpRequestId = RequestId(context),
            traceId = Activity.Current?.TraceId.ToString() }, Json, statusCode: status, contentType: "application/problem+json");
    public static Task WriteErrorAsync(HttpContext context, int status, string code) => Error(context, status, code).ExecuteAsync(context);

    private sealed class LongStringConverter : JsonConverter<long>
    {
        public override long Read(ref Utf8JsonReader reader, Type type, JsonSerializerOptions options) =>
            reader.TokenType == JsonTokenType.String && long.TryParse(reader.GetString(), NumberStyles.AllowLeadingSign,
                CultureInfo.InvariantCulture, out long value) ? value : throw new JsonException("Expected an int64 string.");
        public override void Write(Utf8JsonWriter writer, long value, JsonSerializerOptions options) =>
            writer.WriteStringValue(value.ToString(CultureInfo.InvariantCulture));
    }
    private sealed class UtcConverter : JsonConverter<DateTimeOffset>
    {
        public override DateTimeOffset Read(ref Utf8JsonReader reader, Type type, JsonSerializerOptions options) => reader.GetDateTimeOffset().ToUniversalTime();
        public override void Write(Utf8JsonWriter writer, DateTimeOffset value, JsonSerializerOptions options) =>
            writer.WriteStringValue(value.UtcDateTime.ToString("O", CultureInfo.InvariantCulture));
    }
}
