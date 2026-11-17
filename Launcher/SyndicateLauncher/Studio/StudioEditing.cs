using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Threading;

namespace SyndicateLauncher.Studio
{
    // Syndicate Studio editing in the free cam, like an engine editor viewport:
    // - Selection: left click in the game selects the piece under the cursor (Shift adds / removes), dragging from an
    //   empty spot draws a selection box and selects every piece inside it (Shift adds). SyndicateStudio.dll keeps the
    //   clicks from the game (no pickaxe swing) and hands them over with CMD_POLL_INPUT; the box is drawn on the gizmo
    //   overlay. The first selected piece carries the move arrows, the others get a marker and move along with it.
    // - Undo / Redo (toolbar, Ctrl+Z / Ctrl+Y): moves, rotations, deletes, placed pieces and house plots.
    // - Grid snapping (toolbar Grid: 512 = one build tile, 384 high) for arrow drags, nudges and placing; R turns the
    //   selection 90 degrees (a whole selected house turns around its middle).
    // - House Plots (Content Browser): copies of real Fortnite houses from the island: click a piece of a house,
    //   Select whole house, Save as House Plot. Drag a plot (or any tile) from the Content Browser onto the game to place it there.
    // Pieces are placed through the backend's map editor (the match server spawns them, so they are real for every
    // player and are saved with the map); moves and deletes go through the pipe and the match server like the
    // Details panel's Apply / Delete.
    internal sealed partial class StudioWindow
    {
        // ---- selection ---------------------------------------------------------------------------------
        readonly List<string> _sel = new(); // selected Outliner keys ("actor:<index>"); _selected is the one with the arrows

        static readonly CultureInfo Inv = CultureInfo.InvariantCulture;

        static bool TryF(Dictionary<string, string> p, string key, out float v) =>
            float.TryParse(p.GetValueOrDefault(key, ""), NumberStyles.Float, Inv, out v);

        static bool ActorPos(Item item, out float x, out float y, out float z, out float yaw)
        {
            y = z = yaw = 0;
            bool ok = TryF(item.Props, "x", out x) & TryF(item.Props, "y", out y) & TryF(item.Props, "z", out z);
            TryF(item.Props, "yaw", out yaw);
            return ok;
        }

        List<Item> SelectedActors() => _sel.Select(k => _items.TryGetValue(k, out var i) ? i : null).Where(i => i is { Kind: "actor" }).Select(i => i!).ToList();

        void SetSelection(IEnumerable<string> keys, bool add = false)
        {
            if (!add) _sel.Clear();
            foreach (var k in keys) if (!_sel.Contains(k)) _sel.Add(k);
            _selected = _sel.Count > 0 ? _sel[0] : null;
            SelectionChanged();
        }

        void ToggleSelection(string key)
        {
            if (!_sel.Remove(key)) _sel.Add(key);
            _selected = _sel.Count > 0 ? (_sel.Contains(_selected ?? "") ? _selected : _sel[0]) : null;
            SelectionChanged();
        }

        void ClearSelection()
        {
            if (_sel.Count == 0 && _selected == null) return;
            _sel.Clear(); _selected = null;
            SelectionChanged();
        }

        void SelectionChanged()
        {
            _outlinerSig = ""; // re-highlight the Outliner row
            RefreshOutliner();
            RefreshDetails(force: true);
            GizmoTick();
        }

        // An actor the click found but the Outliner hasn't listed (farther than its 200 m, or a newer piece)
        async Task<string?> EnsureActorItem(uint idx)
        {
            var key = "actor:" + idx;
            if (_items.ContainsKey(key)) return key;
            var pipe = _pipe;
            var a = pipe == null ? null : await pipe.GetActorPropsAsync(idx);
            if (a == null) return null;
            _items[key] = new Item(key, "actor", a.Name ?? a.ClassName ?? "?", "Scene actor", new Dictionary<string, string>
            {
                ["id"] = a.Index.ToString(), ["c"] = a.ClassName ?? "?",
                ["x"] = a.X.ToString("0.##", Inv), ["y"] = a.Y.ToString("0.##", Inv), ["z"] = a.Z.ToString("0.##", Inv),
                ["yaw"] = a.Yaw.ToString("0.##", Inv), ["pitch"] = a.Pitch.ToString("0.##", Inv), ["roll"] = a.Roll.ToString("0.##", Inv),
            });
            return key;
        }

        // Outliner refresh: keep selected actors that dropped out of the nearest-800 list, so the selection holds
        void KeepSelectedItems(Dictionary<string, Item> fresh)
        {
            foreach (var k in _sel)
                if (!fresh.ContainsKey(k) && _items.TryGetValue(k, out var keep) && keep.Kind == "actor") fresh[k] = keep;
        }

        void SetItemPos(string key, float x, float y, float z, float yaw)
        {
            if (!_items.TryGetValue(key, out var item)) return;
            item.Props["x"] = x.ToString("0.##", Inv); item.Props["y"] = y.ToString("0.##", Inv);
            item.Props["z"] = z.ToString("0.##", Inv); item.Props["yaw"] = yaw.ToString("0.##", Inv);
        }

        // ---- free cam input from the game ---------------------------------------------------------------
        DispatcherTimer? _inputTimer;
        bool _inputBusy, _inputOldDllLogged, _clickHelpShown;
        DateTime _freeCamOnAt = DateTime.MinValue;

        void InitEditing()
        {
            _inputTimer = new DispatcherTimer(DispatcherPriority.Input) { Interval = TimeSpan.FromMilliseconds(40) };
            _inputTimer.Tick += (_, __) => PollInput();
            _inputTimer.Start();
            Closed += (_, __) => _inputTimer.Stop();
            PreviewKeyDown += (_, e) =>
            {
                if (Keyboard.FocusedElement is TextBox || Keyboard.FocusedElement is PasswordBox) return;
                var key = e.Key == Key.System ? e.SystemKey : e.Key;
                if (HandleEditorKey(key, Keyboard.Modifiers.HasFlag(ModifierKeys.Control), Keyboard.Modifiers.HasFlag(ModifierKeys.Shift))) e.Handled = true;
            };
            _plots = LoadPlots();
        }

