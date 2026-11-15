using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json.Nodes;
using Microsoft.Extensions.Configuration;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Riding;

static partial class Program
{
    static async Task ManifestExportCases(string directory)
    {
        var paths = Directory.GetFiles(directory, "*.json").Order(StringComparer.Ordinal).ToArray();
        Check(paths.Length == 4, "All four independently compiled Python fixtures present");
        foreach (var path in paths)
        {
            await Case("manifest_python_csharp_" + Path.GetFileNameWithoutExtension(path), () =>
            {
                var document = JsonNode.Parse(File.ReadAllText(path))!;
                Check(document["format"]!.GetValue<string>() == "ProjectRPG.Riding.Export/v1", "Versioned export format");
                var config = new ConfigurationBuilder().AddJsonStream(new MemoryStream(Encoding.UTF8.GetBytes(document["configuration"]!.ToJsonString()))).Build();
                var options = config.GetSection("Riding").Get<RidingOptions>()!;
                Check(!options.Enabled && options.BuildCatalog() is null, "Actual startup binder preserves disabled candidate");
                options.Enabled = true; // Private fixture only; never writes configuration.
                var culture = CultureInfo.CurrentCulture;
                try
                {
                    CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo("ar-SA");
                    var catalog = options.BuildCatalog()!;
                    var canonical = Encoding.UTF8.GetBytes(document["canonical"]!.GetValue<string>());
                    Check(canonical.SequenceEqual(catalog.CanonicalGameplayBytes()), "Python and production C# exact canonical bytes, including float32 bits/order");
                    Check(document["gameplayHash"]!.GetValue<string>() == catalog.GameplayHash
                        && Convert.ToHexStringLower(SHA256.HashData(canonical)) == catalog.GameplayHash, "Both runtimes match SHA256 and startup hash gate");
                    Array.Reverse(options.Mounts); Array.Reverse(options.RegistrationItems);
                    foreach (var mount in options.Mounts) Array.Reverse(mount.RegionTags);
                    Check(options.BuildCatalog()!.GameplayHash == catalog.GameplayHash, "Input ordering cannot change semantics");
                    options.Policy = options.Policy with { YawRate = options.Policy.YawRate == 1 ? 2 : 1 };
                    BadPreference(() => options.BuildCatalog(), RidingFailure.DefinitionMismatch);
                }
                finally { CultureInfo.CurrentCulture = culture; }
                return Task.CompletedTask;
            });
        }
        await Case("manifest_private_http_register_collection_preferences", async () =>
        {
            var document = JsonNode.Parse(File.ReadAllText(Path.Combine(directory, "standard.json")))!;
            var config = new ConfigurationBuilder().AddJsonStream(new MemoryStream(Encoding.UTF8.GetBytes(document["configuration"]!.ToJsonString()))).Build();
            var options = config.GetSection("Riding").Get<RidingOptions>()!;
            await using var off = await Fixture.Create(false, options);
            var no = await off.New();
            Check(!(await off.Get(no))["catalogReady"]!.GetValue<bool>(), "Unmodified export never enables live fixture");
            await off.Post(no, new { requestId = Guid.NewGuid(), itemId = no.Item.ItemId, expectedItemRevision = "1", catalogVersion = options.CatalogVersion }, 404);
            Check((await off.Items.FindAsync(no.Item.ItemId, default))!.State.Quantity == 2, "Disabled candidate cannot consume an item");
            options.Enabled = true;
            await using var f = await Fixture.Create(false, options);
            var person = await f.New();
            var registered = await f.Post(person, new { requestId = Guid.NewGuid(), itemId = person.Item.ItemId, expectedItemRevision = "1", catalogVersion = options.CatalogVersion });
            Check(registered["outcome"]!.GetValue<string>() == "Registered", "Exported actual definition triple authorizes standard item registration");
            var collection = await f.Get(person);
            Check(collection["gameplayHash"]!.GetValue<string>() == options.GameplayHash
                && collection["catalogVersion"]!.GetValue<int>() == 17 && collection["mounts"]!.AsArray().Count == 1, "Player collection exposes exported identity and owned mount");
            var defs = await f.Get(person, person.Root + "/registration-options");
            Check(defs["gameplayHash"]!.GetValue<string>() == options.GameplayHash, "Registration options uses same frozen catalog");
            await Put(f, person, PreferenceCommand(Guid.NewGuid()));
            var after = await f.Get(person);
            Check(after["preferencesRevision"]!.GetValue<string>() == "1", "Exported catalog accepts owned mount preferences");
            Check((await f.Items.FindAsync(person.Item.ItemId, default))!.State.Quantity == 1, "Only registration consumed one common item");
        });
    }
}
