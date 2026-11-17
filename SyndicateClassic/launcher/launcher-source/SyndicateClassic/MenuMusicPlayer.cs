// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;

namespace SyndicateClassic;

// Menu music without Windows Media Player. WPF's MediaElement needs Windows Media Player / the Media Feature Pack,
// which N editions and trimmed ("modded") Windows builds do not have ("Windows Media Player version 10 or later is
// required"). This plays the theme through winmm's waveOut, which every Windows has, and decodes it itself, so no
// Windows codec is needed either.
//
// The theme is embedded as SyndicateClassic.MenuThemeAdpcm (assets\menu-theme.tadp, made from menu-theme.mp3):
//   "TAD1", rate u32, channels u16 (2), block frames u32, frames u32, then per block: for each channel int16
//   predictor, byte step index, byte 0; then one byte per frame (left = low 4 bits, right = high 4 bits),
//   IMA ADPCM. Decoded once into memory (about 25 MB) and looped.
internal sealed class MenuMusicPlayer : IDisposable
{
    static readonly int[] Steps = { 7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767 };
    static readonly int[] IndexMove = { -1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8 };

    public static (short[] Pcm, int Rate) Decode(Stream input)
    {
        using var ms = new MemoryStream(); input.CopyTo(ms); var d = ms.GetBuffer(); int length = (int)ms.Length;
        if (length < 18 || d[0] != 'T' || d[1] != 'A' || d[2] != 'D' || d[3] != '1') throw new InvalidDataException("not a TAD1 file");
        int rate = BitConverter.ToInt32(d, 4), channels = BitConverter.ToUInt16(d, 8), block = BitConverter.ToInt32(d, 10), frames = BitConverter.ToInt32(d, 14);
        if (channels != 2 || rate < 8000 || block <= 0 || frames <= 0) throw new InvalidDataException("unsupported TAD1 file");
        var pcm = new short[frames * 2]; int pos = 18, o = 0;
        for (int b = 0; b < frames; b += block)
        {
            if (pos + 8 > length) break;
            int pl = BitConverter.ToInt16(d, pos), il = d[pos + 2], pr = BitConverter.ToInt16(d, pos + 4), ir = d[pos + 6]; pos += 8;
            int n = Math.Min(block, frames - b);
            for (int f = 0; f < n && pos + f < length; f++)
            {
                int x = d[pos + f];
                pcm[o++] = Step(ref pl, ref il, x & 15); pcm[o++] = Step(ref pr, ref ir, x >> 4);
            }
            pos += n;
        }
        return (pcm, rate);
    }
    static short Step(ref int p, ref int i, int nib)
    {
        int step = Steps[i], delta = step >> 3;
        if ((nib & 4) != 0) delta += step; if ((nib & 2) != 0) delta += step >> 1; if ((nib & 1) != 0) delta += step >> 2;
        p = (nib & 8) != 0 ? p - delta : p + delta; p = Math.Clamp(p, -32768, 32767);
        i = Math.Clamp(i + IndexMove[nib], 0, 88); return (short)p;
    }

    [StructLayout(LayoutKind.Sequential)] struct WaveFormat { public ushort Tag, Channels; public int Rate, BytesPerSecond; public ushort BlockAlign, Bits, Extra; }
    // WAVEHDR is written by hand at fixed offsets below, never via Marshal.SizeOf/StructureToPtr/OffsetOf on a
    // struct: the public build obfuscates (renames) struct fields, and those reflection APIs then throw
    // "Field passed in is not a marshaled member of the type" and kill the menu music. Layout (WAVEHDR):
    //   IntPtr Data; int Length; int Recorded; IntPtr User; int Flags; int Loops; IntPtr Next; IntPtr Reserved;
    [DllImport("winmm.dll")] static extern int waveOutOpen(out IntPtr device, int id, ref WaveFormat format, IntPtr callback, IntPtr instance, int flags);
    [DllImport("winmm.dll")] static extern int waveOutPrepareHeader(IntPtr device, IntPtr header, int size);
    [DllImport("winmm.dll")] static extern int waveOutUnprepareHeader(IntPtr device, IntPtr header, int size);
    [DllImport("winmm.dll")] static extern int waveOutWrite(IntPtr device, IntPtr header, int size);
    [DllImport("winmm.dll")] static extern int waveOutReset(IntPtr device);
    [DllImport("winmm.dll")] static extern int waveOutClose(IntPtr device);
    const int Done = 1, BufferFrames = 8000, BufferCount = 4; // 4 x 0.25 s at 32 kHz

    short[] _pcm; readonly int _rate; readonly IntPtr _device; readonly object _sync = new(); readonly IntPtr[] _headers = new IntPtr[BufferCount], _data = new IntPtr[BufferCount];
    readonly Thread _thread; volatile bool _stop; volatile bool _playing; volatile float _volume;
    // Sizes/offsets by hand (see WAVEHDR layout above): 4 pointer fields + 4 ints. Flags sits after Data, Length,
    // Recorded and User. Never reflection-based, so obfuscating the struct's field names cannot break it.
    int _cursor; readonly int _headerSize = IntPtr.Size * 4 + 16, _flagsOffset = 2 * IntPtr.Size + 8;

