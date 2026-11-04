using System.Net;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.AspNetCore.Authentication.OAuth;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.WebUtilities;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Npgsql;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Backend.Operations.Infrastructure.Diagnostics;

internal sealed class MutableClock : TimeProvider
{
    internal DateTimeOffset Now = DateTimeOffset.UtcNow;
    public override DateTimeOffset GetUtcNow() => Now;
}

internal sealed class HostFixture : IAsyncDisposable
{
    internal WebApplication App { get; }
    internal HttpClient Client { get; }
    internal CookieContainer Cookies { get; }
    internal MutableClock Clock { get; }
    internal FakeGitHub GitHub { get; }
    internal string Origin { get; }
    internal string Root { get; }
    internal int Port { get; }
    internal const string Account = "76561198012345678";
    internal Guid CharacterId { get; private set; }
    internal Guid SessionId { get; private set; }
    private readonly X509Certificate2 certificate;
    private bool stopped;

    private HostFixture(WebApplication app, HttpClient client, CookieContainer cookies, MutableClock clock,
        FakeGitHub github, string root, int port, X509Certificate2 certificate)
    { App=app; Client=client; Cookies=cookies; Clock=clock; GitHub=github; Root=root; Port=port; Origin=$"https://127.0.0.1:{port}"; this.certificate=certificate; }

