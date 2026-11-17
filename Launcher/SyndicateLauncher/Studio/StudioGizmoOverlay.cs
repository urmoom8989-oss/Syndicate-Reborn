using System;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Shapes;
using System.Windows.Threading;

namespace SyndicateLauncher.Studio
{
    /// <summary>
    /// Transparent overlay window that draws a translation gizmo (XYZ arrows) over the game viewport.
    ///
    /// Responsiveness:
    ///  * Its own ~30 Hz loop asks the game for one batch projection: the actor's screen point plus the screen
    ///    points of +100 units along X, Y and Z. That gives the gizmo's position AND the real on-screen direction of
    ///    each axis for the current camera, so the arrows match the world and follow the camera smoothly.
    ///  * While dragging, the arrows follow the cursor locally (no round trip), and moves to the game are coalesced:
    ///    only the newest position is sent once the previous one is done, so the actor never lags behind a backlog.
    ///
    /// Transparent pixels are click-through (layered window), so the game below gets every click that misses the gizmo.
    ///
    /// It also draws the free cam's selection box (marquee) while you drag in the game, and a small marker on every
    /// other selected piece, so a drag-selected house shows what it caught.
    /// </summary>
    internal sealed class StudioGizmoOverlay : Window
    {
        // Axis colors — Unreal Editor convention: X=red, Y=green, Z=blue
        static readonly SolidColorBrush XBrush = Freeze("#F54141");
        static readonly SolidColorBrush YBrush = Freeze("#4BE164");
        static readonly SolidColorBrush ZBrush = Freeze("#4691FF");
        static readonly SolidColorBrush HoverBrush = Freeze("#FFD700");
        static readonly SolidColorBrush CenterBrush = Freeze("#FFFFFF");
        static readonly SolidColorBrush CenterStroke = Freeze("#888888");

        static SolidColorBrush Freeze(string hex) { var b = new SolidColorBrush((Color)ColorConverter.ConvertFromString(hex)); b.Freeze(); return b; }

        // Layout constants (in DIPs)
        const double ArrowLen = 70;
        const double ShaftThick = 3;
        const double HeadW = 14, HeadH = 10;
        const double CenterR = 6;
        const double HitR = 10;
        const float ProbeUnits = 100f;             // world units probed along each axis

        readonly Canvas _canvas;
        readonly Canvas _gizmoLayer = new() { IsHitTestVisible = false };   // the arrows (hidden while nothing is tracked)
        readonly Canvas _markerLayer = new() { IsHitTestVisible = false };  // other selected pieces
        readonly System.Windows.Shapes.Rectangle _marquee = new() { Stroke = Freeze("#FFD23F"), StrokeThickness = 1.5, StrokeDashArray = new DoubleCollection { 4, 3 }, Fill = new SolidColorBrush(Color.FromArgb(0x22, 0xFF, 0xD2, 0x3F)), IsHitTestVisible = false, Visibility = Visibility.Collapsed };
        bool _marqueeOn;
        (float X, float Y, float Z)[] _markers = Array.Empty<(float, float, float)>();
        bool _projectingMarkers;
        readonly Action<string> _log;
        readonly DispatcherTimer _loop;

        /// <summary>The Studio pipe. Replaced by the Studio when the game restarts (a new game = a new pipe).</summary>
        public StudioPipeClient? Pipe { get; set; }

        /// <summary>Raised when a drag completes (not cancelled). Args: actorIndex, finalX, finalY, finalZ.</summary>
        public event Action<uint, float, float, float>? DragCompleted;

        /// <summary>Raised when an arrow is grabbed. Args: actorIndex, startX, startY, startZ (for undo).</summary>
        public event Action<uint, float, float, float>? DragStarted;

