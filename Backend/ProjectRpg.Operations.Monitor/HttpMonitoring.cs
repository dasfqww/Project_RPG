using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;
using ProjectRpg.Operations.Core.Monitoring;

namespace ProjectRpg.Operations.Monitor;

public sealed class HttpOperationsProbe(HttpClient client, MonitorConfiguration settings, TimeProvider clock) : IMonitorProbe
{
    public async Task<MonitorObservation> ObserveAsync(CancellationToken ct)
    {
        using var timeout=CancellationTokenSource.CreateLinkedTokenSource(ct); timeout.CancelAfter(TimeSpan.FromSeconds(5));
        try
        {
            var live=Health("/health/live",timeout.Token); var ready=Health("/health/ready",timeout.Token); var status=Status(timeout.Token);
            await Task.WhenAll(live,ready,status);
            return new(clock.GetUtcNow(), live.Result&&ready.Result ? status.Result : null);
        }
        catch (Exception e) when (e is HttpRequestException or JsonException or IOException or OperationCanceledException)
        { ct.ThrowIfCancellationRequested(); return new(clock.GetUtcNow(),null); }
    }
    private async Task<bool> Health(string path,CancellationToken ct)
    {
        using var response=await client.GetAsync(new Uri(settings.BackendOrigin,path),HttpCompletionOption.ResponseHeadersRead,ct);
        return response.IsSuccessStatusCode;
    }
    private async Task<RuntimeStatus?> Status(CancellationToken ct)
    {
        using var request=new HttpRequestMessage(HttpMethod.Get,new Uri(settings.BackendOrigin,"/internal/operations/status"));
        request.Headers.Authorization=new AuthenticationHeaderValue("Bearer",settings.MonitorToken);
        using var response=await client.SendAsync(request,HttpCompletionOption.ResponseHeadersRead,ct);
        if (!response.IsSuccessStatusCode || response.Content.Headers.ContentLength>16*1024) return null;
        await using var stream=await response.Content.ReadAsStreamAsync(ct);
        byte[] bytes=new byte[16*1024+1]; int total=0,read;
        while(total<bytes.Length&&(read=await stream.ReadAsync(bytes.AsMemory(total),ct))>0) total+=read;
        return total==bytes.Length ? null : JsonSerializer.Deserialize<RuntimeStatus>(bytes.AsSpan(0,total),MonitoringJson.Options);
    }
}
public sealed class HttpWebhookDelivery(HttpClient client, MonitorConfiguration settings) : IAlertDelivery
{
    public async Task<bool> SendAsync(AlertEvent alert,CancellationToken ct)
    {
        using var timeout=CancellationTokenSource.CreateLinkedTokenSource(ct); timeout.CancelAfter(TimeSpan.FromSeconds(5));
        using var request=new HttpRequestMessage(HttpMethod.Post,settings.WebhookUrl)
        { Content=JsonContent.Create(alert,options:MonitoringJson.Options) };
        request.Headers.Authorization=new AuthenticationHeaderValue("Bearer",settings.WebhookToken);
        try
        {
            using var response=await client.SendAsync(request,HttpCompletionOption.ResponseHeadersRead,timeout.Token);
            return response.IsSuccessStatusCode;
        }
        catch(Exception e) when(e is HttpRequestException or IOException or OperationCanceledException)
        { ct.ThrowIfCancellationRequested(); return false; }
    }
}