    public MenuMusicPlayer(short[] pcm, int rate)
    {
        _pcm = pcm; _rate = rate;
        var format = new WaveFormat { Tag = 1, Channels = 2, Rate = rate, Bits = 16, BlockAlign = 4, BytesPerSecond = rate * 4 };
        int result = waveOutOpen(out _device, -1 /*WAVE_MAPPER*/, ref format, IntPtr.Zero, IntPtr.Zero, 0);
        if (result != 0) throw new IOException("no sound output (waveOutOpen error " + result + ")");
        for (int i = 0; i < BufferCount; i++)
        {
            _data[i] = Marshal.AllocHGlobal(BufferFrames * 4);
            _headers[i] = Marshal.AllocHGlobal(_headerSize);
            // Build the WAVEHDR by hand (AllocHGlobal memory is uninitialised): zero it, then set Data, Length and Flags.
            for (int b = 0; b < _headerSize; b += 4) Marshal.WriteInt32(_headers[i], b, 0);
            Marshal.WriteIntPtr(_headers[i], 0, _data[i]);                 // Data (lpData)
            Marshal.WriteInt32(_headers[i], IntPtr.Size, BufferFrames * 4); // Length (dwBufferLength)
            Marshal.WriteInt32(_headers[i], _flagsOffset, Done);           // Flags (dwFlags)
            waveOutPrepareHeader(_device, _headers[i], _headerSize);
            Marshal.WriteInt32(_headers[i], _flagsOffset, Marshal.ReadInt32(_headers[i], _flagsOffset) | Done); // free to fill
        }
        _thread = new Thread(Run) { IsBackground = true, Name = "Syndicate menu music", Priority = ThreadPriority.AboveNormal };
        _thread.Start();
    }
    public float Volume { get => _volume; set => _volume = Math.Clamp(value, 0f, 1f); }
    public int Rate => _rate;
    // Loop: the track starts over at its end (one track only). Otherwise Ended turns true and silence plays until
    // the next track is loaded.
    public bool Loop { get; set; } = true;
    public volatile bool Ended;
    public double Duration { get { lock (_sync) return _pcm.Length / 2.0 / _rate; } }
    // Seconds into the track (what was last handed to Windows, about a second ahead of what is heard).
    public double Position { get { lock (_sync) return Math.Max(0, _cursor / 2 - BufferFrames * (BufferCount - 1)) / (double)_rate; } }
    public void Seek(double seconds)
    {
        lock (_sync) { _cursor = Math.Clamp((int)(seconds * _rate) * 2, 0, Math.Max(0, _pcm.Length - 2)); Ended = false; }
        if (_playing) try { waveOutReset(_device); } catch { } // drop what is queued so the jump is heard at once
    }
    // Another track (same sample rate: every track is made at 32 kHz).
    public void Load(short[] pcm, int rate)
    {
        if (rate != _rate) throw new InvalidDataException("track sample rate " + rate + " differs from " + _rate);
        lock (_sync) { _pcm = pcm; _cursor = 0; Ended = false; }
        if (_playing) try { waveOutReset(_device); } catch { }
    }
    public bool Playing { get => _playing; set { if (_playing == value) return; _playing = value; if (!value) try { waveOutReset(_device); } catch { } } }

    void Run()
    {
        var chunk = new short[BufferFrames * 2];
        while (!_stop)
        {
            bool wrote = false;
            if (_playing)
                for (int i = 0; i < BufferCount && _playing && !_stop; i++)
                {
                    if ((Marshal.ReadInt32(_headers[i], _flagsOffset) & Done) == 0) continue; // still queued
                    float v = _volume * _volume; // the slider feels even when the loudness follows its square
                    lock (_sync)
                        for (int s = 0; s < chunk.Length; s++)
                        {
                            if (_cursor >= _pcm.Length) { if (Loop) _cursor = 0; else { Ended = true; chunk[s] = 0; continue; } }
                            chunk[s] = (short)(_pcm[_cursor++] * v);
                        }
                    Marshal.Copy(chunk, 0, _data[i], chunk.Length);
                    Marshal.WriteInt32(_headers[i], _flagsOffset, Marshal.ReadInt32(_headers[i], _flagsOffset) & ~Done);
                    waveOutWrite(_device, _headers[i], _headerSize); wrote = true;
                }
            Thread.Sleep(wrote ? 5 : 25);
        }
    }
    public void Dispose()
    {
        _stop = true; _playing = false;
        try { _thread.Join(500); } catch { }
        try { waveOutReset(_device); } catch { }
        for (int i = 0; i < BufferCount; i++) { try { waveOutUnprepareHeader(_device, _headers[i], _headerSize); } catch { } }
        try { waveOutClose(_device); } catch { }
        for (int i = 0; i < BufferCount; i++) { Marshal.FreeHGlobal(_headers[i]); Marshal.FreeHGlobal(_data[i]); }
    }
}