        readonly Line _xShaft, _yShaft, _zShaft;
        // Wide, almost invisible grab bands under each arrow. The overlay is a layered window, and Windows sends clicks
        // on fully transparent pixels straight through to the game: a 3 px arrow was nearly impossible to grab.
        // Alpha 1/255 is invisible but counts as solid for that test.
        readonly Line _xGrab, _yGrab, _zGrab;
        readonly Ellipse _centerGrab;
        static readonly SolidColorBrush GrabBrush = MakeGrabBrush();
        static SolidColorBrush MakeGrabBrush() { var b = new SolidColorBrush(Color.FromArgb(1, 0, 0, 0)); b.Freeze(); return b; }
        readonly Polygon _xHead, _yHead, _zHead;
        readonly Ellipse _center;

        // ---- state ----
        bool _tracking;                 // an actor is selected
        bool _shown;                    // drawn on screen
        double _gx, _gy;                // gizmo center (overlay DIPs)
        uint _actorIdx;
        float _ax, _ay, _az;            // actor world position
        DateTime _lastLocalMove = DateTime.MinValue;

        enum Axis { None, X, Y, Z }
        Axis _hover = Axis.None, _drag = Axis.None;
        Point _dragOrigin;
        double _dragGx0, _dragGy0;
        float _dragAx0, _dragAy0, _dragAz0;

        // On-screen vector (DIPs) of +ProbeUnits along each axis; null until the first projection
        readonly Vector?[] _axisPx = new Vector?[4];

        // Coalesced moves
        bool _sending;
        (float X, float Y, float Z)? _pendingMove;
        bool _projecting;

        public StudioGizmoOverlay(StudioPipeClient pipe, Action<string> log)
        {
            Pipe = pipe;
            _log = log;

            WindowStyle = WindowStyle.None;
            AllowsTransparency = true;
            Background = Brushes.Transparent;
            ShowInTaskbar = false;
            Topmost = false;
            ResizeMode = ResizeMode.NoResize;
            ShowActivated = false;

            _canvas = new Canvas { Background = null, IsHitTestVisible = true };
            Content = _canvas;
            _canvas.Children.Add(_markerLayer);
            _canvas.Children.Add(_gizmoLayer);
            _canvas.Children.Add(_marquee);

            _xGrab = MakeGrab(); _yGrab = MakeGrab(); _zGrab = MakeGrab();
            _centerGrab = new Ellipse { Width = (CenterR + 6) * 2, Height = (CenterR + 6) * 2, Fill = GrabBrush, IsHitTestVisible = false };
            _gizmoLayer.Children.Add(_xGrab); _gizmoLayer.Children.Add(_yGrab); _gizmoLayer.Children.Add(_zGrab); _gizmoLayer.Children.Add(_centerGrab);
            _xShaft = MakeShaft(XBrush); _yShaft = MakeShaft(YBrush); _zShaft = MakeShaft(ZBrush);
            _xHead = MakeHead(XBrush); _yHead = MakeHead(YBrush); _zHead = MakeHead(ZBrush);
            _center = new Ellipse { Width = CenterR * 2, Height = CenterR * 2, Fill = CenterBrush, Stroke = CenterStroke, StrokeThickness = 1 };
            _gizmoLayer.Children.Add(_xShaft); _gizmoLayer.Children.Add(_yShaft); _gizmoLayer.Children.Add(_zShaft);
            _gizmoLayer.Children.Add(_xHead); _gizmoLayer.Children.Add(_yHead); _gizmoLayer.Children.Add(_zHead);
            _gizmoLayer.Children.Add(_center);

            _loop = new DispatcherTimer(DispatcherPriority.Render) { Interval = TimeSpan.FromMilliseconds(33) };
            _loop.Tick += (_, __) => { Project(); ProjectMarkers(); };
            _loop.Start();
            Closed += (_, __) => _loop.Stop();

            HideGizmo();
        }

        static Line MakeGrab() => new() { Stroke = GrabBrush, StrokeThickness = HitR * 2, StrokeStartLineCap = PenLineCap.Round, StrokeEndLineCap = PenLineCap.Round, IsHitTestVisible = false };
        static Line MakeShaft(Brush brush) => new() { Stroke = brush, StrokeThickness = ShaftThick, StrokeStartLineCap = PenLineCap.Round, StrokeEndLineCap = PenLineCap.Round, IsHitTestVisible = false };
        static Polygon MakeHead(Brush brush) => new() { Fill = brush, IsHitTestVisible = false, Points = new PointCollection { new(0, -HeadH / 2), new(HeadW, 0), new(0, HeadH / 2) } };