        async void PollInput()
        {
            var pipe = _pipe;
            if (pipe == null || !_freeCam) { _gizmo?.HideMarquee(); return; }
            if (_inputBusy) return;
            _inputBusy = true;
            try
            {
                var input = await pipe.PollInputAsync();
                if (input == null)
                {
                    if (pipe.LastFailure.Length == 0 && !_inputOldDllLogged)
                    {
                        _inputOldDllLogged = true;
                        Out("This SyndicateStudio.dll is too old for clicking and box-selecting in the free cam: run UPDATE FROM GIT, then restart the match.");
                    }
                    return;
                }
                if (!input.FreeCam && (DateTime.UtcNow - _freeCamOnAt).TotalSeconds > 2)
                {
                    // The game turned the free cam off by itself (new world, respawn)
                    _freeCam = false; if (_freeCamBtn != null) _freeCamBtn.Content = "Free cam: off";
                    _gizmo?.HideMarquee();
                    return;
                }
                if (input.Dragging) _gizmo?.ShowMarquee(input.Box.X0, input.Box.Y0, input.Box.X1, input.Box.Y1);
                else _gizmo?.HideMarquee();
                foreach (var e in input.Events) await HandleInput(pipe, e);
            }
            catch { }
            finally { _inputBusy = false; }
        }

        async Task HandleInput(StudioPipeClient pipe, StudioInputEvent e)
        {
            switch (e.Type)
            {
                case 1: // click: what is under the cursor
                {
                    var hit = await pipe.RaycastAsync(e.X0, e.Y0);
                    if (hit == null) { if (!e.Shift) ClearSelection(); return; }
                    var key = await EnsureActorItem(hit.Value);
                    if (key == null) return;
                    if (e.Shift || e.Ctrl) ToggleSelection(key); else SetSelection(new[] { key });
                    if (!_clickHelpShown) { _clickHelpShown = true; Out("Selected " + _items[key].Name + ". Drag the arrows to move it, R turns it, Delete removes it, Ctrl+Z undoes. Drag on an empty spot to box-select a whole house."); }
                    return;
                }
                case 2: // selection box
                    await BoxSelect(pipe, e.X0, e.Y0, e.X1, e.Y1, e.Shift || e.Ctrl, e.Alt);
                    return;
                case 3: // key pressed in the game
                    HandleEditorKey(KeyInterop.KeyFromVirtualKey(e.Vk), e.Ctrl, e.Shift);
                    return;
            }
        }

        // Things a box should not catch unless you hold Alt: the island itself, lights, volumes, players, effects
        static readonly string[] BoxSkip = { "Landscape", "Foliage", "Light", "Volume", "Sky", "Camera", "Controller", "PlayerState", "GameState", "GameMode", "HUD",
            "Info", "Manager", "Brush", "Emitter", "Sound", "Spawner", "Pawn", "Effect", "Decal", "Fog", "Reflection", "PostProcess", "Nav", "LevelBounds", "Water", "Note" };

        async Task BoxSelect(StudioPipeClient pipe, float x0, float y0, float x1, float y1, bool add, bool everything)
        {
            var candidates = _items.Values.Where(i => i.Kind == "actor" && ActorPos(i, out _, out _, out _, out _)
                && (everything || !BoxSkip.Any(s => i.Props.GetValueOrDefault("c", "").Contains(s, StringComparison.OrdinalIgnoreCase)))).ToList();
            if (candidates.Count == 0) { if (!add) ClearSelection(); Out("Nothing to select there yet (the Outliner lists pieces within 200 m of the camera)."); return; }
            var pts = new float[candidates.Count * 3];
            for (int i = 0; i < candidates.Count; i++) { ActorPos(candidates[i], out var x, out var y, out var z, out _); pts[i * 3] = x; pts[i * 3 + 1] = y; pts[i * 3 + 2] = z; }
            var r = await pipe.BatchProjectAsync(pts);
            if (r == null) { Out("Couldn't box-select this time" + PipeWhy() + "."); return; }
            var inside = new List<string>();
            for (int i = 0; i < candidates.Count && i < r.Length; i++)
                if (r[i].OnScreen && r[i].X >= x0 && r[i].X <= x1 && r[i].Y >= y0 && r[i].Y <= y1) inside.Add(candidates[i].Key);
            if (inside.Count == 0) { if (!add) ClearSelection(); return; }
            SetSelection(inside, add);
            Out($"Selected {_sel.Count} piece{(_sel.Count == 1 ? "" : "s")}. Drag the arrows to move them together, R turns them, Delete removes them.");
        }

        // Keys from the Studio window and from the game while the free cam is on
        bool HandleEditorKey(Key key, bool ctrl, bool shift)
        {
            switch (key)
            {
                case Key.Z when ctrl && shift: Redo(); return true;
                case Key.Z when ctrl: Undo(); return true;
                case Key.Y when ctrl: Redo(); return true;
                case Key.Delete: if (_sel.Count == 0) return false; DeleteSelection(); return true;
                case Key.R when !ctrl: if (_sel.Count == 0) return false; RotateSelection(shift ? -90 : 90); return true;
                case Key.Escape: if (_sel.Count == 0) return false; ClearSelection(); return true;
                case Key.Up: case Key.Down: case Key.Left: case Key.Right: case Key.PageUp: case Key.PageDown:
                    if (_sel.Count == 0 || !_freeCam) return false;
                    Nudge(key); return true;
            }
            return false;
        }

        // ---- grid --------------------------------------------------------------------------------------
        int GridStep => _layout.Grid is 64 or 128 or 256 or 512 ? _layout.Grid : 0;
        float GridZ => GridStep == 512 ? 384 : GridStep;
        static float SnapTo(float v, float step) => step > 0 ? MathF.Round(v / step) * step : v;
        static float SnapYaw(float yaw) => MathF.Round(yaw / 90f) * 90f;
        static bool IsBuildPiece(string cls) => cls.StartsWith("PBWA_", StringComparison.OrdinalIgnoreCase) || cls.StartsWith("PBW_", StringComparison.OrdinalIgnoreCase);
        static float NormYaw(float yaw) { yaw %= 360f; if (yaw > 180f) yaw -= 360f; if (yaw <= -180f) yaw += 360f; return yaw; }

        // ---- moves -------------------------------------------------------------------------------------
        sealed record Spot(float X, float Y, float Z, float Yaw);
        sealed record MoveStep(uint Index, string Key, string Name, Spot Before, Spot After);

