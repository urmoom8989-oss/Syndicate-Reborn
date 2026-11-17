// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Threading;
using System.Threading.Tasks;

namespace SyndicateClassic;

internal static class GameDownload
{
	internal const string Url = "https://dl.fortforge.co.uk/t/a9f0fabeb6f34c2ee154d6729415b0b8a79ff18fb883b678";

	internal static bool Ready(LauncherPaths p)
	{
		if (File.Exists(p.GameExe) && Directory.Exists(Path.Combine(p.GameInstallRoot, "Engine")))
		{
			return !File.Exists(Path.Combine(p.GameInstallRoot, ".tilted-install.pending")) && (p.GameInstallRoot != p.Root || !File.Exists(Path.Combine(p.Root,"Runtime","Downloads","install.pending")));
		}
		return false;
	}

	internal static async Task Install(LauncherPaths paths, IProgress<string> progress, CancellationToken token, IProgress<DownloadSample>? samples = null)
	{
		string work = Path.Combine(paths.Root, "Runtime", "Downloads");
		Directory.CreateDirectory(work);
		string archive = Path.Combine(work, "Fortnite-4.00.7z");
		string partial = archive + ".part";
		string validator = partial + ".etag";
		string extractor = Path.Combine(AppContext.BaseDirectory, "Tools", "7zip", "7z.exe");
		if (!File.Exists(extractor))
		{
			throw new IOException("The archive extractor is missing. Reinstall Syndicate Launcher.");
		}
		if (!File.Exists(archive))
		{
			long offset = (File.Exists(partial) ? new FileInfo(partial).Length : 0);
			if (!File.Exists(validator))
			{
				offset = 0L;
			}
			using HttpClient client = new HttpClient
			{
				Timeout = Timeout.InfiniteTimeSpan
			};
			using HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Get, "https://dl.fortforge.co.uk/t/a9f0fabeb6f34c2ee154d6729415b0b8a79ff18fb883b678");
			request.Headers.Range = new RangeHeaderValue(offset, null);
			if (offset > 0)
			{
				request.Headers.TryAddWithoutValidation("If-Range", File.ReadAllText(validator));
			}
			using CancellationTokenSource headerTimeout = CancellationTokenSource.CreateLinkedTokenSource(token);
			headerTimeout.CancelAfter(TimeSpan.FromSeconds(60.0));
			using HttpResponseMessage response = await client.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, headerTimeout.Token);
			if (!response.IsSuccessStatusCode)
			{
				throw new IOException($"Download server returned {response.StatusCode}. Try again later; the saved download is retained.");
			}
			if (response.StatusCode != HttpStatusCode.PartialContent)
			{
				offset = 0L;
			}
			else
			{
				ContentRangeHeaderValue? contentRange = response.Content.Headers.ContentRange;
				if (contentRange == null || contentRange.From != offset)
				{
					throw new IOException("Download server returned an unexpected byte range.");
				}
			}
			long? num = response.Content.Headers.ContentRange?.Length;
			long? obj;
			if (!num.HasValue)
			{
				long? contentLength = response.Content.Headers.ContentLength;
				if (contentLength.HasValue)
				{
					long valueOrDefault = contentLength.GetValueOrDefault();
					obj = offset + valueOrDefault;
				}
				else
				{
					obj = null;
				}
			}
			else
			{
				obj = num;
			}
			long? total = obj;
			string text = response.Headers.ETag?.ToString();
			if (text != null)
			{
				File.WriteAllText(validator, text);
			}
			else if (File.Exists(validator))
			{
				File.Delete(validator);
			}
			if (total.HasValue)
			{
				long valueOrDefault2 = total.GetValueOrDefault();
				if (new DriveInfo(Path.GetPathRoot(work)).AvailableFreeSpace < valueOrDefault2 - offset + 1073741824)
				{
					throw new IOException("There is not enough free disk space for the game download.");
				}
			}
			await using (Stream source = await response.Content.ReadAsStreamAsync(token))
			{
				await using FileStream output = new FileStream(partial, (offset == 0L) ? FileMode.Create : FileMode.Append, FileAccess.Write, FileShare.None, 1048576, useAsync: true);
				byte[] buffer = new byte[1048576];
				long received = offset;
				long previous = offset;
				Stopwatch timer = Stopwatch.StartNew();
				while (true)
				{
					using CancellationTokenSource readTimeout = CancellationTokenSource.CreateLinkedTokenSource(token);
					readTimeout.CancelAfter(TimeSpan.FromSeconds(60.0));
					int count = await source.ReadAsync(buffer, readTimeout.Token);
					if (count == 0)
					{
						break;
					}
					await output.WriteAsync(buffer.AsMemory(0, count), token);
					received += count;
					if (timer.ElapsedMilliseconds > 300)
					{
						progress.Report((total > 0) ? $"Downloading {(double)received * 100.0 / (double?)total:0.0}% — {(double)received / 1000000000.0:0.00} / {(double?)total / 1000000000.0:0.00} GB (click Cancel to pause)" : $"Downloading {(double)received / 1000000000.0:0.00} GB (click Cancel to pause)");
						samples?.Report(new DownloadSample(received, total, (double)(received - previous) / timer.Elapsed.TotalSeconds));
						previous = received;
						timer.Restart();
					}
					continue;
				}
				if (total.HasValue)
				{
					long valueOrDefault3 = total.GetValueOrDefault();
					if (received != valueOrDefault3)
					{
						throw new IOException("Download was interrupted. Click Download to resume.");
					}
				}
			}
			samples?.Report(new DownloadSample(new FileInfo(partial).Length, new FileInfo(partial).Length, 0.0));
			byte[] array = new byte[6];
			using (FileStream fileStream = File.OpenRead(partial))
			{
				fileStream.ReadExactly(array);
			}
			if (!array.SequenceEqual(new byte[6] { 55, 122, 188, 175, 39, 28 }))
			{
				File.Delete(partial);
				throw new IOException("The download did not contain a 7z archive.");
			}
			File.Move(partial, archive, overwrite: true);
		}
		progress.Report("Checking archive and available space…");
		string obj2 = await Run(extractor, new string[4] { "l", "-slt", "-ba", archive }, token);
		long num2 = 0L;
		foreach (string item in from x in obj2.Split('\n')
			select x.TrimEnd('\r'))
		{
			if (item.StartsWith("Path = "))
			{
				ValidateEntry(item.Substring(7));
			}
			if (item.StartsWith("Symbolic Link = ") || item.StartsWith("Hard Link = "))
			{
				throw new IOException("Archive contains unsupported links.");
			}
			if (item.StartsWith("Size = ") && long.TryParse(item.Substring(7), out var result))
			{
				num2 = checked(num2 + result);
			}
		}
		if (new DriveInfo(Path.GetPathRoot(work)).AvailableFreeSpace < num2 + 1073741824 || new DriveInfo(Path.GetPathRoot(paths.GameInstallRoot)).AvailableFreeSpace < num2 + 1073741824)
		{
			throw new IOException($"Free at least {(double)num2 / 1000000000.0 + 1.0:0.0} GB to extract the game. The download is saved.");
		}
		string stage = Path.Combine(work, "Extract-" + Guid.NewGuid().ToString("N"));
		Directory.CreateDirectory(stage);
		try
		{
			progress.Report("Extracting Fortnite… This may take several minutes.");
			await Run(extractor, new string[6]
			{
				"x",
				"-y",
				"-bso0",
				"-bsp0",
				"-o" + stage,
				archive
			}, token);
			string[] roots = (from x in Directory.EnumerateFiles(stage, "FortniteClient-Win64-Shipping.exe", SearchOption.AllDirectories)
				select Directory.GetParent(x)?.Parent?.Parent?.Parent?.FullName into x
				where x != null && Directory.Exists(Path.Combine(x, "Engine")) && File.Exists(Path.Combine(x, "FortniteGame", "Binaries", "Win64", "FortniteClient-Win64-Shipping.exe"))
				select x).ToArray();
			if (roots.Length != 1)
			{
				throw new IOException("The archive must contain one FortniteGame folder and its Engine folder.");
			}
			Directory.CreateDirectory(paths.GameInstallRoot);
			File.WriteAllText(Path.Combine(paths.GameInstallRoot, ".tilted-install.pending"), "Installation in progress");
			progress.Report("Installing game files…");
			await Task.Run(delegate
			{
				string[] array2 = new string[2] { "FortniteGame", "Engine" };
				foreach (string path in array2)
				{
					Merge(Path.Combine(roots[0], path), Path.Combine(paths.GameInstallRoot, path), token);
				}
			}, token);
			File.Delete(Path.Combine(paths.GameInstallRoot, ".tilted-install.pending"));
			File.Delete(archive);
			if (File.Exists(validator))
			{
				File.Delete(validator);
			}
		}
		finally
		{
			if (Directory.Exists(stage))
			{
				Directory.Delete(stage, recursive: true);
			}
		}
	}

	internal static void ValidateEntry(string entry)
	{
		if (string.IsNullOrWhiteSpace(entry) || Path.IsPathRooted(entry) || entry.Contains(':') || entry.Replace('\\', '/').Split('/').Any((string x) => x == ".."))
		{
			throw new IOException("Archive contains an unsafe file path.");
		}
	}

	private static void Merge(string source, string destination, CancellationToken token)
	{
		token.ThrowIfCancellationRequested();
		if (Directory.Exists(destination) && (File.GetAttributes(destination) & FileAttributes.ReparsePoint) != FileAttributes.None)
		{
			throw new IOException("Game destination cannot be a directory link.");
		}
		Directory.CreateDirectory(destination);
		foreach (string item in Directory.EnumerateFiles(source))
		{
			token.ThrowIfCancellationRequested();
			File.Move(item, Path.Combine(destination, Path.GetFileName(item)), overwrite: true);
		}
		foreach (string item2 in Directory.EnumerateDirectories(source))
		{
			Merge(item2, Path.Combine(destination, Path.GetFileName(item2)), token);
		}
	}

	private static async Task<string> Run(string executable, IEnumerable<string> arguments, CancellationToken token)
	{
		ProcessStartInfo processStartInfo = new ProcessStartInfo(executable)
		{
			UseShellExecute = false,
			CreateNoWindow = true,
			RedirectStandardOutput = true,
			RedirectStandardError = true
		};
		foreach (string argument in arguments)
		{
			processStartInfo.ArgumentList.Add(argument);
		}
		using Process process = Process.Start(processStartInfo) ?? throw new IOException("Cannot start the archive extractor.");
		Task<string> stdout = process.StandardOutput.ReadToEndAsync();
		Task<string> stderr = process.StandardError.ReadToEndAsync();
		try
		{
			await process.WaitForExitAsync(token);
		}
		catch
		{
			try
			{
				process.Kill(entireProcessTree: true);
				await process.WaitForExitAsync();
			}
			catch
			{
			}
			throw;
		}
		string text = await stdout;
		string text2 = await stderr;
		if (process.ExitCode != 0)
		{
			throw new IOException("Archive extraction/check failed. " + text2.Trim());
		}
		return text;
	}
}