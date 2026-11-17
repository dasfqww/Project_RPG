using ProjectRpg.Backend.ShopTests;
using Microsoft.Extensions.Configuration;
using ProjectRpg.Backend.Shop;

int assertions = 0;
void Check(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
    ++assertions;
}
try
{
    var production = new ConfigurationBuilder().SetBasePath(AppContext.BaseDirectory).AddJsonFile("appsettings.json").Build();
    Check(!ShopCatalogConfiguration.Load(production).Enabled, "production shop is disabled until configured");
    var development = new ConfigurationBuilder().SetBasePath(AppContext.BaseDirectory)
        .AddJsonFile("appsettings.json").AddJsonFile("appsettings.Development.json").Build();
    var catalog = ShopCatalogConfiguration.Load(development);
    Check(catalog.Enabled && catalog.CurrencyCode == "RosterGold" && catalog.SaleRules.Count == 1
        && catalog.Offers.Count == 1 && catalog.Offers[0].DefinitionName == "GameItem.Consume.Potion.Red.Large"
        && catalog.Offers[0].DefinitionVersion == 1 && catalog.Offers[0].MaximumStackSize == 10,
        "packaged development catalog uses the authored potion and junk");
    await CatalogContentCases.Run(catalog, Check);
    DomainCases.Run(Check);
    await StorageCases.Run(Check);
    await ApiCases.Run(Check);
    Console.WriteLine($"PASS Shop: {assertions} assertions.");
    return 0;
}
catch (Exception error) { Console.Error.WriteLine(error); return 1; }
