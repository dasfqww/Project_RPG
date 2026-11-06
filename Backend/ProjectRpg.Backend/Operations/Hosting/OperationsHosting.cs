using System.Security.Cryptography.X509Certificates;
using Microsoft.AspNetCore.DataProtection;
using Microsoft.AspNetCore.HttpOverrides;
using Microsoft.Extensions.DependencyInjection.Extensions;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Operations.Infrastructure.Authentication;
using ProjectRpg.Backend.Operations.Infrastructure.Diagnostics;
using ProjectRpg.Backend.Operations.Infrastructure.Reads;
using ProjectRpg.Backend.Operations.Web;
using ProjectRpg.Operations.Core.Access;
using ProjectRpg.Operations.Core.Characters;
using ProjectRpg.Operations.Core.Diagnostics;
using ProjectRpg.Operations.Core.Evidence;
using ProjectRpg.Operations.Core.Inventory;
using ProjectRpg.Operations.Core.Sessions;
using ProjectRpg.Operations.Core.Wallets;
using ProjectRpg.Operations.Core.Compensation;
using ProjectRpg.Backend.Operations.Infrastructure.Compensation;
using ProjectRpg.Backend.Operations.Restrictions;
using ProjectRpg.Operations.Core.Restrictions;

namespace ProjectRpg.Backend.Operations.Hosting;

