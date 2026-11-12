///////////////////////////////////////////////////////////////////////////
// wirenet.h -- orthogonal wire geometry + electrical net extraction.
//
// This module is deliberately independent from the rest of the editor: it
// only knows about grid points and orthogonal polylines.  Keeping it free
// of wxString / wxDC makes the tricky geometry testable on its own.
//
// Rules used everywhere in the editor:
//   * a wire is a polyline whose consecutive points share x or y (Manhattan)
//   * two wires are electrically connected as soon as they touch or cross
//   * a port only connects at the END of a polyline (Logisim behaviour)
///////////////////////////////////////////////////////////////////////////

#ifndef EDA_WIRENET_H
#define EDA_WIRENET_H

#include <wx/gdicmn.h>
#include <map>
#include <utility>
#include <vector>

// Lexicographic order, so a grid point can be used as a std::map key.
struct wnPtLess
{
    bool operator()(const wxPoint& a, const wxPoint& b) const
    {
        if (a.x != b.x) return a.x < b.x;
        return a.y < b.y;
    }
};

// Two points lie on a horizontal or vertical line.
bool wnAligned(const wxPoint& a, const wxPoint& b);

// Point lies on an axis aligned segment (endpoints included).
bool wnPointOnSeg(const wxPoint& p, const wxPoint& a, const wxPoint& b);

// Does any wire still pass through the point?  A connection mark whose wires
// are gone is rubbish -- it used to be painted forever while refusing to be
// clicked away, which is exactly why this is a function of its own.
bool wnOnWires(const std::vector<std::vector<wxPoint>>& polys, const wxPoint& p);

// Remove duplicate points and merge collinear vertices.  The two end points
// are never removed, so port attachments stay intact.
void wnNormalize(std::vector<wxPoint>& pts);

// Logisim WiringTool: extend a path towards g while the pointer moves.
// Moving along the same axis stretches the last segment, changing axis turns
// the previous point into a corner, and coming back drops that corner again.
// path.front() is the fixed start of the run.
void wnExtendPath(std::vector<wxPoint>& path, const wxPoint& g);

// ---------------------------------------------------------------------------
// Minimal routing
//
// A wire drawn by hand comes back with every accidental kink the hand made.
// Rather than store the shake, the editor stores the shortest orthogonal route
// between the two points the user actually cared about; extra bends only ever
// appear when the user asks for them (click-to-place, "add vertex").
// ---------------------------------------------------------------------------

// Which way the wire has to run next to a point.
//
// The directional values answer the question "on which side of this point does
// the wire lie?" -- WN_LEFT means the neighbouring grid point is at x-1.  That
// single meaning works for both ends of a route, which is what lets a pin say
// "come at me from the left" without any special casing at the call site.
enum wnSide
{
    WN_ANY = 0,   // no constraint
    WN_H,         // horizontally next to the point (either side)
    WN_V,         // vertically next to the point (either side)
    WN_LEFT,      // the wire lies to the left  (x-1)
    WN_RIGHT,     // the wire lies to the right (x+1)
    WN_UP,        // the wire lies above        (y-1)
    WN_DOWN       // the wire lies below        (y+1)
};

// The axis a route would naturally take from a to b (the dominant offset,
// ties going horizontal).
wnSide wnAxisSide(const wxPoint& a, const wxPoint& b);

// Shortest orthogonal route from a to b that respects the two sides.
// One segment when the points share an axis, one bend otherwise, and the
// classic "out, across, in" Z when both ends insist on the same axis and there
// is room for it.  Never a staircase.
void wnRoute(const wxPoint& a, const wxPoint& b, wnSide aSide, wnSide bSide,
             std::vector<wxPoint>& out);

// A blocked rectangle of grid cells, corners included.  Component bodies.
struct wnRect
{
    int x0 = 0, y0 = 0, x1 = -1, y1 = -1;
    bool Empty() const { return x1 < x0 || y1 < y0; }
};

