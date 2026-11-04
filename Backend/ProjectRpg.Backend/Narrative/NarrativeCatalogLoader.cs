using System.Collections.Immutable;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace ProjectRpg.Backend.Narrative;

/// <summary>Startup-only content loading. Invalid authored data never becomes an empty catalog.</summary>
public static class NarrativeCatalogLoader
{
    public const int MaximumBytes = 1024 * 1024;
    private static readonly JsonSerializerOptions Options = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow,
        RespectRequiredConstructorParameters = true,
        RespectNullableAnnotations = true,
        MaxDepth = 16,
        Converters = { new JsonStringEnumConverter<QuestStatus>(allowIntegerValues: false),
            new JsonStringEnumConverter<DialogueEffect>(allowIntegerValues: false) }
    };

    public static NarrativeCatalog LoadConfigured(IConfiguration configuration, IHostEnvironment environment)
    {
        string? path = configuration["Narrative:CatalogPath"];
        bool development = configuration.GetValue<bool>("Narrative:EnableDevelopmentContent");
        bool hasPath = !string.IsNullOrWhiteSpace(path);
        if (hasPath && development) throw new NarrativeCatalogException("ambiguous_narrative_catalog");
        if (development && !environment.IsDevelopment() && !environment.IsEnvironment("Testing"))
            throw new NarrativeCatalogException("development_narrative_not_allowed");
        if (development) return NarrativeCatalog.Development;
        if (!hasPath) return NarrativeCatalog.Empty;
        try
        {
            using FileStream file = File.OpenRead(Path.GetFullPath(path!, environment.ContentRootPath));
            NarrativeCatalog catalog = Load(file);
            if (!environment.IsDevelopment() && !environment.IsEnvironment("Testing")
                && catalog.Quests.Keys.Concat(catalog.Chapters.Keys).Concat(catalog.Dialogues.Keys)
                    .Concat(catalog.Chapters.Values.SelectMany(c => c.StoryUnlocks))
                    .Any(id => id.StartsWith("DEV.", StringComparison.Ordinal)))
                throw new NarrativeCatalogException("development_narrative_not_allowed");
            return catalog;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException)
        { throw new NarrativeCatalogException("narrative_catalog_file_unavailable"); }
    }

    internal static NarrativeCatalog LoadDevelopment()
    {
        using Stream stream = typeof(NarrativeCatalogLoader).Assembly.GetManifestResourceStream(
            "ProjectRpg.Backend.Narrative.Content.development.json")
            ?? throw new NarrativeCatalogException("development_narrative_missing");
        return Load(stream);
    }

    public static NarrativeCatalog Load(Stream stream)
    {
        using MemoryStream buffer = new();
        byte[] block = new byte[4096];
        int count;
        while ((count = stream.Read(block, 0, Math.Min(block.Length, MaximumBytes + 1 - (int)buffer.Length))) > 0)
        {
            buffer.Write(block, 0, count);
            if (buffer.Length > MaximumBytes) throw new NarrativeCatalogException("narrative_catalog_too_large");
        }
        return Parse(buffer.ToArray());
    }

    public static NarrativeCatalog Parse(ReadOnlyMemory<byte> utf8)
    {
        if (utf8.Length > MaximumBytes) throw new NarrativeCatalogException("narrative_catalog_too_large");
        if (utf8.Span.StartsWith(new byte[] { 0xef, 0xbb, 0xbf })) utf8 = utf8[3..];
        try
        {
            using JsonDocument json = JsonDocument.Parse(utf8, new JsonDocumentOptions { MaxDepth = 16 });
            RejectDuplicates(json.RootElement);
            Document document = json.RootElement.Deserialize<Document>(Options)
                ?? throw new NarrativeCatalogException("invalid_narrative_catalog");
            if (document.SchemaVersion != 1) throw new NarrativeCatalogException("unsupported_narrative_catalog_schema");
            if (document.Quests.IsDefault || document.Chapters.IsDefault || document.Dialogues.IsDefault
                || document.Quests.Any(q => q is null) || document.Chapters.Any(c => c is null)
                || document.Dialogues.Any(d => d is null || d.Choices.IsDefault || d.Choices.Any(c => c is null)))
                throw new NarrativeCatalogException("invalid_narrative_catalog");
            NarrativeCatalog catalog = new(document.CatalogVersion, document.Quests, document.Chapters, document.Dialogues);
            ValidateResponseBudget(catalog);
            return catalog;
        }
        catch (JsonException error)
        { throw new NarrativeCatalogException("invalid_narrative_catalog_json", error.Path); }
        catch (NarrativeDefinitionException error)
        { throw new NarrativeCatalogException(error.Code); }
        catch (ArgumentException)
        { throw new NarrativeCatalogException("invalid_narrative_catalog_definition"); }
    }

    private static void RejectDuplicates(JsonElement element)
    {
        if (element.ValueKind == JsonValueKind.Object)
        {
            HashSet<string> keys = new(StringComparer.Ordinal);
            foreach (JsonProperty property in element.EnumerateObject())
            {
                if (!keys.Add(property.Name)) throw new NarrativeCatalogException("duplicate_narrative_json_property");
                RejectDuplicates(property.Value);
            }
        }
        else if (element.ValueKind == JsonValueKind.Array)
            foreach (JsonElement child in element.EnumerateArray()) RejectDuplicates(child);
    }

    private static void ValidateResponseBudget(NarrativeCatalog catalog)
    {
        // Overestimate a snapshot: every node/choice and every possible progress ID,
        // even though acceptance/status gates make many mutually exclusive at runtime.
        byte[] response = JsonSerializer.SerializeToUtf8Bytes(new
        {
            catalogVersion = catalog.Version, characterId = Guid.Empty, schemaVersion = 1,
            revision = long.MaxValue.ToString(System.Globalization.CultureInfo.InvariantCulture),
            quests = catalog.Quests.Values.Select(q => new { questId = q.Id, definitionVersion = q.Version,
                status = "ReadyToReport", completedObjectives = q.Objectives }),
            completedChapters = catalog.Chapters.Keys,
            storyUnlocks = catalog.Chapters.Values.SelectMany(c => c.StoryUnlocks).Distinct(),
            availableDialogues = catalog.Dialogues.Values.Select(d => new { id = d.Id, speaker = d.Speaker,
                text = d.Text, choices = d.Choices.Select(c => new { id = c.Id, text = c.Text }) })
        });
        if (response.Length > MaximumBytes) throw new NarrativeCatalogException("narrative_catalog_response_too_large");
    }

    private sealed record Document(int SchemaVersion, int CatalogVersion, ImmutableArray<QuestDefinition> Quests,
        ImmutableArray<ChapterDefinition> Chapters, ImmutableArray<DialogueDefinition> Dialogues);
}

public sealed class NarrativeCatalogException(string code, string? jsonPath = null)
    : Exception(jsonPath is null ? code : $"{code} at {jsonPath}")
{
    public string Code { get; } = code;
}