        // Same path as the Details panel's Apply: the match server moves its piece, the pipe moves yours right away
        async Task<bool> MoveActorAsync(uint idx, string name, Spot to)
        {
            var pipe = _pipe;
            if (pipe == null) return false;
            DevCommand("actormove", name + "@" + FormattableString.Invariant($"{to.X:0.##},{to.Y:0.##},{to.Z:0.##},{to.Yaw:0.##}"), quiet: true);
            bool posOk = await pipe.SetActorPositionAsync(idx, to.X, to.Y, to.Z);
            bool rotOk = await pipe.SetActorRotationAsync(idx, 0, to.Yaw, 0);
            if (posOk && rotOk) { _mapSession.RecordMoveYaw(idx, to.X, to.Y, to.Z, to.Yaw); SetItemPos("actor:" + idx, to.X, to.Y, to.Z, to.Yaw); }
            return posOk && rotOk;
        }

        async Task<bool> ApplyMoves(IEnumerable<MoveStep> steps, bool forward)
        {
            bool all = true;
            foreach (var s in steps) all &= await MoveActorAsync(s.Index, s.Name, forward ? s.After : s.Before);
            UpdateMapLabel(); RefreshDetails(); GizmoTick();
            return all;
        }

        async void RunMoves(string label, List<MoveStep> steps)
        {
            if (steps.Count == 0) return;
            if (await ApplyMoves(steps, true)) PushEdit(new Edit(label, () => ApplyMoves(steps, false), () => ApplyMoves(steps, true)));
            else Out("ERROR: some pieces didn't move" + PipeWhy() + ".");
        }

        // Gizmo: the arrows moved the first selected piece; snap the move to the grid and bring the rest along
        Spot? _dragStart;
        void OnGizmoDragStarted(uint idx, float x, float y, float z)
        {
            float yaw = 0;
            if (_items.TryGetValue("actor:" + idx, out var it)) TryF(it.Props, "yaw", out yaw);
            _dragStart = new Spot(x, y, z, yaw);
        }

        void OnGizmoDragCompleted(uint idx, float x, float y, float z)
        {
            var start = _dragStart; _dragStart = null;
            var key = "actor:" + idx;
            if (start == null || !_items.TryGetValue(key, out var primary)) { _mapSession.RecordMove(idx, x, y, z); UpdateMapLabel(); return; }
            float dx = x - start.X, dy = y - start.Y, dz = z - start.Z;
            if (GridStep > 0) { dx = SnapTo(dx, GridStep); dy = SnapTo(dy, GridStep); dz = SnapTo(dz, GridZ); }
            var steps = new List<MoveStep> { new(idx, key, primary.Name, start, new Spot(start.X + dx, start.Y + dy, start.Z + dz, start.Yaw)) };
            foreach (var other in SelectedActors())
            {
                if (other.Key == key || !uint.TryParse(other.Props.GetValueOrDefault("id"), out var oi) || !ActorPos(other, out var ox, out var oy, out var oz, out var oyaw)) continue;
                steps.Add(new MoveStep(oi, other.Key, other.Name, new Spot(ox, oy, oz, oyaw), new Spot(ox + dx, oy + dy, oz + dz, oyaw)));
            }
            if (dx == 0 && dy == 0 && dz == 0) { _ = MoveActorAsync(idx, primary.Name, start); return; } // snapped back to where it was
            RunMoves(steps.Count > 1 ? $"move {steps.Count} pieces" : "move " + primary.Name, steps);
        }

        // Arrow keys move the selection one grid step along the camera's nearest axis; PageUp / PageDown up and down
        async void Nudge(Key key)
        {
            var pipe = _pipe;
            if (pipe == null) return;
            float step = GridStep > 0 ? GridStep : 50, stepZ = GridStep > 0 ? GridZ : 50;
            float dx = 0, dy = 0, dz = 0;
            if (key is Key.PageUp or Key.PageDown) dz = key == Key.PageUp ? stepZ : -stepZ;
            else
            {
                var cam = await pipe.GetCameraAsync();
                double yaw = (cam?.Yaw ?? 0) * Math.PI / 180;
                // Forward along whichever world axis the camera faces most
                float fx = Math.Abs(Math.Cos(yaw)) >= Math.Abs(Math.Sin(yaw)) ? MathF.Sign((float)Math.Cos(yaw)) : 0;
                float fy = fx == 0 ? MathF.Sign((float)Math.Sin(yaw)) : 0;
                (float rx, float ry) = (-fy, fx); // right of forward
                switch (key)
                {
                    case Key.Up: dx = fx * step; dy = fy * step; break;
                    case Key.Down: dx = -fx * step; dy = -fy * step; break;
                    case Key.Right: dx = rx * step; dy = ry * step; break;
                    case Key.Left: dx = -rx * step; dy = -ry * step; break;
                }
            }
            var steps = new List<MoveStep>();
            foreach (var a in SelectedActors())
                if (uint.TryParse(a.Props.GetValueOrDefault("id"), out var i) && ActorPos(a, out var x, out var y, out var z, out var yw))
                    steps.Add(new MoveStep(i, a.Key, a.Name, new Spot(x, y, z, yw), new Spot(x + dx, y + dy, z + dz, yw)));
            RunMoves(steps.Count > 1 ? $"nudge {steps.Count} pieces" : "nudge", steps);
        }

        // R: turn 90 degrees. One piece turns in place; several turn together around their middle (on the grid)
        void RotateSelection(float by)
        {
            var sel = SelectedActors().Where(a => uint.TryParse(a.Props.GetValueOrDefault("id"), out _) && ActorPos(a, out _, out _, out _, out _)).ToList();
            if (sel.Count == 0) { Out("Select a piece first (click it in the game with the free cam, or in the Outliner)."); return; }
            float cx = 0, cy = 0;
            foreach (var a in sel) { ActorPos(a, out var x, out var y, out _, out _); cx += x; cy += y; }
            cx /= sel.Count; cy /= sel.Count;
            if (GridStep > 0 && sel.Count > 1) { cx = SnapTo(cx, GridStep); cy = SnapTo(cy, GridStep); }
            double r = by * Math.PI / 180;
            float c = (float)Math.Round(Math.Cos(r), 6), s = (float)Math.Round(Math.Sin(r), 6);
            var steps = new List<MoveStep>();
            foreach (var a in sel)
            {
                uint.TryParse(a.Props.GetValueOrDefault("id"), out var i);
                ActorPos(a, out var x, out var y, out var z, out var yaw);
                float ox = x - cx, oy = y - cy;
                float nx = sel.Count > 1 ? cx + ox * c - oy * s : x, ny = sel.Count > 1 ? cy + ox * s + oy * c : y;
                float nyaw = NormYaw(GridStep > 0 ? SnapYaw(yaw + by) : yaw + by);
                steps.Add(new MoveStep(i, a.Key, a.Name, new Spot(x, y, z, yaw), new Spot(nx, ny, z, nyaw)));
            }
            RunMoves(sel.Count > 1 ? $"turn {sel.Count} pieces" : "turn " + sel[0].Name, steps);
        }