    internal static async Task<HostFixture> StartAsync(string root, string? postgres = null, int port = 0,
        CookieContainer? cookies = null, MutableClock? clock = null, string epoch = "operations-test-epoch-0001", bool enabled = true, bool compensation = false, string[]? allowedDefinitions = null,
        bool monitoring = false, Action<Microsoft.Extensions.DependencyInjection.IServiceCollection>? customize = null,
        Action<WebApplication>? customizeRoutes = null)
    {
        Directory.CreateDirectory(root);
        if (port == 0)
        {
            var listener = new System.Net.Sockets.TcpListener(IPAddress.Loopback, 0);
            listener.Start(); port=((IPEndPoint)listener.LocalEndpoint).Port; listener.Stop();
        }
        clock ??= new();
        using RSA rsa = RSA.Create(2048);
        CertificateRequest request = new("CN=localhost", rsa, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);
        using X509Certificate2 generated = request.CreateSelfSigned(DateTimeOffset.UtcNow.AddMinutes(-1), DateTimeOffset.UtcNow.AddHours(2));
        // Windows Schannel needs an accessible key container for server TLS.
        // DefaultKeySet creates a temporary container removed when the certificate is disposed.
        X509Certificate2 certificate = X509CertificateLoader.LoadPkcs12(generated.Export(X509ContentType.Pfx), null);
        WebApplicationBuilder builder = WebApplication.CreateBuilder(new WebApplicationOptions
        {
            EnvironmentName="Testing", ContentRootPath=root, ApplicationName=typeof(OperationsHosting).Assembly.FullName
        });
        builder.Logging.ClearProviders();
        builder.WebHost.ConfigureKestrel(server => server.Listen(IPAddress.Loopback, port, listen => listen.UseHttps(certificate)));
        builder.Configuration.AddInMemoryCollection(new Dictionary<string,string?>
        {
            ["Storage:Provider"]=postgres is null ? "Memory" : "Postgres",
            ["Operations:Enabled"]=enabled.ToString(), ["Operations:EnvironmentId"]="OpsTest",
            ["Operations:ManagementOrigin"]=$"https://127.0.0.1:{port}",
            ["Operations:ClientId"]="fixture", ["Operations:ClientSecret"]="fixture-only-no-real-secret",
            ["Operations:AllowedGitHubUserId"]="1", ["Operations:AuthEpoch"]=epoch,
            ["Operations:KeyDirectory"]=Path.Combine(root,"keys"), ["Operations:LogRoot"]=Path.Combine(root,"logs"),
            ["Operations:AllowUnencryptedDevelopmentKeys"]="true",
            ["Operations:Compensation:Enabled"]=compensation.ToString(),
            ["Operations:Monitor:Enabled"]=monitoring.ToString(), ["Operations:Monitor:Token"]=new string('m',48)
        });
        if (compensation) builder.Configuration.AddInMemoryCollection((allowedDefinitions ?? [
            ProjectRpg.Backend.Operations.Infrastructure.Compensation.CompensationCatalog.Key(FirstSliceEquipmentDefinitions.OreDefinitionName),
            ProjectRpg.Backend.Operations.Infrastructure.Compensation.CompensationCatalog.Key(FirstSliceEquipmentDefinitions.TokenDefinitionName)])
            .Select((value,index) => new KeyValuePair<string,string?>("Operations:Compensation:AllowedDefinitions:"+index,value)));
        builder.Services.AddSingleton<TimeProvider>(clock);
        builder.Services.AddSingleton(Microsoft.Extensions.Options.Options.Create(new AuthOptions { AdminToken="fixture-admin-token" }));
        builder.Services.Configure<SecurityTelemetryOptions>(_ => { });
        builder.Services.AddSingleton<AccessTokenService>();
        builder.Services.AddScoped<BearerAuthenticator>();
        if (postgres is null)
        {
            builder.Services.AddSingleton<InMemoryTransactionGate>();
            builder.Services.AddSingleton<InMemoryItemRepository>();
            builder.Services.AddSingleton<InMemoryEconomyRepository>();
            builder.Services.AddSingleton<IEconomyRepository>(p => p.GetRequiredService<InMemoryEconomyRepository>());
            builder.Services.AddSingleton<IItemRepository>(p => p.GetRequiredService<InMemoryItemRepository>());
            builder.Services.AddSingleton<IGameRepository>(p => new InMemoryGameRepository(
                p.GetRequiredService<InMemoryTransactionGate>(), p.GetRequiredService<InMemoryItemRepository>()));
            builder.Services.AddSingleton<IDungeonRewardCommitRepository,InMemoryDungeonRewardCommitRepository>();
        }
        else
        {
            builder.Services.AddSingleton(NpgsqlDataSource.Create(postgres));
            builder.Services.AddSingleton<PostgresItemRepository>();
            builder.Services.AddSingleton<IItemRepository>(p => p.GetRequiredService<PostgresItemRepository>());
            builder.Services.AddSingleton<PostgresEconomyRepository>();
            builder.Services.AddSingleton<IEconomyRepository>(p => p.GetRequiredService<PostgresEconomyRepository>());
            builder.Services.AddSingleton<IDungeonRewardCommitRepository,PostgresDungeonRewardCommitRepository>();
            builder.Services.AddSingleton<IGameRepository, PostgresGameRepository>();
        }
        FakeGitHub github = new();
        builder.Services.AddOperations(builder.Configuration,builder.Environment);
        builder.Services.AddSingleton<ProjectRpg.Backend.Application.IStorageReadinessProbe,ProjectRpg.Backend.Application.MemoryStorageReadinessProbe>();
        customize?.Invoke(builder.Services);
        if (enabled) builder.Services.Configure<OAuthOptions>(OperationsAuthentication.GitHubScheme, options => options.BackchannelHttpHandler=github);
        WebApplication app=builder.Build();
        await app.Services.GetRequiredService<IGameRepository>().EnsureCreatedAsync(default);
        app.UseOperations();
        app.UseMiddleware<ApiAuthenticationMiddleware>();
        app.MapOperations();
        ProjectRpg.Backend.Api.HealthApiEndpoints.MapHealthApi(app);
        customizeRoutes?.Invoke(app);
        app.MapGet("/api/protected", (HttpContext c) => Results.Json(new { kind=c.GetProjectRpgPrincipal().Kind.ToString() }));
        await app.StartAsync();
        cookies ??= new();
        HttpClient client = new(new HttpClientHandler { CookieContainer=cookies, AllowAutoRedirect=false,
            ServerCertificateCustomValidationCallback=HttpClientHandler.DangerousAcceptAnyServerCertificateValidator })
            { BaseAddress=new Uri($"https://127.0.0.1:{port}"), Timeout=TimeSpan.FromSeconds(15) };
        return new(app,client,cookies,clock,github,root,port,certificate);
    }

