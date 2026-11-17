// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;

namespace SyndicateClassic;

internal sealed class LocalRelay : IDisposable
{
	private readonly CancellationTokenSource _stop = new CancellationTokenSource();

	private readonly List<TcpListener> _listeners = new List<TcpListener>();

	private readonly ConcurrentDictionary<TcpClient, byte> _clients = new ConcurrentDictionary<TcpClient, byte>();

	private readonly Dictionary<IPEndPoint, UdpClient> _peers = new Dictionary<IPEndPoint, UdpClient>();

	private readonly Dictionary<IPEndPoint, DateTime> _seen = new Dictionary<IPEndPoint, DateTime>();

	// Windows reports an ICMP "port unreachable" for an earlier UDP send as an error on the NEXT receive
	// (WSAECONNRESET). After a match the game closes its old UDP port; the host's last packets forwarded to
	// it bounced, the relay's receive loop threw and stopped for good, and every following match timed out
	// for the guest ("Connection TIMED OUT" after 45 s). Turn that report off on every relay socket.
	private static void IgnoreConnectionReset(UdpClient client)
	{
		try
		{
			if (OperatingSystem.IsWindows()) client.Client.IOControl(unchecked((int)0x9800000C), new byte[4], null); // SIO_UDP_CONNRESET = false
		}
		catch { }
	}

	private readonly object _udpGate = new object();

	private UdpClient? _udp;

	public void Start(string host, int[] tcpPorts, int udpPort)
	{
		foreach (int port in tcpPorts)
		{
			TcpListener tcpListener = new TcpListener(IPAddress.Loopback, port);
			tcpListener.Server.ExclusiveAddressUse = true;
			tcpListener.Start();
			_listeners.Add(tcpListener);
			Accept(tcpListener, host, port);
		}
		_udp = new UdpClient(new IPEndPoint(IPAddress.Loopback, udpPort));
		IgnoreConnectionReset(_udp);
		ReceiveUdp(host, udpPort);
	}

	private async Task Accept(TcpListener listener, string host, int port)
	{
		try
		{
			while (!_stop.IsCancellationRequested)
			{
				TcpClient tcpClient = await listener.AcceptTcpClientAsync(_stop.Token);
				_clients[tcpClient] = 0;
				Pipe(tcpClient, host, port);
			}
		}
		catch (OperationCanceledException)
		{
		}
		catch (ObjectDisposedException)
		{
		}
		catch (SocketException)
		{
		}
	}

	private async Task Pipe(TcpClient client, string host, int port)
	{
		using (client)
		{
			using TcpClient remote = new TcpClient();
			_clients[remote] = 0;
			using CancellationTokenSource cancel = CancellationTokenSource.CreateLinkedTokenSource(_stop.Token);
			_ = 2;
			try
			{
				await remote.ConnectAsync(host, port, cancel.Token);
				Task a = client.GetStream().CopyToAsync(remote.GetStream(), cancel.Token);
				Task b = remote.GetStream().CopyToAsync(client.GetStream(), cancel.Token);
				await Task.WhenAny(a, b);
				cancel.Cancel();
				try
				{
					await Task.WhenAll(a, b);
				}
				catch (OperationCanceledException)
				{
				}
			}
			catch (Exception ex2) when (((ex2 is IOException || ex2 is SocketException || ex2 is OperationCanceledException || ex2 is ObjectDisposedException) ? 1 : 0) != 0)
			{
			}
			finally
			{
				_clients.TryRemove(client, out var value);
				_clients.TryRemove(remote, out value);
			}
		}
	}

	private async Task ReceiveUdp(string host, int port)
	{
		_ = 1;
		try
		{
			while (!_stop.IsCancellationRequested)
			{
				UdpReceiveResult udpReceiveResult;
				try
				{
					udpReceiveResult = await _udp.ReceiveAsync(_stop.Token);
				}
				catch (SocketException)
				{
					// A stale error from an old endpoint must never end the relay: keep listening.
					await Task.Delay(50, _stop.Token);
					continue;
				}
				UdpClient value;
				lock (_udpGate)
				{
					_seen[udpReceiveResult.RemoteEndPoint] = DateTime.UtcNow;
					if (!_peers.TryGetValue(udpReceiveResult.RemoteEndPoint, out value))
					{
						// A new game connection (each match uses a new local port): drop peers idle for 2 minutes.
						foreach (var old in new List<IPEndPoint>(_peers.Keys))
							if (!_seen.TryGetValue(old, out var last) || DateTime.UtcNow - last > TimeSpan.FromMinutes(2))
							{
								_peers[old].Dispose(); _peers.Remove(old); _seen.Remove(old);
							}
						value = new UdpClient();
						IgnoreConnectionReset(value);
						value.Connect(host, port);
						_peers.Add(udpReceiveResult.RemoteEndPoint, value);
						ReplyUdp(value, udpReceiveResult.RemoteEndPoint);
					}
				}
				try
				{
					await value.SendAsync(udpReceiveResult.Buffer, _stop.Token);
				}
				catch (SocketException)
				{
				}
			}
		}
		catch (Exception ex2) when (((ex2 is SocketException || ex2 is OperationCanceledException || ex2 is ObjectDisposedException) ? 1 : 0) != 0)
		{
		}
	}

	private async Task ReplyUdp(UdpClient remote, IPEndPoint local)
	{
		_ = 2;
		try
		{
			while (!_stop.IsCancellationRequested)
			{
				try
				{
					UdpReceiveResult udpReceiveResult = await remote.ReceiveAsync(_stop.Token);
					await _udp.SendAsync(udpReceiveResult.Buffer, local, _stop.Token);
				}
				catch (SocketException)
				{
					await Task.Delay(100, _stop.Token);
				}
				catch (ObjectDisposedException)
				{
					return; // this peer was dropped as idle
				}
			}
		}
		catch (Exception ex2) when (((ex2 is SocketException || ex2 is OperationCanceledException || ex2 is ObjectDisposedException) ? 1 : 0) != 0)
		{
		}
	}

	public void Dispose()
	{
		_stop.Cancel();
		foreach (TcpListener listener in _listeners)
		{
			listener.Stop();
		}
		foreach (TcpClient key in _clients.Keys)
		{
			key.Dispose();
		}
		_udp?.Dispose();
		lock (_udpGate)
		{
			foreach (UdpClient value in _peers.Values)
			{
				value.Dispose();
			}
			_peers.Clear();
			_seen.Clear();
		}
	}
}