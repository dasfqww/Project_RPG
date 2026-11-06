using System.Net;
using System.Text.Json;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Options;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Operations.Core.ServiceControl;

// Explicit test-console mode only. Production hosts never map these controls.
internal static class ServiceStatusUEHost
{
    internal static async Task RunAsync()
    {
        string root=Environment.GetEnvironmentVariable("PROJECT_RPG_SERVICE_UE_ROOT")??throw new InvalidOperationException("Private runtime root required.");
        Directory.CreateDirectory(root);
        var builder=WebApplication.CreateBuilder(new WebApplicationOptions {EnvironmentName="Testing",ContentRootPath=root});
        builder.Logging.ClearProviders(); builder.Configuration["Storage:Provider"]="Memory";
        builder.Configuration["Operations:Enabled"]="false"; // Never inherit management keys/log roots from the invoking shell.
        builder.WebHost.ConfigureKestrel(k=>k.Listen(IPAddress.Loopback,0));
        builder.Services.AddSingleton<TimeProvider>(TimeProvider.System);
        builder.Services.AddSingleton<IGameRepository,InMemoryGameRepository>();
        builder.Services.AddSingleton(Options.Create(new AuthOptions {AdminToken="service-fixture-not-production"}));
        builder.Services.Configure<SecurityTelemetryOptions>(_=>{});builder.Services.AddSingleton<AccessTokenService>();builder.Services.AddScoped<BearerAuthenticator>();
        builder.Services.AddOperations(builder.Configuration,builder.Environment);
        await using var app=builder.Build();
        var game=app.Services.GetRequiredService<IGameRepository>();
        await game.UpsertAccountAsync(new("76561198012345678","fixture",false,false),DateTimeOffset.UtcNow,default);
        string key=Guid.NewGuid().ToString("N");int statusRequests=0,leakedHeaders=0,login503=0;bool error=false;
        app.Use(async(c,next)=>
        {
            if(c.Request.Path.StartsWithSegments("/verify")&&c.Request.Headers["X-Verification-Key"]!=key){c.Response.StatusCode=401;return;}
            if(c.Request.Path=="/api/service-status")
            {Interlocked.Increment(ref statusRequests);if(c.Request.Headers.Authorization.Count>0||c.Request.Headers.Cookie.Count>0)Interlocked.Increment(ref leakedHeaders);
             if(error){c.Response.StatusCode=500;return;}}
            await next();if(c.Request.Path=="/api/auth/steam-ticket"&&c.Response.StatusCode==503)Interlocked.Increment(ref login503);
        });
        app.UseMiddleware<ApiAuthenticationMiddleware>();app.MapOperations();
        app.MapPost("/api/auth/steam-ticket",async()=>{await game.UpsertAccountAsync(new("76561198012345678","fixture",false,false),DateTimeOffset.UtcNow,default);return Results.Ok();});
        app.MapPost("/verify/{command}",async(string command,IServiceControlReader reader,IServiceControlCommitter writer)=>
        {
            if(command=="error"){error=true;return Results.Ok();}
            if(command=="result")
            {
                bool passed=statusRequests==4&&leakedHeaders==0&&login503==1;
                var proof=new {passed,statusRequests,leakedCredentialHeaders=leakedHeaders,login503,realSteam=false,realNetworkBattle=false};
                await File.WriteAllTextAsync(Path.Combine(root,"BackendResult.json"),JsonSerializer.Serialize(proof));
                return passed?Results.Json(proof):Results.StatusCode(500);
            }
            if(command is not ("open" or "drain"))return Results.NotFound();
            var current=await reader.ReadAsync(default);error=false;
            var intent=new ServiceIntent(Guid.NewGuid(),current.Value.Revision,command=="drain"?"Drain":"Open","plain <script> notice",null,null,"native service status integration fixture");
            var committed=await writer.CommitAsync(new(intent,"github:fixture",Guid.NewGuid(),null),default);
            return committed.IsSuccess?Results.Ok():Results.StatusCode(500);
        });
        await app.StartAsync();
        string origin=app.Services.GetRequiredService<IServer>().Features.Get<IServerAddressesFeature>()!.Addresses.Single();
        await File.WriteAllTextAsync(Path.Combine(root,"Bootstrap.json"),JsonSerializer.Serialize(new {origin,key}));
        await app.WaitForShutdownAsync();
    }
}