        // ---- public API ----

        /// <summary>Follow this actor. The overlay projects it itself every frame from now on.</summary>
        public void Track(uint actorIdx, float ax, float ay, float az)
        {
            bool same = _tracking && actorIdx == _actorIdx;
            // The Outliner's numbers can be older than our own last move: keep ours for a moment after a drag/apply
            if (!same || (_drag == Axis.None && !_sending && (DateTime.UtcNow - _lastLocalMove).TotalSeconds > 2))
            { _ax = ax; _ay = ay; _az = az; }
            if (!same) { _actorIdx = actorIdx; for (int i = 0; i < 4; i++) _axisPx[i] = null; }
            _tracking = true;
        }

        /// <summary>Old entry point (one projected point from the Studio); kept for callers, now just tracks.</summary>
        public void ShowAt(double viewportPixelX, double viewportPixelY, uint actorIdx, float ax, float ay, float az) => Track(actorIdx, ax, ay, az);

        /// <summary>Stop following (nothing selected).</summary>
        public new void Hide()
        {
            _tracking = false;
            if (_drag != Axis.None) { _drag = Axis.None; ReleaseMouseCapture(); }
            _markers = Array.Empty<(float, float, float)>(); _markerLayer.Children.Clear();
            _marqueeOn = false; _marquee.Visibility = Visibility.Collapsed;
            HideGizmo();
        }

        void HideGizmo()
        {
            _shown = false;
            _hover = Axis.None;
            UpdateVisibility();
        }

        // The window shows while there is anything to draw: the arrows, the selection box or selection markers
        void UpdateVisibility()
        {
            _gizmoLayer.Visibility = _shown ? Visibility.Visible : Visibility.Hidden;
            bool any = _shown || _marqueeOn || _markerLayer.Children.Count > 0;
            var want = any ? Visibility.Visible : Visibility.Hidden;
            if (Visibility != want) Visibility = want;
        }

        // ---- selection box and markers (free cam) ----

        /// <summary>Draw the selection box; corners in game pixels.</summary>
        public void ShowMarquee(double x0, double y0, double x1, double y1)
        {
            var dpi = VisualTreeHelper.GetDpi(this);
            Canvas.SetLeft(_marquee, Math.Min(x0, x1) / dpi.DpiScaleX);
            Canvas.SetTop(_marquee, Math.Min(y0, y1) / dpi.DpiScaleY);
            _marquee.Width = Math.Abs(x1 - x0) / dpi.DpiScaleX;
            _marquee.Height = Math.Abs(y1 - y0) / dpi.DpiScaleY;
            _marquee.Visibility = Visibility.Visible;
            _marqueeOn = true;
            UpdateVisibility();
        }

        public void HideMarquee()
        {
            if (!_marqueeOn) return;
            _marqueeOn = false;
            _marquee.Visibility = Visibility.Collapsed;
            UpdateVisibility();
        }

        /// <summary>World points of the other selected pieces (the tracked one has the arrows); empty clears them.</summary>
        public void SetMarkers(IReadOnlyList<(float X, float Y, float Z)> points)
        {
            _markers = new (float, float, float)[points.Count];
            for (int i = 0; i < points.Count; i++) _markers[i] = points[i];
            if (_markers.Length == 0) { _markerLayer.Children.Clear(); UpdateVisibility(); }
        }