// Route that keeps out of every blocked rectangle.
//
// The plain wnRoute answer is used unless something is really in the way, so a
// wire that has nothing to avoid costs exactly what it costs today.  Only when
// that answer crosses a component does a bounded grid search take over, and it
// is the search that produces the "out of the pin, around the body, back into
// the other pin" shape.
//
// Because a pin sits on the edge of its own body, the body also decides which
// side the wire may approach from: an input pin on the left edge can only be
// reached from the left.  The two end points themselves are exempt from the
// blocking test, which is exactly what lets a wire touch the pin it belongs to.
//
// Should even the search come up empty (a component walled in on every side)
// the plain route is returned, because a wire that crosses a body still
// conducts while a wire that was never created does not.
void wnRouteAvoid(const wxPoint& a, const wxPoint& b, wnSide aSide, wnSide bSide,
                  const std::vector<wnRect>& blocks, std::vector<wxPoint>& out);

// Append a leg to a run, dropping the duplicated joint point.
void wnJoinRun(std::vector<wxPoint>& run, const std::vector<wxPoint>& leg);

// Insert a stair shaped jog inside one segment ("add vertex").
bool wnInsertJog(std::vector<wxPoint>& pts, int seg);

// ---------------------------------------------------------------------------
// Net extraction
// ---------------------------------------------------------------------------

struct wnSegRef
{
    int     poly = -1;      // index of the polyline
    int     seg  = -1;      // index of the segment inside that polyline
    wxPoint a, b;
};

struct wnNet
{
    std::vector<wnSegRef>  segs;        // every segment of this net
    std::vector<int>       ports;       // indices into the caller's port list
    std::vector<wxPoint>   junctions;   // T / crossing points needing a dot
};

// Group every segment into electrical nets.  portPos uses the same indexing
// as the caller's port table; a port joins a net when it sits on the end of
// one of its segments.
//
// Logisim rule: two wires are only joined where an END POINT takes part --
// end against end, an end landing in the middle of another wire (T branch) or
// two collinear wires overlapping.  A plain crossing, where both wires simply
// pass through, is NOT a connection: the wires are independent.  junctionPos
// lists the points where the user explicitly asked for a connection, which is
// the only way to join a crossing.
//
// portAlias declares pairs of ports that are the SAME electrical point without
// touching at all.  A sub-circuit instance needs this: the port on the box edge
// and the pin inside the definition are one node even though the definition
// draws them far apart, and faking that with a hidden wire would risk the stub
// running over unrelated geometry.
std::vector<wnNet> wnBuildNets(const std::vector<std::vector<wxPoint>>& polys,
                               const std::vector<wxPoint>& portPos,
                               const std::vector<wxPoint>& junctionPos
                                   = std::vector<wxPoint>(),
                               const std::vector<std::pair<int, int>>& portAlias
                                   = std::vector<std::pair<int, int>>());

// A canonical description of which ports are electrically tied together.
// Port indices (not coordinates) are used, so two geometries can be compared
// even though the ports themselves moved.  Net order and port order are both
// normalised, and a port that is unconnected appears in no group at all.
//
// This is the invariant a move repair must preserve: dragging a component may
// stretch its wires, but must never merge two nets or orphan a port.
std::vector<std::vector<int>> wnPortGroups(
    const std::vector<std::vector<wxPoint>>& polys,
    const std::vector<wxPoint>& portPos,
    const std::vector<wxPoint>& junctionPos = std::vector<wxPoint>());

// How many junction dots the canvas would paint for this geometry.  Used to
// prefer the repair candidate that does not invent new connection marks.
int wnJunctionCount(const std::vector<std::vector<wxPoint>>& polys,
                    const std::vector<wxPoint>& junctionPos
                        = std::vector<wxPoint>());

// Stretch a polyline run so its ends land on na / nb while KEEPING the bend
// skeleton: only the bend next to a moved end slides along the run's last leg.
//
// The router used by a drag is free to drop interior bends, and on one real
// circuit that moved a wire onto the column of an unrelated run, shorting two
// nets.  Sliding the last bend instead keeps the original column/row, so the
// stretched wire still crosses the other run the way it did before the drag.
// slideOffset steps the sliding bend column/row sideways to get out of a
// corridor already occupied by another wire.  Zero is the plain stretch.
//
// The result is ALWAYS an orthogonal polyline with no zero-length legs, even
// when a two-point run's ends move diagonally (the old code emitted a slanted
// segment there).  Collinear reversal points -- a genuine spur another wire
// branches off -- are preserved rather than normalised away.
std::vector<wxPoint> wnStretchRun(const std::vector<wxPoint>& pts,
                                  const wxPoint& na, const wxPoint& nb,
                                  int slideOffset = 0);

