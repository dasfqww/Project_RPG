using System.Net;
using System.Net.Sockets;

namespace ProjectRpg.Backend.HealthTests;

// Faults affect only connections made through this ephemeral loopback port.
internal sealed class FaultProxy(string targetHost, int targetPort) : IAsyncDisposable
{
    private readonly TcpListener _listener = new(IPAddress.Loopback, 0);
    private readonly CancellationTokenSource _stop = new();
    private readonly object _gate = new();
    private readonly HashSet<TcpClient> _clients = [];
    private readonly List<Task> _connections = [];
    private Task _acceptLoop = Task.CompletedTask;
    private ProxyMode _mode;

    public int Port => ((IPEndPoint)_listener.LocalEndpoint).Port;

    public void Start()
    {
        _listener.Start();
        _acceptLoop = AcceptAsync();
    }

    public void SetMode(ProxyMode mode, bool disconnectExisting = true)
    {
        lock (_gate)
        {
            _mode = mode;
            if (disconnectExisting)
            {
                foreach (TcpClient client in _clients)
                {
                    client.Dispose();
                }
            }
        }
    }

    private async Task AcceptAsync()
    {
        try
        {
            while (!_stop.IsCancellationRequested)
            {
                TcpClient client = await _listener.AcceptTcpClientAsync(_stop.Token);
                lock (_gate)
                {
                    _clients.Add(client);
                    _connections.RemoveAll(task => task.IsCompletedSuccessfully);
                    _connections.Add(ForwardAsync(client));
                }
            }
        }
        catch (OperationCanceledException) when (_stop.IsCancellationRequested) { }
    }

    private async Task ForwardAsync(TcpClient client)
    {
        using TcpClient upstream = new();
        using CancellationTokenSource connectionStop =
            CancellationTokenSource.CreateLinkedTokenSource(_stop.Token);
        try
        {
            ProxyMode mode;
            lock (_gate)
            {
                mode = _mode;
                _clients.Add(upstream);
            }
            if (mode == ProxyMode.Disconnected)
            {
                return;
            }
            if (mode == ProxyMode.Stalled)
            {
                // Accept TCP but never complete the PostgreSQL handshake.
                await client.GetStream().CopyToAsync(Stream.Null, connectionStop.Token);
                return;
            }

            await upstream.ConnectAsync(targetHost, targetPort, connectionStop.Token);
            Task outgoing = PumpAsync(client.GetStream(), upstream.GetStream(), connectionStop.Token);
            Task incoming = PumpAsync(upstream.GetStream(), client.GetStream(), connectionStop.Token);
            await Task.WhenAny(outgoing, incoming);
            await connectionStop.CancelAsync();
            client.Dispose();
            upstream.Dispose();
            await Task.WhenAll(outgoing, incoming);
        }
        catch (Exception exception) when (exception is IOException
            or SocketException or OperationCanceledException or ObjectDisposedException) { }
        finally
        {
            client.Dispose();
            lock (_gate)
            {
                _clients.Remove(client);
                _clients.Remove(upstream);
            }
        }
    }

    private async Task PumpAsync(Stream source, Stream destination, CancellationToken cancellationToken)
    {
        byte[] buffer = new byte[8192];
        int count;
        while ((count = await source.ReadAsync(buffer, cancellationToken)) > 0)
        {
            ProxyMode mode;
            lock (_gate)
            {
                mode = _mode;
            }
            if (mode == ProxyMode.Disconnected)
            {
                return;
            }
            if (mode == ProxyMode.Forwarding)
            {
                await destination.WriteAsync(buffer.AsMemory(0, count), cancellationToken);
            }
            // Stalled connections silently discard traffic in both directions.
        }
    }

    public async ValueTask DisposeAsync()
    {
        await _stop.CancelAsync();
        await _acceptLoop;
        SetMode(ProxyMode.Disconnected);
        _listener.Stop();
        await Task.WhenAll(_connections);
        _stop.Dispose();
    }
}

internal enum ProxyMode { Forwarding, Disconnected, Stalled }
