using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;

namespace SyndicateLauncher.Studio
{
    /// <summary>
    /// Tracks every spawn/move/delete the user makes in the Studio and converts them to/from the
    /// tilted-custom-map-v1 JSON format the backend already understands.
    ///
    /// Lifecycle: created when the user starts a Studio session, records pipe actions as they happen,
    /// and can save/load .syndicate-map files. On match start, <see cref="ReplayAsync"/> spawns the
    /// recorded actors through the pipe so the map comes back.
    /// </summary>
    internal sealed class StudioMapSession
    {
        /// <summary>One actor the user placed or moved.</summary>
        internal sealed class MapActor
        {
            public int Id { get; set; }           // local edit id (monotonic)
            public string ClassName { get; set; }  // UE4 class (e.g. "PBWA_W1_Solid_C")
            public float X { get; set; }
            public float Y { get; set; }
            public float Z { get; set; }
            public float Yaw { get; set; }
            public uint? PipeIndex { get; set; }   // the actor's current pipe index (transient, changes each match)

            public MapActor(int id, string className, float x, float y, float z, float yaw)
            { Id = id; ClassName = className; X = x; Y = y; Z = z; Yaw = yaw; }
        }

        readonly List<MapActor> _actors = new();
        readonly Action<string> _log;
        int _nextId = 1;
        bool _dirty;

        public string? MapName { get; set; }
        public string? BaseSeason { get; set; }     // e.g. "4.5", "7.40", "11.00"
        public string? FilePath { get; private set; } // last saved/loaded path
        public bool IsDirty => _dirty;
        public int ActorCount => _actors.Count;
        public IReadOnlyList<MapActor> Actors => _actors;

        public StudioMapSession(Action<string> log)
        {
            _log = log;
        }

        // ---- recording edits -----------------------------------------------------------------------

        /// <summary>Record a spawn. Returns the local edit id.</summary>
        public int RecordSpawn(string className, float x, float y, float z, float yaw, uint? pipeIndex)
        {
            var id = _nextId++;
            _actors.Add(new MapActor(id, className, x, y, z, yaw) { PipeIndex = pipeIndex });
            _dirty = true;
            return id;
        }

        /// <summary>Record a move (by pipe index).</summary>
        public void RecordMove(uint pipeIndex, float x, float y, float z)
        {
            var actor = _actors.FirstOrDefault(a => a.PipeIndex == pipeIndex);
            if (actor != null) { actor.X = x; actor.Y = y; actor.Z = z; _dirty = true; }
        }

        /// <summary>Record a move with yaw (by pipe index).</summary>
        public void RecordMoveYaw(uint pipeIndex, float x, float y, float z, float yaw)
        {
            var actor = _actors.FirstOrDefault(a => a.PipeIndex == pipeIndex);
            if (actor != null) { actor.X = x; actor.Y = y; actor.Z = z; actor.Yaw = yaw; _dirty = true; }
        }

        /// <summary>Record a deletion (by pipe index).</summary>
        public bool RecordDelete(uint pipeIndex)
        {
            var actor = _actors.FirstOrDefault(a => a.PipeIndex == pipeIndex);
            if (actor == null) return false;
            _actors.Remove(actor);
            _dirty = true;
            return true;
        }

        /// <summary>Update the pipe index for an actor (after replay assigns new indices).</summary>
        public void UpdatePipeIndex(int editId, uint newPipeIndex)
        {
            var actor = _actors.FirstOrDefault(a => a.Id == editId);
            if (actor != null) actor.PipeIndex = newPipeIndex;
        }

        /// <summary>Clear all tracked actors.</summary>
        public void Clear()
        {
            _actors.Clear();
            _nextId = 1;
            _dirty = true;
        }

        // ---- save / load ---------------------------------------------------------------------------

        static readonly string MapsDir = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "ProjectTilted", "studio-maps");

        // Maps are saved as .syndicate-map. Maps saved by the old Tilted Studio (.tilted-map, same JSON) still load.
        const string MapExtension = ".syndicate-map";
        static readonly string[] MapPatterns = { "*.syndicate-map", "*.tilted-map" };

        /// <summary>List saved maps.</summary>
        public static List<(string Name, string Path, string? Season, int Actors, DateTime Modified)> ListMaps()
        {
            var result = new List<(string, string, string?, int, DateTime)>();
            if (!Directory.Exists(MapsDir)) return result;
            foreach (var file in MapPatterns.SelectMany(p => Directory.GetFiles(MapsDir, p)))
            {
                try
                {
                    using var doc = JsonDocument.Parse(File.ReadAllText(file));
                    var root = doc.RootElement;
                    var name = root.TryGetProperty("name", out var n) ? n.GetString() ?? Path.GetFileNameWithoutExtension(file) : Path.GetFileNameWithoutExtension(file);
                    var season = root.TryGetProperty("baseSeason", out var s) ? s.GetString() : null;
                    int actors = root.TryGetProperty("actors", out var a) && a.ValueKind == JsonValueKind.Array ? a.GetArrayLength() : 0;
                    result.Add((name, file, season, actors, File.GetLastWriteTime(file)));
                }
                catch { /* skip corrupt files */ }
            }
            return result.OrderByDescending(r => r.Item5).ToList();
        }

        /// <summary>Save the current session to a .syndicate-map file.</summary>
        public string Save(string? name = null)
        {
            if (name != null) MapName = name;
            if (string.IsNullOrWhiteSpace(MapName)) MapName = "Untitled";

            Directory.CreateDirectory(MapsDir);
            var slug = Slugify(MapName);
            var path = Path.Combine(MapsDir, slug + MapExtension);

            var json = ToJson();
            File.WriteAllText(path, json);
            FilePath = path;
            _dirty = false;
            _log($"Map saved: {MapName} ({_actors.Count} actors) → {Path.GetFileName(path)}");
            return path;
        }