    internal async Task SeedAsync()
    {
        IGameRepository game=App.Services.GetRequiredService<IGameRepository>();
        await game.UpsertAccountAsync(new(Account,Account,false,false),Clock.Now,default);
        var c=await game.CreateCharacterAsync(Account,"Ops"+Guid.NewGuid().ToString("N")[..10],Clock.Now,default);
        CharacterId=c.CharacterId;
        await game.CreateCharacterAsync(Account,"Ops"+Guid.NewGuid().ToString("N")[..10],Clock.Now,default);
        var session=await game.CreateDungeonSessionAsync(Account,c.CharacterId,"training","Normal",Clock.Now,Clock.Now.AddSeconds(30),default);
        SessionId=session.DungeonSessionId;
        IEconomyRepository economy=App.Services.GetRequiredService<IEconomyRepository>();
        await economy.UpsertDefinitionAsync(new("OPS_COIN","검사용 재화",CurrencyScope.Character,long.MaxValue,true),Clock.Now,default);
        var result=await economy.CommitAsync(new(Guid.NewGuid(),c.CharacterId,session.DungeonSessionId,"Fixture","Fixture","Fixture",
            [new("OPS_COIN",9007199254740993L)]),new(c.CharacterId,c.RosterId,Account),Clock.Now,default);
        Check.That(result.Status==CurrencyTransactionStatus.Committed,"Fixture currency committed.");
        Clock.Now=Clock.Now.AddMinutes(2); // Expired stored leases: operations reads must not clean them.
    }

    internal async Task<string> CsrfAsync()
    {
        using var page=await Client.GetAsync("/ops/login");
        Check.That(page.StatusCode==HttpStatusCode.OK,"Login page is accessible over management HTTPS.");
        string html=await page.Content.ReadAsStringAsync();
        Match match=Regex.Match(html,"name=\"__RequestVerificationToken\"[^>]*value=\"([^\"]+)\"");
        Check.That(match.Success,"Razor login renders antiforgery token.");
        return WebUtility.HtmlDecode(match.Groups[1].Value);
    }
    internal async Task<HttpResponseMessage> LoginAsync(string? csrf=null)
    {
        csrf ??= await CsrfAsync();
        using var begin=await Client.PostAsync("/ops/auth/login",new FormUrlEncodedContent(new Dictionary<string,string>
            { ["__RequestVerificationToken"]=csrf, ["returnUrl"]="/ops" }));
        Check.That(begin.StatusCode==HttpStatusCode.Redirect && begin.Headers.Location?.Host=="github.com","OAuth challenge uses fixed GitHub authority.");
        var query=QueryHelpers.ParseQuery(begin.Headers.Location!.Query);
        Check.That(query["code_challenge_method"]=="S256" && query["code_challenge"].ToString().Length==43 && query.ContainsKey("state"),"OAuth PKCE/state present.");
        return await Client.GetAsync("/ops/auth/github-callback?code=fixture-code&state="+Uri.EscapeDataString(query["state"].ToString()));
    }
    internal async Task StopAsync()
    {
        if (stopped) return;
        stopped=true;
        await App.StopAsync();
        await App.DisposeAsync();
        Client.Dispose(); certificate.Dispose();
    }
    public async ValueTask DisposeAsync() => await StopAsync();

    internal sealed class FakeGitHub : HttpMessageHandler
    {
        internal ulong UserId=1;
        internal bool SawVerifier;
        internal bool Unavailable;
        protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request,CancellationToken ct)
        {
            if (request.RequestUri?.AbsoluteUri=="https://github.com/login/oauth/access_token")
            {
                string body=await request.Content!.ReadAsStringAsync(ct);
                SawVerifier=QueryHelpers.ParseQuery(body)["code_verifier"].ToString().Length>=43;
                return new(HttpStatusCode.OK) { Content=new StringContent("""{"access_token":"fixture-oauth-token-never-log","token_type":"bearer","scope":""}""",System.Text.Encoding.UTF8,"application/json") };
            }
            Check.That(request.RequestUri?.AbsoluteUri=="https://api.github.com/user","No arbitrary OAuth endpoint.");
            if (Unavailable) return new(HttpStatusCode.ServiceUnavailable);
            return new(HttpStatusCode.OK) { Content=new StringContent(JsonSerializer.Serialize(new { id=UserId,login="ignored-name" }),System.Text.Encoding.UTF8,"application/json") };
        }
    }
}