        async void ProjectMarkers()
        {
            var pipe = Pipe;
            if (_projectingMarkers || pipe == null) return;
            if (_markers.Length == 0) { if (_markerLayer.Children.Count > 0) { _markerLayer.Children.Clear(); UpdateVisibility(); } return; }
            _projectingMarkers = true;
            try
            {
                var pts = new float[_markers.Length * 3];
                for (int i = 0; i < _markers.Length; i++) { pts[i * 3] = _markers[i].X; pts[i * 3 + 1] = _markers[i].Y; pts[i * 3 + 2] = _markers[i].Z; }
                var r = await pipe.BatchProjectAsync(pts);
                if (r == null) return;
                var dpi = VisualTreeHelper.GetDpi(this);
                while (_markerLayer.Children.Count < r.Length)
                    _markerLayer.Children.Add(new System.Windows.Shapes.Rectangle { Width = 9, Height = 9, Fill = HoverBrush, Stroke = CenterStroke, StrokeThickness = 1, IsHitTestVisible = false });
                while (_markerLayer.Children.Count > r.Length) _markerLayer.Children.RemoveAt(_markerLayer.Children.Count - 1);
                for (int i = 0; i < r.Length; i++)
                {
                    var m = (UIElement)_markerLayer.Children[i];
                    m.Visibility = r[i].OnScreen ? Visibility.Visible : Visibility.Hidden;
                    Canvas.SetLeft(m, r[i].X / dpi.DpiScaleX - 4.5);
                    Canvas.SetTop(m, r[i].Y / dpi.DpiScaleY - 4.5);
                }
                UpdateVisibility();
            }
            catch { }
            finally { _projectingMarkers = false; }
        }

        public void Reposition(int screenLeft, int screenTop, int pixelWidth, int pixelHeight)
        {
            var dpi = VisualTreeHelper.GetDpi(this);
            Left = screenLeft / dpi.DpiScaleX;
            Top = screenTop / dpi.DpiScaleY;
            Width = pixelWidth / dpi.DpiScaleX;
            Height = pixelHeight / dpi.DpiScaleY;
        }

        /// <summary>Keep the overlay above the docked game: a window always sits above the window that owns it, so
        /// clicking into the game can't bury the gizmo underneath it.</summary>
        public void AttachToGame(IntPtr game)
        {
            if (game == IntPtr.Zero || game == _ownerGame) return;
            var me = new System.Windows.Interop.WindowInteropHelper(this).EnsureHandle();
            NativeEmbed.SetOwner(me, game);
            _ownerGame = game;
        }
        IntPtr _ownerGame;

        public void UpdateActorPosition(float ax, float ay, float az) => Track(_actorIdx, ax, ay, az);

        public bool IsDragging => _drag != Axis.None;
        public uint ActorIndex => _actorIdx;

        // ---- projection loop ----

        async void Project()
        {
            var pipe = Pipe;
            if (!_tracking || _drag != Axis.None || _projecting || pipe == null) return;
            _projecting = true;
            try
            {
                float ax = _ax, ay = _ay, az = _az;
                var pts = new[]
                {
                    ax, ay, az,
                    ax + ProbeUnits, ay, az,
                    ax, ay + ProbeUnits, az,
                    ax, ay, az + ProbeUnits,
                };
                var r = await pipe.BatchProjectAsync(pts);
                if (!_tracking || _drag != Axis.None) return;
                if (r == null || r.Length < 4 || !r[0].OnScreen) { HideGizmo(); return; }
                var dpi = VisualTreeHelper.GetDpi(this);
                double ox = r[0].X / dpi.DpiScaleX, oy = r[0].Y / dpi.DpiScaleY;
                for (int i = 1; i <= 3; i++)
                {
                    // An axis pointing almost straight at the camera projects to ~nothing: keep the last good one
                    var v = new Vector(r[i].X / dpi.DpiScaleX - ox, r[i].Y / dpi.DpiScaleY - oy);
                    if (r[i].OnScreen && v.Length > 1.5) _axisPx[i] = v;
                }
                _gx = ox; _gy = oy;
                _shown = true;
                Layout();
                UpdateVisibility();
            }
            catch { }
            finally { _projecting = false; }
        }

        // ---- layout ----

        Vector Dir(Axis a)
        {
            var v = _axisPx[(int)a];
            if (v is { } p && p.Length > 0.5) { p.Normalize(); return p; }
            // Fallback before the first projection: a plausible editor-style layout
            return a switch { Axis.X => new Vector(1, 0), Axis.Y => new Vector(0.707, 0.707), Axis.Z => new Vector(0, -1), _ => new Vector(0, 0) };
        }