        // ---- delete ------------------------------------------------------------------------------------
        sealed record Gone(uint Index, string Name, string Class, Spot At, Look? Look);

        async void DeleteSelection()
        {
            var pipe = _pipe;
            var sel = SelectedActors();
            if (sel.Count == 0) return;
            if (pipe == null) { Out("Deleting needs the Studio pipe: let your game dock first."); return; }
            var gone = new List<Gone>();
            // Read how each piece looks first, so Undo brings back the same piece (mesh, textures, tilt, scale)
            var looks = await LooksFor(sel);
            foreach (var a in sel)
            {
                if (!uint.TryParse(a.Props.GetValueOrDefault("id"), out var idx) || !ActorPos(a, out var x, out var y, out var z, out var yaw)) continue;
                DevCommand("actordel", a.Name, quiet: true); // the match server owns map pieces: remove it there too
                if (await pipe.DeleteActorAsync(idx))
                {
                    _mapSession.RecordDelete(idx);
                    gone.Add(new Gone(idx, a.Name, a.Props.GetValueOrDefault("c", ""), new Spot(x, y, z, yaw), LookOf(a, looks)));
                    _items.Remove(a.Key);
                }
            }
            ClearSelection(); UpdateMapLabel();
            if (gone.Count == 0) { Out("ERROR: the game couldn't delete that" + PipeWhy() + "."); return; }
            Out($"Deleted {gone.Count} piece{(gone.Count == 1 ? "" : "s")}. Ctrl+Z brings {(gone.Count == 1 ? "it" : "them")} back.");
            // Undo puts copies back through the map editor (the match server spawns them); Redo removes those copies
            var restored = new List<Placed>();
            PushEdit(new Edit($"delete {gone.Count} piece{(gone.Count == 1 ? "" : "s")}",
                async () =>
                {
                    restored.Clear();
                    foreach (var g in gone.Where(g => g.Class.Length > 0)) restored.Add(new Placed(_placeId++, ServerClass(g.Class), g.At, g.Look));
                    return restored.Count > 0 && await SpawnPlaced(restored);
                },
                () => RemovePlaced(restored)));
        }

        // Pipe class names are the live class (PBWA_W1_Solid_C); the map editor takes the same name
        static string ServerClass(string cls) => cls;

        // ---- placing through the map editor (the match server spawns it for everyone) ----------------------
        sealed record Placed(int Id, string Class, Spot At, Look? Look = null);

        // How a copied piece looks: the rest of its rotation, its scale, and (building pieces) the mesh, the four
        // texture sets, the alternate mesh and mirroring it had. Without these the match server spawns the class
        // default, which is why copied houses came out with other textures and pieces turned the wrong way.
        sealed record Look(float Pitch, float Roll, float Sx, float Sy, float Sz, string? Mesh, string[]? Tex, int Alt, int Mirror);

        static string SpawnOp(Placed p)
        {
            var op = FormattableString.Invariant(
                $"{{\"op\":\"spawn\",\"id\":{p.Id},\"class\":{Json(p.Class)},\"x\":{p.At.X:0.#},\"y\":{p.At.Y:0.#},\"z\":{p.At.Z:0.#},\"yaw\":{NormYaw(p.At.Yaw):0.##}");
            if (p.Look is { } k)
            {
                op += FormattableString.Invariant($",\"pitch\":{k.Pitch:0.##},\"roll\":{k.Roll:0.##},\"sx\":{k.Sx:0.####},\"sy\":{k.Sy:0.####},\"sz\":{k.Sz:0.####},\"alt\":{k.Alt},\"mir\":{k.Mirror}");
                if (!string.IsNullOrEmpty(k.Mesh)) op += ",\"mesh\":" + Json(k.Mesh);
                if (k.Tex is { Length: > 0 }) op += ",\"tex\":[" + string.Join(",", k.Tex.Take(4).Select(t => Json(t ?? ""))) + "]";
            }
            return op + "}";
        }

        // Looks of these scene actors from the game (null entries when the game's SyndicateStudio.dll is too old)
        async Task<Dictionary<uint, PieceLook>?> LooksFor(IEnumerable<Item> actors)
        {
            var pipe = _pipe;
            if (pipe == null) return null;
            var idx = actors.Select(a => uint.TryParse(a.Props.GetValueOrDefault("id"), out var i) ? i : uint.MaxValue).Where(i => i != uint.MaxValue).Distinct().ToList();
            if (idx.Count == 0) return new();
            var looks = await pipe.GetLooksAsync(idx);
            if (looks == null) Out("Note: your game's SyndicateStudio.dll can't read piece textures yet, so copies get the default look. Run UPDATE FROM GIT and restart the game.");
            return looks;
        }

        static Look LookOf(Item a, Dictionary<uint, PieceLook>? looks)
        {
            TryF(a.Props, "pitch", out var pitch); TryF(a.Props, "roll", out var roll);
            PieceLook? l = null;
            if (looks != null && uint.TryParse(a.Props.GetValueOrDefault("id"), out var i)) looks.TryGetValue(i, out l);
            return new Look(pitch, roll, l?.Sx ?? 1, l?.Sy ?? 1, l?.Sz ?? 1, l?.Mesh, l?.Tex, l?.Alt ?? -1, l?.Mirror ?? -1);
        }

