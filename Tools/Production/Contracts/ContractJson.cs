using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Schema;
using System.Text.Json.Serialization;

namespace ProjectRpg.Production.Contracts;

public static class ContractJson
{
    public static JsonSerializerOptions Options { get; } = CreateOptions();

    private static JsonSerializerOptions CreateOptions()
    {
        var options = new JsonSerializerOptions
        {
            PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower,
            UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow,
            RespectNullableAnnotations = true,
            MaxDepth = 32,
            WriteIndented = true
        };
        options.Converters.Add(new JsonStringEnumConverter(JsonNamingPolicy.SnakeCaseLower, allowIntegerValues: false));
        options.MakeReadOnly(populateMissingResolver: true);
        return options;
    }

    public static T Read<T>(string json) where T : class
    {
        if (Encoding.UTF8.GetByteCount(json) > 1_048_576)
            throw new ContractException("Document exceeds 1 MiB.");
        using var document = JsonDocument.Parse(json, new JsonDocumentOptions { MaxDepth = 32 });
        CheckDuplicates(document.RootElement);
        var value = JsonSerializer.Deserialize<T>(json, Options) ?? throw new ContractException("Document is null.");
        switch (value)
        {
            case TaskCard card: ContractRules.Validate(card); break;
            case WorkerResult result: ContractRules.Validate(result); break;
            case RuntimePolicy policy: ContractRules.Validate(policy); break;
            default: throw new ContractException("Unsupported contract type.");
        }
        return value;
    }

    public static string Write<T>(T value) => JsonSerializer.Serialize(value, Options);
    public static string Hash<T>(T value) => Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(Write(value))));
    public static string InputHash(TaskCard card) => Hash(card.Inputs);

    // Structural schemas come from the actual wire types. Cross-field rules live in ContractRules.
    public static string Schema<T>()
    {
        var schema = Options.GetJsonSchemaAsNode(typeof(T), new JsonSchemaExporterOptions { TreatNullObliviousAsNonNullable = true });
        schema["$schema"] = "https://json-schema.org/draft/2020-12/schema";
        return schema.ToJsonString(Options).Replace("\r\n", "\n") + "\n";
    }

    private static void CheckDuplicates(JsonElement element)
    {
        if (element.ValueKind == JsonValueKind.Object)
        {
            var names = new HashSet<string>(StringComparer.Ordinal);
            foreach (var property in element.EnumerateObject())
            {
                if (!names.Add(property.Name)) throw new ContractException("Duplicate JSON property.");
                CheckDuplicates(property.Value);
            }
        }
        else if (element.ValueKind == JsonValueKind.Array)
            foreach (var item in element.EnumerateArray()) CheckDuplicates(item);
    }
}

public sealed class ContractException(string message) : Exception(message);
