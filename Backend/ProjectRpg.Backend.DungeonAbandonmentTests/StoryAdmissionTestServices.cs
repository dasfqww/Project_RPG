using Microsoft.Extensions.DependencyInjection;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Narrative;

// Existing ticket/recovery cases intentionally have no story requirements.
internal static class StoryAdmissionTestServices
{
    public static void AddUnrestricted(IServiceCollection services) => services.AddSingleton(
        new DungeonStoryAdmission(new DungeonContentCatalog(new DungeonContentOptions
        {
            Entries = [new() { DungeonId = "Test.Unrestricted", MapPackage = "/Game/Test", Difficulties = ["Normal"] }]
        }), NarrativeCatalog.Empty));
}