public static class OperationsHosting
{
    public static IServiceCollection AddOperations(this IServiceCollection services, IConfiguration configuration, IHostEnvironment environment)
    {
        OperationsOptions options = configuration.GetSection("Operations").Get<OperationsOptions>() ?? new();
        options.Validate(environment);
        services.AddSingleton(options);
        ServiceControl.ServiceControlHosting.Register(services, configuration);
        if (!options.Enabled) return services;
        services.AddScoped<ProjectRpg.Operations.Core.ServiceControl.ChangeServiceControl>();
        services.AddScoped<ProjectRpg.Operations.Core.ServiceControl.ReadServiceControl>();
        services.AddScoped<OperationsServiceFacade>();
        Monitoring.OperationsMonitoring.Register(services, string.Equals(configuration["Storage:Provider"], "Postgres", StringComparison.OrdinalIgnoreCase));
        var compensationPolicy = new CompensationCatalog(options);
        services.AddSingleton<ICompensationPolicy>(compensationPolicy);
        services.TryAddSingleton<TimeProvider>(TimeProvider.System);
        services.AddHttpContextAccessor();
        IDataProtectionBuilder keys = services.AddDataProtection()
            .SetApplicationName("ProjectRpg.Operations." + options.EnvironmentId)
            .PersistKeysToFileSystem(new DirectoryInfo(options.KeyDirectory));
        if (options.KeyCertificatePath.Length > 0)
        {
            X509Certificate2 certificate = X509CertificateLoader.LoadPkcs12FromFile(options.KeyCertificatePath, options.KeyCertificatePassword);
            keys.ProtectKeysWithCertificate(certificate);
            services.AddSingleton(_ => certificate);
        }
        else if (OperatingSystem.IsWindows() && !options.AllowUnencryptedDevelopmentKeys) keys.ProtectKeysWithDpapi();
        if (string.Equals(configuration["Storage:Provider"], "Postgres", StringComparison.OrdinalIgnoreCase))
        {
            services.AddSingleton<IOperatorSessionRepository, PostgresOperatorSessionRepository>();
            services.AddSingleton<ICharacterReadStore, PostgresCharacterReadStore>();
            services.AddSingleton<IInventoryReadStore, PostgresInventoryReadStore>();
            services.AddSingleton<IWalletReadStore, PostgresWalletReadStore>();
            services.AddSingleton<ISessionEvidenceReadStore, PostgresSessionEvidenceReadStore>();
            services.AddSingleton<IEvidenceReadStore, PostgresEvidenceReadStore>();
            services.AddSingleton<PostgresCompensationStore>();
            services.AddSingleton<PostgresRestrictionStore>();
            services.AddSingleton<IAccountRestrictionReader>(p=>p.GetRequiredService<PostgresRestrictionStore>());
            services.AddSingleton<IAccountRestrictionCommitter>(p=>p.GetRequiredService<PostgresRestrictionStore>());
            services.AddSingleton<ICompensationCommitter>(p => p.GetRequiredService<PostgresCompensationStore>());
            services.AddSingleton<IOperationActionReader>(p => p.GetRequiredService<PostgresCompensationStore>());
        }
        else
        {
            if (!string.Equals(configuration["Storage:Provider"], "Memory", StringComparison.OrdinalIgnoreCase) || (!environment.IsDevelopment() && !environment.IsEnvironment("Testing")))
                throw new InvalidOperationException("Operations requires PostgreSQL outside Development/Testing.");
            services.AddSingleton(p => (InMemoryGameRepository)p.GetRequiredService<IGameRepository>());
            services.AddSingleton<IOperatorSessionRepository, MemoryOperatorSessionRepository>();
            services.AddSingleton<ICharacterReadStore, MemoryCharacterReadStore>();
            services.AddSingleton<IInventoryReadStore, MemoryInventoryReadStore>();
            services.AddSingleton<IWalletReadStore, MemoryWalletReadStore>();
            services.AddSingleton<ISessionEvidenceReadStore, MemorySessionEvidenceReadStore>();
            services.AddSingleton<IEvidenceReadStore, MemoryEvidenceReadStore>();
            services.AddSingleton<MemoryCompensationStore>();
            services.AddSingleton<MemoryRestrictionStore>();
            services.AddSingleton<IAccountRestrictionReader>(p=>p.GetRequiredService<MemoryRestrictionStore>());
            services.AddSingleton<IAccountRestrictionCommitter>(p=>p.GetRequiredService<MemoryRestrictionStore>());
            services.AddSingleton<ICompensationCommitter>(p => p.GetRequiredService<MemoryCompensationStore>());
            services.AddSingleton<IOperationActionReader>(p => p.GetRequiredService<MemoryCompensationStore>());
        }
        services.AddSingleton<OperationsTicketStore>();
        OperationsAuthentication.Register(services, options);
        services.AddScoped<IOperatorAccess, OperationsAccess>();
        services.AddScoped<SearchCharacters>();
        services.AddScoped<GetCharacterSummary>();
        services.AddScoped<GetInventoryPage>();
        services.AddScoped<GetWallet>();
        services.AddScoped<GetSessionEvidence>();
        services.AddScoped<SearchOperationLogs>();
        services.AddScoped<GetCharacterEvidence>();
        services.AddScoped<OperationsEvidenceFacade>();
        services.AddScoped<PreviewCompensation>();
        services.AddScoped<CommitCompensation>();
        services.AddScoped<ReadOperationActions>();
        services.AddSingleton<CompensationPreviewTokens>();
        services.AddScoped<OperationsCompensationFacade>();
        services.AddScoped<ChangeAccountRestriction>();
        services.AddScoped<ReadAccountRestriction>();
        services.AddScoped<OperationsRestrictionFacade>();
        services.AddSingleton<OperationsCursor>();
        services.AddScoped<OperationsReadFacade>();
        services.AddSingleton<OperationsRequestLimits>();
        services.AddSingleton<OperationsLogStore>();
        services.AddSingleton<OperationsEventRecorder>();
        services.AddSingleton<OperationsStorageTrace>();
        services.AddSingleton<ProjectRpg.Backend.Application.IDungeonSettlementObserver, OperationsSettlementObserver>();
        services.AddSingleton<IOperationLogReader, LocalJsonlOperationLogReader>();
        services.AddHostedService(p => p.GetRequiredService<OperationsLogStore>());
        services.AddHostedService<OperationsSessionMaintenance>();
        services.AddRazorPages(options =>
        {
            options.Conventions.AuthorizeFolder("/Ops", OperationsAuthentication.Policy);
            options.Conventions.AllowAnonymousToPage("/Ops/Login");
        }).AddApplicationPart(typeof(OperationsHosting).Assembly);
        services.Configure<ForwardedHeadersOptions>(forwarded =>
        {
            forwarded.ForwardedHeaders = ForwardedHeaders.XForwardedFor | ForwardedHeaders.XForwardedProto | ForwardedHeaders.XForwardedHost;
            forwarded.KnownProxies.Clear();
            forwarded.KnownNetworks.Clear();
            foreach (string proxy in options.KnownProxies) forwarded.KnownProxies.Add(System.Net.IPAddress.Parse(proxy));
            forwarded.ForwardLimit = 1;
        });
        // Framework request/auth logs can contain OAuth callback query strings or provider error bodies.
        services.AddLogging(logging =>
        {
            logging.AddFilter("Microsoft.AspNetCore.Hosting.Diagnostics", LogLevel.Warning);
            logging.AddFilter("Microsoft.AspNetCore.Authentication", LogLevel.None);
        });
        return services;
    }

    public static WebApplication UseOperations(this WebApplication app)
    {
        OperationsOptions options = app.Services.GetRequiredService<OperationsOptions>();
        if (!options.Enabled) return app;
        _ = app.Services.GetService<X509Certificate2>(); // Container owns/disposes the configured key protector certificate.
        app.UseRouting();
        if (options.KnownProxies.Length > 0)
            app.UseWhen(c => c.Request.Path.StartsWithSegments("/ops") || c.Request.Path.StartsWithSegments("/internal/operations"), branch => branch.UseForwardedHeaders());
        app.UseMiddleware<Monitoring.OperationsMonitorBoundary>();
        app.UseMiddleware<OperationsBoundary>();
        app.UseAuthentication();
        app.UseAuthorization();
        return app;
    }
    public static WebApplication MapOperations(this WebApplication app)
    {
        ServiceControl.ServiceControlHosting.MapPublic(app);
        if (!app.Services.GetRequiredService<OperationsOptions>().Enabled) return app;
        OperationsEndpoints.Map(app);
        Monitoring.OperationsMonitoring.Map(app);
        app.MapRazorPages();
        return app;
    }
}