        /// <summary>Load a saved map file into this session (replaces current state).</summary>
        public void Load(string filePath)
        {
            var text = File.ReadAllText(filePath);
            FromJson(text);
            FilePath = filePath;
            _dirty = false;
            _log($"Map loaded: {MapName ?? "?"} ({_actors.Count} actors) from {Path.GetFileName(filePath)}");
        }

        /// <summary>Replay all actors into the game via pipe spawns. Returns how many succeeded.</summary>
        public async Task<int> ReplayAsync(StudioPipeClient pipe)
        {
            if (_actors.Count == 0) return 0;
            int ok = 0;
            foreach (var actor in _actors)
            {
                try
                {
                    var idx = await pipe.SpawnActorAsync(actor.ClassName, actor.X, actor.Y, actor.Z);
                    if (idx != null)
                    {
                        actor.PipeIndex = idx.Value;
                        // Set the yaw if it isn't zero (SpawnActorAsync doesn't take rotation)
                        if (Math.Abs(actor.Yaw) > 0.1f)
                            await pipe.SetActorRotationAsync(idx.Value, 0, actor.Yaw, 0);
                        ok++;
                    }
                    else _log($"WARNING: couldn't spawn {actor.ClassName} (class may not exist in this build).");
                }
                catch (Exception ex)
                {
                    _log($"WARNING: spawn failed for {actor.ClassName} — {ex.Message}");
                }
            }
            _log($"Replayed {ok}/{_actors.Count} actors from \"{MapName ?? "map"}\".");
            return ok;
        }

        // ---- JSON (tilted-custom-map-v1 compatible) ------------------------------------------------

        string ToJson()
        {
            using var ms = new MemoryStream();
            using var w = new Utf8JsonWriter(ms, new JsonWriterOptions { Indented = true });
            w.WriteStartObject();
            w.WriteString("schema", "tilted-custom-map-v1");
            w.WriteString("name", MapName ?? "Untitled");
            if (BaseSeason != null) w.WriteString("baseSeason", BaseSeason);
            w.WriteString("savedAt", DateTime.UtcNow.ToString("o"));
            w.WriteString("savedBy", "SyndicateStudio");

            w.WriteStartArray("actors");
            foreach (var a in _actors)
            {
                w.WriteStartObject();
                w.WriteString("class", a.ClassName);
                w.WriteNumber("x", Math.Round(a.X, 2));
                w.WriteNumber("y", Math.Round(a.Y, 2));
                w.WriteNumber("z", Math.Round(a.Z, 2));
                w.WriteNumber("yaw", Math.Round(a.Yaw, 2));
                w.WriteEndObject();
            }
            w.WriteEndArray();

            w.WriteEndObject();
            w.Flush();
            return System.Text.Encoding.UTF8.GetString(ms.ToArray());
        }

        void FromJson(string json)
        {
            using var doc = JsonDocument.Parse(json);
            var root = doc.RootElement;

            MapName = root.TryGetProperty("name", out var n) ? n.GetString() : null;
            BaseSeason = root.TryGetProperty("baseSeason", out var s) ? s.GetString() : null;

            _actors.Clear();
            _nextId = 1;

            if (root.TryGetProperty("actors", out var actors) && actors.ValueKind == JsonValueKind.Array)
            {
                foreach (var a in actors.EnumerateArray())
                {
                    var cls = a.TryGetProperty("class", out var c) ? c.GetString() : null;
                    if (string.IsNullOrEmpty(cls)) continue;

                    float x = a.TryGetProperty("x", out var xv) ? (float)xv.GetDouble() : 0;
                    float y = a.TryGetProperty("y", out var yv) ? (float)yv.GetDouble() : 0;
                    float z = a.TryGetProperty("z", out var zv) ? (float)zv.GetDouble() : 0;
                    float yaw = a.TryGetProperty("yaw", out var yw) ? (float)yw.GetDouble() : 0;

                    _actors.Add(new MapActor(_nextId++, cls!, x, y, z, yaw));
                }
            }
        }

        // Also support loading the backend's tilted-custom-map-v1 format from Runtime/Server/custom-maps/
        public void LoadFromCustomMaps(string serverDir, string slug)
        {
            var path = Path.Combine(serverDir, "custom-maps", slug + ".json");
            if (!File.Exists(path)) throw new FileNotFoundException("No saved map: " + slug);
            var text = File.ReadAllText(path);
            FromJson(text);
            // Don't set FilePath — this isn't our .syndicate-map copy
            _dirty = false;
            _log($"Loaded custom map: {MapName ?? slug} ({_actors.Count} actors)");
        }

        // Save a copy to the backend's custom-maps folder and activate it for the current season.
        public void ExportToCustomMaps(string serverDir)
        {
            var dir = Path.Combine(serverDir, "custom-maps");
            Directory.CreateDirectory(dir);
            var slug = Slugify(MapName ?? "studio-map");
            var path = Path.Combine(dir, slug + ".json");
            File.WriteAllText(path, ToJson());
            _log($"Exported to custom-maps/{slug}.json");
        }

        static string Slugify(string s)
        {
            var sb = new System.Text.StringBuilder(s.Length);
            foreach (var c in s.ToLowerInvariant())
            {
                if (char.IsLetterOrDigit(c) || c == '.' || c == '-' || c == '_') sb.Append(c);
                else if (sb.Length > 0 && sb[^1] != '-') sb.Append('-');
            }
            var result = sb.ToString().Trim('-');
            return result.Length > 0 ? (result.Length > 64 ? result[..64] : result) : "custom-map";
        }
    }
}