        async Task<bool> MapOpsAsync(IEnumerable<string> ops)
        {
            var list = ops.ToList();
            if (list.Count == 0) return true;
            var token = DevToken();
            if (string.IsNullOrEmpty(token)) { Out("ERROR: the backend hasn't created its developer token yet — start Syndicate's local services first."); return false; }
            try
            {
                // Sent in parts of at most ~80 KB: the backend's JSON body limit is 100 KB, and a big house with
                // every piece's mesh and texture paths is more than that in one go
                for (int start = 0; start < list.Count;)
                {
                    int end = start, bytes = 0;
                    while (end < list.Count && (end == start || bytes + list[end].Length < 80_000)) bytes += list[end++].Length + 1;
                    using var content = new System.Net.Http.StringContent("{\"ops\":[" + string.Join(",", list.Skip(start).Take(end - start)) + "]}", Encoding.UTF8, "application/json");
                    using var resp = await Http.PostAsync("http://127.0.0.1:3551/tilted/mapeditor/live?token=" + Uri.EscapeDataString(token), content);
                    var body = await resp.Content.ReadAsStringAsync();
                    if (!resp.IsSuccessStatusCode || !body.Contains("\"ok\":true")) { Out("ERROR: the map editor refused it — " + (body.Length > 200 ? body[..200] : body)); return false; }
                    start = end;
                }
                return true;
            }
            catch (Exception ex) { Out("ERROR: couldn't reach the backend — " + ex.Message); }
            return false;
        }

        Task<bool> SpawnPlaced(IEnumerable<Placed> pieces) => MapOpsAsync(pieces.Select(SpawnOp));
        Task<bool> RemovePlaced(IEnumerable<Placed> pieces) => MapOpsAsync(pieces.Select(p => "{\"op\":\"delete\",\"id\":" + p.Id + "}"));

        async Task PlaceWithUndo(string label, List<Placed> pieces)
        {
            if (pieces.Count == 0) return;
            if (!await SpawnPlaced(pieces)) return;
            PushEdit(new Edit(label, () => RemovePlaced(pieces), () => SpawnPlaced(pieces)));
            Out($"Placed {label}. The match server spawns it in a moment; Ctrl+Z removes it.");
        }

        // A tile dropped on the game: put it where the cursor points
        async void DropOnGame(string pick)
        {
            var pipe = _pipe;
            if (_viewArea == null || !NativeEmbed.Alive(_game)) { Out("Drop it on your docked game in the Server viewport."); return; }
            var p = Mouse.GetPosition(_viewArea);
            if (p.X < 0 || p.Y < 0 || p.X > _viewArea.ActualWidth || p.Y > _viewArea.ActualHeight) return; // dropped somewhere else: nothing to do
            if (pipe == null) { Out("Placing by dropping needs the Studio pipe: let your game dock first, or double-click the tile."); return; }
            var dpi = VisualTreeHelper.GetDpi(_viewArea);
            var hit = await pipe.TracePointAsync((float)(p.X * dpi.DpiScaleX), (float)(p.Y * dpi.DpiScaleY));
            if (hit == null) { Out("Nothing under the cursor to put it on (aim at the ground)." + (pipe.LastFailure.Length > 0 ? PipeWhy() : " If this keeps happening, run UPDATE FROM GIT for the newest SyndicateStudio.dll.")); return; }
            var cam = await pipe.GetCameraAsync();
            await PlacePick(pick, hit.Value.X, hit.Value.Y, hit.Value.Z, cam?.Yaw ?? 0);
        }

        // Place in front: the spot in the middle of the view (free cam), else the old 4 m in front of your character
        async Task<bool> PlaceAtViewCenter(string pick)
        {
            var pipe = _pipe;
            if (pipe == null || !_freeCam) return false;
            var vp = await pipe.GetViewportAsync();
            if (vp == null || vp.Value.Width == 0) return false;
            var hit = await pipe.TracePointAsync(vp.Value.Width / 2f, vp.Value.Height / 2f);
            if (hit == null) return false;
            var cam = await pipe.GetCameraAsync();
            await PlacePick(pick, hit.Value.X, hit.Value.Y, hit.Value.Z, cam?.Yaw ?? 0);
            return true;
        }

        async Task PlacePick(string pick, float x, float y, float z, float camYaw)
        {
            if (pick.StartsWith(PlotPrefix))
            {
                if (FindPlot(pick) is { } plot) await PlacePlot(plot, x, y, z, camYaw);
                return;
            }
            float yaw = 0;
            if (IsBuildPiece(pick) && GridStep > 0) { x = SnapTo(x, 512); y = SnapTo(y, 512); yaw = SnapYaw(camYaw); }
            else if (GridStep > 0) { x = SnapTo(x, GridStep); y = SnapTo(y, GridStep); }
            await PlaceWithUndo(Short(pick), new List<Placed> { new(_placeId++, pick, new Spot(x, y, z, yaw)) });
            RecordRecent(pick); RefreshContent(categoriesToo: true);
        }

        // ---- undo / redo -------------------------------------------------------------------------------
        sealed record Edit(string Label, Func<Task<bool>> Undo, Func<Task<bool>> Redo);
        readonly List<Edit> _undo = new(), _redo = new();
        bool _editBusy;
        Button? _undoBtn, _redoBtn;

        void PushEdit(Edit e)
        {
            _undo.Add(e);
            if (_undo.Count > 100) _undo.RemoveAt(0);
            _redo.Clear();
            UpdateUndoButtons();
        }

        async void Undo()
        {
            if (_editBusy) return;
            if (_undo.Count == 0) { Out("Nothing to undo."); return; }
            var e = _undo[^1]; _undo.RemoveAt(_undo.Count - 1);
            _editBusy = true;
            bool ok;
            try { ok = await e.Undo(); } catch { ok = false; } finally { _editBusy = false; }
            if (ok) { _redo.Add(e); Out("Undo: " + e.Label + "."); }
            else Out("Couldn't undo " + e.Label + PipeWhy() + ".");
            UpdateUndoButtons();
        }

        async void Redo()
        {
            if (_editBusy) return;
            if (_redo.Count == 0) { Out("Nothing to redo."); return; }
            var e = _redo[^1]; _redo.RemoveAt(_redo.Count - 1);
            _editBusy = true;
            bool ok;
            try { ok = await e.Redo(); } catch { ok = false; } finally { _editBusy = false; }
            if (ok) { _undo.Add(e); Out("Redo: " + e.Label + "."); }
            else Out("Couldn't redo " + e.Label + PipeWhy() + ".");
            UpdateUndoButtons();
        }

