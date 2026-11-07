namespace ProjectRpg.Operations.Monitor;

public sealed record MonitorConfiguration(string Environment, string Service, Uri BackendOrigin, string MonitorToken,
    Uri WebhookUrl, string WebhookToken, string StateDirectory, long LogBudgetBytes, string BackupStatusPath = "")
{
    public static MonitorConfiguration FromEnvironment()
    {
        string Read(string name) => System.Environment.GetEnvironmentVariable("RPG_MONITOR_"+name) ?? "";
        if (!Uri.TryCreate(Read("BACKEND_ORIGIN"), UriKind.Absolute, out var backend)
            || !Uri.TryCreate(Read("WEBHOOK_URL"), UriKind.Absolute, out var webhook)
            || !long.TryParse(Read("LOG_BUDGET_BYTES") is { Length:>0 } size ? size : "1073741824", out long budget))
            throw new InvalidOperationException("Invalid monitor settings.");
        var value = new MonitorConfiguration(Read("ENVIRONMENT"),Read("SERVICE"),backend,Read("TOKEN"),webhook,
            Read("WEBHOOK_TOKEN"),Read("STATE_DIRECTORY"),budget,Read("BACKUP_STATUS_PATH"));
        value.Validate(); return value;
    }
    public void Validate()
    {
        static bool Label(string value) => value.Length is >=1 and <=64 && value.All(c=>char.IsAsciiLetterOrDigit(c)||c is '-' or '_');
        if (!Label(Environment)||!Label(Service)||BackendOrigin.Scheme!="https"||BackendOrigin.AbsolutePath!="/"
            || BackendOrigin.UserInfo.Length!=0||BackendOrigin.Query.Length!=0||BackendOrigin.Fragment.Length!=0
            || WebhookUrl.Scheme!="https"||WebhookUrl.UserInfo.Length!=0||WebhookUrl.Fragment.Length!=0
            || MonitorToken.Length is <32 or >256 || MonitorToken.Any(c=>!char.IsAsciiLetterOrDigit(c)&&c is not '-' and not '_')
            || WebhookToken.Length is <16 or >512 || WebhookToken.Any(c=>c<'!'||c>'~')
            || !Path.IsPathFullyQualified(StateDirectory)||LogBudgetBytes<1024*1024
            || BackupStatusPath.Length > 0 && (!Path.IsPathFullyQualified(BackupStatusPath) || BackupStatusPath.StartsWith("\\\\", StringComparison.Ordinal)))
            throw new InvalidOperationException("Invalid monitor settings.");
    }
}