        // World units per DIP along an axis (from the projected probe); fallback 8
        double UnitsPerDip(Axis a)
        {
            var v = _axisPx[(int)a];
            return v is { } p && p.Length > 1.5 ? ProbeUnits / p.Length : 8;
        }

        void Layout()
        {
            void PlaceArrow(Line shaft, Polygon head, Axis axis)
            {
                var d = Dir(axis);
                var grab = axis == Axis.X ? _xGrab : axis == Axis.Y ? _yGrab : _zGrab;
                grab.X1 = _gx; grab.Y1 = _gy;
                grab.X2 = _gx + d.X * (ArrowLen + HeadW); grab.Y2 = _gy + d.Y * (ArrowLen + HeadW);
                shaft.X1 = _gx; shaft.Y1 = _gy;
                shaft.X2 = _gx + d.X * ArrowLen; shaft.Y2 = _gy + d.Y * ArrowLen;
                head.RenderTransform = new RotateTransform(Math.Atan2(d.Y, d.X) * 180 / Math.PI, 0, 0);
                Canvas.SetLeft(head, shaft.X2);
                Canvas.SetTop(head, shaft.Y2);
            }
            PlaceArrow(_xShaft, _xHead, Axis.X);
            PlaceArrow(_yShaft, _yHead, Axis.Y);
            PlaceArrow(_zShaft, _zHead, Axis.Z);
            Canvas.SetLeft(_centerGrab, _gx - CenterR - 6);
            Canvas.SetTop(_centerGrab, _gy - CenterR - 6);
            Canvas.SetLeft(_center, _gx - CenterR);
            Canvas.SetTop(_center, _gy - CenterR);
            PaintColors();
        }

        void PaintColors()
        {
            Brush Pick(Axis a, Brush normal) => _drag == a || _hover == a ? HoverBrush : normal;
            _xShaft.Stroke = Pick(Axis.X, XBrush); _xHead.Fill = Pick(Axis.X, XBrush);
            _yShaft.Stroke = Pick(Axis.Y, YBrush); _yHead.Fill = Pick(Axis.Y, YBrush);
            _zShaft.Stroke = Pick(Axis.Z, ZBrush); _zHead.Fill = Pick(Axis.Z, ZBrush);
        }

        // ---- hit testing ----

        Axis HitAxis(Point p)
        {
            foreach (var a in new[] { Axis.Z, Axis.X, Axis.Y })
            {
                var d = Dir(a);
                if (DistToSegment(p.X, p.Y, _gx, _gy, _gx + d.X * (ArrowLen + HeadW), _gy + d.Y * (ArrowLen + HeadW)) < HitR) return a;
            }
            return Axis.None;
        }

        static double DistToSegment(double px, double py, double x1, double y1, double x2, double y2)
        {
            double dx = x2 - x1, dy = y2 - y1, len2 = dx * dx + dy * dy;
            if (len2 < 0.01) return Math.Sqrt((px - x1) * (px - x1) + (py - y1) * (py - y1));
            double t = Math.Clamp(((px - x1) * dx + (py - y1) * dy) / len2, 0, 1);
            double nx = x1 + t * dx, ny = y1 + t * dy;
            return Math.Sqrt((px - nx) * (px - nx) + (py - ny) * (py - ny));
        }

        bool NearCenter(Point p) => (p.X - _gx) * (p.X - _gx) + (p.Y - _gy) * (p.Y - _gy) <= (CenterR + 4) * (CenterR + 4);

        protected override HitTestResult? HitTestCore(PointHitTestParameters p)
        {
            if (!_shown) return null;
            var pos = p.HitPoint;
            if (_drag != Axis.None) return new PointHitTestResult(this, pos);
            if (HitAxis(pos) != Axis.None || NearCenter(pos)) return new PointHitTestResult(this, pos);
            return null;
        }

        // ---- mouse interaction ----