        void UpdateUndoButtons()
        {
            if (_undoBtn != null) { _undoBtn.IsEnabled = _undo.Count > 0; _undoBtn.ToolTip = _undo.Count > 0 ? "Undo " + _undo[^1].Label + " (Ctrl+Z)" : "Nothing to undo (Ctrl+Z)"; }
            if (_redoBtn != null) { _redoBtn.IsEnabled = _redo.Count > 0; _redoBtn.ToolTip = _redo.Count > 0 ? "Redo " + _redo[^1].Label + " (Ctrl+Y)" : "Nothing to redo (Ctrl+Y)"; }
        }

        // Toolbar: Undo, Redo, Turn 90, Delete and the grid size
        void AddEditTools(DockPanel row)
        {
            row.Children.Add(new Border { Width = 1, Background = Line, Margin = new Thickness(8, 6, 12, 6) });
            _undoBtn = ToolButton("", "Undo", Undo, "Undo (Ctrl+Z)"); row.Children.Add(_undoBtn);
            _redoBtn = ToolButton("", "Redo", Redo, "Redo (Ctrl+Y)"); row.Children.Add(_redoBtn);
            row.Children.Add(ToolButton("", "Turn 90°", () => RotateSelection(90), "Turn the selection 90° (R; Shift+R the other way). Several pieces turn together around their middle."));
            row.Children.Add(ToolButton("", "Delete", DeleteSelection, "Delete the selection (Delete)"));
            var gridBox = new StackPanel { VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(4, 0, 12, 0) };
            gridBox.Children.Add(new TextBlock { Text = "GRID", Foreground = Muted, FontSize = 10, FontWeight = FontWeights.SemiBold, Margin = new Thickness(1, 0, 0, 3) });
            var pick = Picker(new List<(string, string)> { ("0", "Off"), ("64", "64"), ("128", "128"), ("256", "256"), ("512", "512 (build tile)") }, GridStep.ToString(Inv), v =>
            {
                _layout.Grid = int.Parse(v, Inv); SaveLayout();
                Out(GridStep > 0 ? $"Grid snapping: {GridStep} units ({GridZ} up and down). Arrow drags, nudges and placing snap to it." : "Grid snapping off.");
            });
            pick.Width = 120; pick.ToolTip = "Snap moves and placing to this grid. 512 is one Fortnite build tile (384 high), so pieces line up with builds.";
            gridBox.Children.Add(pick);
            row.Children.Add(gridBox);
            UpdateUndoButtons();
        }

        // ---- Details: selection tools ------------------------------------------------------------------------
        void AddSelectionTools(StackPanel panel)
        {
            if (_sel.Count == 0 || !_sel.All(k => k.StartsWith("actor:"))) return;
            if (_sel.Count == 1)
            {
                // One map piece: grab the rest of its house in one go
                panel.Children.Add(Section("House"));
                var one = new WrapPanel();
                one.Children.Add(MakeButton("Select whole house", SelectWholeHouse, tip: "Selects every piece joined to this one (walls, floors, roof, stairs, doors, windows), so you can save the house as a House Plot"));
                panel.Children.Add(one);
                return;
            }
            panel.Children.Add(new TextBlock { Text = _sel.Count + " pieces selected", Foreground = Bright, FontSize = 15, FontWeight = FontWeights.SemiBold, Margin = new Thickness(0, 10, 0, 2) });
            panel.Children.Add(Note("The arrows move the first one; the others follow. Shift+click adds or removes a piece."));
            var row = new WrapPanel { Margin = new Thickness(0, 8, 0, 0) };
            row.Children.Add(MakeButton("Turn 90°", () => RotateSelection(90)));
            row.Children.Add(MakeButton("Delete all", DeleteSelection));
            row.Children.Add(MakeButton("Save as House Plot", SaveSelectionAsPlot, primary: true, tip: "Keep these real Fortnite house pieces as a house you can drop anywhere (Content Browser > House Plots)"));
            row.Children.Add(MakeButton("Clear", ClearSelection));
            panel.Children.Add(row);
            panel.Children.Add(Section("First selected"));
        }

        // Nature and loose things that stand next to houses but aren't part of them
        static readonly string[] NotHouse = { "Tree", "Rock", "Bush", "Shrub", "Grass", "Flower", "Cliff", "Road", "Car_", "Vehicle", "Boulder", "Stump", "Log_", "Pickup" };

        // From the selected piece, collect every piece within reach of one already found (a house's walls, floors,
        // roof, stairs and furniture touch each other), up to 40 m out. Uses what the Outliner lists (200 m around the camera).
        void SelectWholeHouse()
        {
            var seed = SelectedActors().FirstOrDefault();
            if (seed == null || !ActorPos(seed, out var sx, out var sy, out var sz, out _)) { Out("Select one piece of the house first (left click it in the free cam)."); return; }
            bool Usable(Item i) => i.Kind == "actor" && ActorPos(i, out _, out _, out _, out _)
                && !BoxSkip.Any(k => i.Props.GetValueOrDefault("c", "").Contains(k, StringComparison.OrdinalIgnoreCase))
                && !NotHouse.Any(k => i.Props.GetValueOrDefault("c", "").Contains(k, StringComparison.OrdinalIgnoreCase));
            var pool = _items.Values.Where(i => i.Key != seed.Key && Usable(i)).Select(i => { ActorPos(i, out var x, out var y, out var z, out _); return (Item: i, X: x, Y: y, Z: z); })
                .Where(p => (p.X - sx) * (p.X - sx) + (p.Y - sy) * (p.Y - sy) < 4000f * 4000f && Math.Abs(p.Z - sz) < 3000).ToList();
            var found = new List<(float X, float Y, float Z)> { (sx, sy, sz) };
            var keys = new List<string> { seed.Key };
            const float Reach = 700f; // a bit more than one build tile
            for (int f = 0; f < found.Count && keys.Count < 600; f++)
            {
                var (fx, fy, fz) = found[f];
                for (int k = pool.Count - 1; k >= 0; k--)
                {
                    var p = pool[k];
                    if ((p.X - fx) * (p.X - fx) + (p.Y - fy) * (p.Y - fy) + (p.Z - fz) * (p.Z - fz) > Reach * Reach) continue;
                    found.Add((p.X, p.Y, p.Z)); keys.Add(p.Item.Key); pool.RemoveAt(k);
                }
            }
            SetSelection(keys);
            Out($"Selected {keys.Count} piece{(keys.Count == 1 ? "" : "s")} of this house. Shift+click adds or removes a piece, then press Save as House Plot in Details.");
        }