// Every plausible skeleton-preserving, orthogonal reconnection of a run whose
// ends moved, ordered least to most invasive: bend slide, stub (bend kept, a
// corner inserted near the moved end) and lateral corridor shifts.  The drag
// repair scores these against the OTHER wires and keeps the one that preserves
// the electrical topology -- this is the "avoidance" fallback.
std::vector<std::vector<wxPoint>> wnStretchCandidates(
    const std::vector<wxPoint>& pts, const wxPoint& na, const wxPoint& nb,
    int maxOffset = 4);

// One run the drag repair has to re-lay.  `router` is the candidate the
// ordinary router produced; an empty router means it is not available.
struct wnMoveRun
{
    std::vector<wxPoint> pts;      // pre-drag skeleton, interior bends relocated
    wxPoint na, nb;                // where the two ends have to land
    std::vector<wxPoint> router;   // may be empty
};

// Choose one orthogonal path per run.  `ambient` polylines are always present
// (unchanged runs, dragged runs repaired segment by segment).
//
// Each candidate geometry is measured with the SAME net extractor the canvas
// draws with: port grouping must survive, no slanted segment may appear, no
// extra junction dot or collinear overlap may be invented.  A coordinate
// descent over the per-run candidate lists finds the least invasive geometry
// that satisfies all of them; the search never silently accepts a candidate
// that shorts two nets just because it is the last one tried.
//
// maxOffset < 0 disables the avoidance search (candidate 0 only), which is
// what callers with a changing port set need.
std::vector<std::vector<wxPoint>> wnChooseMovePaths(
    const std::vector<wnMoveRun>& runs,
    const std::vector<std::vector<wxPoint>>& ambient,
    const std::vector<std::vector<wxPoint>>& beforeSegs,
    const std::vector<wxPoint>& beforePorts,
    const std::vector<wxPoint>& afterPorts,
    const std::vector<wxPoint>& junctionPos,
    int maxOffset = 3);

// ---------------------------------------------------------------------------
// Move repair (Logisim "WireRepair")
//
// A drag is described by a table  old grid point -> new grid point.  Every
// segment is then re-laid from that table: an end that moved travels, an end
// that stayed is left alone, and a segment whose two ends no longer share an
// axis is bent into two orthogonal pieces.  Because the table is derived from
// the *pre-drag* geometry and applied to every segment at once, the operation
// is idempotent -- dragging twice does not pile up jogs.
// ---------------------------------------------------------------------------

// One piece produced by wnRelocate.  srcA / srcB tell which end of the input
// segment the corresponding point came from: 0 = old a, 1 = old b, -1 = the
// jog point that was inserted (so it has no port attachment).
struct wnRelocPiece
{
    wxPoint a, b;
    int     srcA = 0;
    int     srcB = 1;
};

// Build the displacement table for one drag.
//   segs      : every segment, at its pre-drag position
//   dragIdx   : which of those segments the user is dragging
//   portMove  : (old, new) position of every component port
//   delta     : the drag offset
// A port move wins over a wire move, and a point on a dragged segment travels
// with it -- including end points of *other* segments, which is what keeps a
// wire that was plugged into the middle of the dragged one connected.
std::map<wxPoint, wxPoint, wnPtLess>
wnBuildDisplacement(const std::vector<std::pair<wxPoint, wxPoint>>& segs,
                    const std::vector<size_t>& dragIdx,
                    const std::vector<std::pair<wxPoint, wxPoint>>& portMove,
                    const wxPoint& delta);

// Re-lay one segment whose ends moved from (oldA, oldB) to (newA, newB).
// Returns 1 piece when the two new ends are still on one axis and 2 pieces
// otherwise (or 0 when the segment collapsed to a single point).
std::vector<wnRelocPiece> wnRelocate(const wxPoint& oldA, const wxPoint& oldB,
                                     const wxPoint& newA, const wxPoint& newB);

#endif // EDA_WIRENET_H