        protected override void OnMouseMove(MouseEventArgs e)
        {
            base.OnMouseMove(e);
            if (!_shown) return;
            var pos = e.GetPosition(this);

            if (_drag != Axis.None)
            {
                // Distance along the axis's on-screen direction, in DIPs → world units for that axis
                var d = Dir(_drag);
                var delta = pos - _dragOrigin;
                double along = delta.X * d.X + delta.Y * d.Y;
                float world = (float)(along * UnitsPerDip(_drag));
                if (Keyboard.Modifiers.HasFlag(ModifierKeys.Control)) world = MathF.Round(world / 50f) * 50f; // Ctrl: 50-unit steps

                float nx = _dragAx0, ny = _dragAy0, nz = _dragAz0;
                if (_drag == Axis.X) nx += world; else if (_drag == Axis.Y) ny += world; else nz += world;
                _ax = nx; _ay = ny; _az = nz;

                // Arrows follow the cursor right away (no round trip)
                double moved = world / UnitsPerDip(_drag);
                _gx = _dragGx0 + d.X * moved; _gy = _dragGy0 + d.Y * moved;
                Layout();

                QueueMove(nx, ny, nz);
                Cursor = Cursors.SizeAll;
                e.Handled = true;
                return;
            }

            var hit = HitAxis(pos);
            if (hit != _hover) { _hover = hit; PaintColors(); }
            Cursor = hit != Axis.None || NearCenter(pos) ? Cursors.SizeAll : Cursors.Arrow;
        }

        protected override void OnMouseLeftButtonDown(MouseButtonEventArgs e)
        {
            base.OnMouseLeftButtonDown(e);
            if (!_shown) return;
            var pos = e.GetPosition(this);
            var hit = HitAxis(pos);
            if (hit == Axis.None) return;
            _drag = hit;
            _dragOrigin = pos;
            _dragGx0 = _gx; _dragGy0 = _gy;
            _dragAx0 = _ax; _dragAy0 = _ay; _dragAz0 = _az;
            DragStarted?.Invoke(_actorIdx, _ax, _ay, _az);
            CaptureMouse();
            PaintColors();
            e.Handled = true;
        }

        protected override void OnMouseLeftButtonUp(MouseButtonEventArgs e)
        {
            base.OnMouseLeftButtonUp(e);
            if (_drag == Axis.None) return;
            var axis = _drag;
            _drag = Axis.None;
            ReleaseMouseCapture();
            PaintColors();
            Cursor = Cursors.Arrow;
            QueueMove(_ax, _ay, _az); // make sure the final spot is the last one sent
            _log($"Moved actor {_actorIdx} to ({_ax:0.#}, {_ay:0.#}, {_az:0.#}) with the {axis} arrow.");
            DragCompleted?.Invoke(_actorIdx, _ax, _ay, _az);
            e.Handled = true;
        }

        protected override void OnMouseRightButtonDown(MouseButtonEventArgs e)
        {
            if (_drag == Axis.None) return;
            // Right click cancels the drag
            _ax = _dragAx0; _ay = _dragAy0; _az = _dragAz0;
            _gx = _dragGx0; _gy = _dragGy0;
            _drag = Axis.None;
            ReleaseMouseCapture();
            Layout();
            Cursor = Cursors.Arrow;
            QueueMove(_dragAx0, _dragAy0, _dragAz0);
            _log($"Gizmo drag cancelled — actor {_actorIdx} put back.");
            e.Handled = true;
        }

        // ---- coalesced moves ----

        void QueueMove(float x, float y, float z)
        {
            _pendingMove = (x, y, z);
            _lastLocalMove = DateTime.UtcNow;
            if (!_sending) SendLoop();
        }

        async void SendLoop()
        {
            _sending = true;
            try
            {
                while (_pendingMove is { } m)
                {
                    _pendingMove = null;
                    var pipe = Pipe;
                    if (pipe == null) break;
                    try { await pipe.SetActorPositionAsync(_actorIdx, m.X, m.Y, m.Z); } catch { }
                }
            }
            finally { _sending = false; _lastLocalMove = DateTime.UtcNow; }
        }
    }
}