        // ---- House Plots ---------------------------------------------------------------------------------
        // Real Fortnite houses: copies of houses from the island (select one with Select whole house or a selection
        // box, then Save as House Plot). Each piece keeps its own class (the game's own walls, floors, roofs, doors,
        // furniture), so a placed plot is the same house. Shared plots ship in Runtime\studio-house-plots.json;
        // your own are saved in the launcher's data folder.
        const string PlotsCat = "House Plots";
        const string PlotPrefix = "plot:";
        // Pitch/Roll/scale/Mesh/Tex/Alt/Mirror: the piece's own look (see Look); plots saved before these existed load with defaults
        sealed record PlotPiece(string Class, float X, float Y, float Z, float Yaw, float Pitch = 0, float Roll = 0, float Sx = 1, float Sy = 1, float Sz = 1,
            string? Mesh = null, string[]? Tex = null, int Alt = -1, int Mirror = -1);
        sealed class HousePlot
        {
            public string Name { get; set; } = "";
            public string Note { get; set; } = "";
            public string Season { get; set; } = "";  // the Fortnite build it was copied in (its classes exist there)
            public float CenterX { get; set; }   // the middle of the plot, relative to the plot's origin
            public float CenterY { get; set; }
            public float FacingYaw { get; set; } // the camera's yaw when it was saved: placing turns it to face you the same way
            public bool Custom { get; set; }
            public List<PlotPiece> Pieces { get; set; } = new();
        }
        List<HousePlot> _plots = new();

        static string PlotsFile => Path.Combine(SyndicateLauncher.Services.SettingsService.DataDirectory, "studio-house-plots.json");
        static string SharedPlotsFile => Path.Combine(SyndicateLauncher.Services.GameLauncher.RuntimeFolder, "studio-house-plots.json");

        List<HousePlot> LoadPlots()
        {
            var all = new List<HousePlot>();
            void Read(string file, bool custom)
            {
                try
                {
                    if (File.Exists(file))
                        foreach (var p in JsonSerializer.Deserialize<List<HousePlot>>(File.ReadAllText(file)) ?? new())
                            if (p.Pieces.Count > 0) { p.Custom = custom; all.Add(p); }
                }
                catch (Exception ex) { Out("Couldn't read House Plots from " + file + " — " + ex.Message); }
            }
            Read(SharedPlotsFile, false);
            Read(PlotsFile, true);
            return all;
        }

        void SaveCustomPlots()
        {
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(PlotsFile)!);
                File.WriteAllText(PlotsFile, JsonSerializer.Serialize(_plots.Where(p => p.Custom).ToList(), new JsonSerializerOptions { WriteIndented = true }));
            }
            catch (Exception ex) { Out("ERROR saving House Plots — " + ex.Message); }
        }

        IEnumerable<(string Key, string Label, string Note, bool Custom)> PlotTiles()
        {
            for (int i = 0; i < _plots.Count; i++)
            {
                var p = _plots[i];
                var note = p.Pieces.Count + " pieces" + (p.Season.Length > 0 ? " · Fortnite " + p.Season : "") + (p.Custom ? " · yours (right-click deletes)" : "");
                yield return (PlotPrefix + i, p.Name, p.Note.Length > 0 ? p.Note + " · " + note : note, p.Custom);
            }
        }

        HousePlot? FindPlot(string key) =>
            int.TryParse(key[PlotPrefix.Length..], out var i) && i >= 0 && i < _plots.Count ? _plots[i] : null;

        // Drop point = the middle of the plot, turned to face you the way it faced you when it was saved
        async Task PlacePlot(HousePlot plot, float x, float y, float z, float camYaw)
        {
            if (plot.Season.Length > 0 && _build != null && plot.Season != _build)
                Out($"Note: \"{plot.Name}\" was copied in Fortnite {plot.Season}; pieces this season doesn't have are skipped by the match server.");
            float yaw = SnapYaw(camYaw - plot.FacingYaw);
            double r = yaw * Math.PI / 180;
            float c = (float)Math.Round(Math.Cos(r), 6), s = (float)Math.Round(Math.Sin(r), 6);
            (float X, float Y) Turn(float px, float py) => (px * c - py * s, px * s + py * c);
            var (cx, cy) = Turn(plot.CenterX, plot.CenterY);
            float ox = x - cx, oy = y - cy; // the plot's origin
            if (GridStep > 0) { ox = SnapTo(ox, 512); oy = SnapTo(oy, 512); }
            var pieces = new List<Placed>();
            foreach (var piece in plot.Pieces)
            {
                var (px, py) = Turn(piece.X, piece.Y);
                // Turning the whole house about the vertical axis only adds to each piece's yaw; pitch and roll stay
                pieces.Add(new Placed(_placeId++, piece.Class, new Spot(ox + px, oy + py, z + piece.Z, NormYaw(piece.Yaw + yaw)),
                    new Look(piece.Pitch, piece.Roll, piece.Sx, piece.Sy, piece.Sz, piece.Mesh, piece.Tex, piece.Alt, piece.Mirror)));
            }
            await PlaceWithUndo(plot.Name, pieces);
        }

        async void SaveSelectionAsPlot()
        {
            var sel = SelectedActors().Where(a => ActorPos(a, out _, out _, out _, out _) && a.Props.GetValueOrDefault("c", "").Length > 0).ToList();
            if (sel.Count == 0) { Out("Select the pieces of a house first: click one piece, then Select whole house in Details (or drag a box around it in the free cam)."); return; }
            var cam = _pipe == null ? null : await _pipe.GetCameraAsync();
            var name = Ask("Save as House Plot", "Name of this house:", "My house " + (_plots.Count(p => p.Custom) + 1));
            if (string.IsNullOrWhiteSpace(name)) return;
            float minZ = float.MaxValue, cx = 0, cy = 0;
            foreach (var a in sel) { ActorPos(a, out var x, out var y, out var z, out _); cx += x; cy += y; minZ = Math.Min(minZ, z); }
            cx /= sel.Count; cy /= sel.Count;
            // Origin on the build grid, so a copy lines up with the grid the same way the original does
            float gx = SnapTo(cx, 512), gy = SnapTo(cy, 512);
            var plot = new HousePlot { Name = name.Trim(), Custom = true, CenterX = cx - gx, CenterY = cy - gy, Season = _build ?? "", FacingYaw = SnapYaw(cam?.Yaw ?? 0) };
            var looks = await LooksFor(sel);
            int dressed = 0;
            foreach (var a in sel)
            {
                ActorPos(a, out var x, out var y, out var z, out var yaw);
                var k = LookOf(a, looks);
                if (k.Mesh != null || k.Tex?.Any(t => t.Length > 0) == true) dressed++;
                plot.Pieces.Add(new PlotPiece(a.Props["c"], x - gx, y - gy, z - minZ, yaw, k.Pitch, k.Roll, k.Sx, k.Sy, k.Sz, k.Mesh, k.Tex, k.Alt, k.Mirror));
            }
            _plots.Add(plot);
            SaveCustomPlots();
            Out($"Saved \"{plot.Name}\" ({plot.Pieces.Count} pieces, {dressed} with their own mesh and textures) to Content Browser > House Plots. Drag it onto the game to place a copy.");
            if (_contentCategory == PlotsCat) RefreshContent();
            else RefreshContent(categoriesToo: true);
        }

        void DeleteCustomPlot(string key)
        {
            if (FindPlot(key) is not { } found || !found.Custom) return;
            if (MessageBox.Show(this, "Delete the House Plot \"" + found.Name + "\"?", "Syndicate Studio", MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes) return;
            _plots.Remove(found);
            SaveCustomPlots();
            if (_contentPick == key) _contentPick = null;
            RefreshContent(categoriesToo: true);
        }

        // Content Browser tiles for House Plots (drag one onto the game, or double-click to place it in the middle of the view)
        void RenderPlotTiles()
        {
            if (_tiles == null || _contentInfo == null) return;
            var shown = PlotTiles().Where(t => _contentFilter.Length == 0 || t.Label.Contains(_contentFilter, StringComparison.OrdinalIgnoreCase)).ToList();
            foreach (var t in shown)
            {
                bool picked = t.Key == _contentPick;
                var tile = new Border { Width = 132, Height = 104, Margin = new Thickness(4), Background = picked ? AccentDim : Raised, BorderBrush = picked ? Accent : Line, BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(3), Cursor = Cursors.Hand, ToolTip = t.Label + " — " + t.Note + ". Drag onto the game to place it." };
                var stack = new DockPanel();
                var label = new TextBlock { Text = t.Label, Foreground = Fg, FontSize = 10.5, TextWrapping = TextWrapping.Wrap, TextAlignment = TextAlignment.Center, Margin = new Thickness(4, 0, 4, 6), MaxHeight = 30, TextTrimming = TextTrimming.CharacterEllipsis };
                DockPanel.SetDock(label, Dock.Bottom); stack.Children.Add(label);
                stack.Children.Add(new TextBlock { Text = "", FontFamily = Icons, FontSize = 34, Foreground = t.Custom ? Accent : Muted, HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center });
                tile.Child = stack;
                var key = t.Key;
                WireTileDrag(tile, key);
                if (t.Custom) tile.MouseRightButtonUp += (_, e) => { e.Handled = true; DeleteCustomPlot(key); };
                _tiles.Children.Add(tile);
            }
            _contentInfo.Text = shown.Count == 0 && _contentFilter.Length == 0
                ? "No House Plots yet. In the free cam, left click a piece of a house on the island, press Select whole house in Details, then Save as House Plot: the real house becomes a plot you can drag onto the game anywhere."
                : shown.Count + " house plot(s) · drag one onto the game to place it (it faces you the way it did when saved) · add more with Select whole house > Save as House Plot";
        }

        // ---- dragging tiles onto the game ------------------------------------------------------------------
        string? _tileDrag;
        Point _tileDragFrom;
        bool _tileDragging;

        // Press on a tile: pick it (double-click places it); drag it out and let go over the game to place it there
        void WireTileDrag(Border tile, string key)
        {
            tile.MouseLeftButtonDown += (_, e) =>
            {
                _contentPick = key;
                if (_placingOn && !key.StartsWith(PlotPrefix)) PushPlacing(true);
                if (e.ClickCount == 2) { PlaceContent(); return; }
                _tileDrag = key; _tileDragFrom = e.GetPosition(this); _tileDragging = false;
                tile.CaptureMouse();
                e.Handled = true;
            };
            tile.MouseMove += (_, e) =>
            {
                if (_tileDrag != key || !tile.IsMouseCaptured) return;
                if (!_tileDragging && (e.GetPosition(this) - _tileDragFrom).Length > 8) { _tileDragging = true; tile.Cursor = Cursors.Cross; }
            };
            tile.MouseLeftButtonUp += (_, e) =>
            {
                if (_tileDrag != key) return;
                bool dropped = _tileDragging;
                _tileDrag = null; _tileDragging = false;
                tile.ReleaseMouseCapture();
                tile.Cursor = Cursors.Hand;
                if (dropped) DropOnGame(key);
                RefreshContent(); // show the picked tile
                e.Handled = true;
            };
        }

        // ---- a small text prompt ---------------------------------------------------------------------------
        string? Ask(string title, string question, string initial)
        {
            var dlg = new Window
            {
                Title = title, Width = 380, Height = 160, WindowStartupLocation = WindowStartupLocation.CenterOwner,
                Owner = this, ResizeMode = ResizeMode.NoResize, Background = Panel, WindowStyle = WindowStyle.ToolWindow
            };
            var col = new StackPanel { Margin = new Thickness(16) };
            col.Children.Add(new TextBlock { Text = question, Foreground = Fg, Margin = new Thickness(0, 0, 0, 6) });
            var box = new TextBox { Text = initial, Background = Field, Foreground = Fg, BorderBrush = Line, CaretBrush = Fg, Padding = new Thickness(6, 4, 6, 4) };
            col.Children.Add(box);
            var row = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(0, 12, 0, 0) };
            string? result = null;
            row.Children.Add(MakeButton("Save", () => { result = box.Text; dlg.DialogResult = true; dlg.Close(); }, primary: true));
            row.Children.Add(MakeButton("Cancel", () => dlg.Close()));
            col.Children.Add(row);
            dlg.Content = col;
            box.SelectAll(); box.Focus();
            // The docked game sits above the Studio's own windows: hide it while the prompt is open
            _popupOpen = true; PlaceView();
            try { return dlg.ShowDialog() == true ? result : null; }
            finally { _popupOpen = false; PlaceView(); }
        }
    }
}
