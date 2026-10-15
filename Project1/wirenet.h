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

// Remove duplicate points and merge collinear vertices.  The two end points
// are never removed, so port attachments stay intact.
void wnNormalize(std::vector<wxPoint>& pts);

// Logisim WiringTool: extend a path towards g while the pointer moves.
// Moving along the same axis stretches the last segment, changing axis turns
// the previous point into a corner, and coming back drops that corner again.
// path.front() is the fixed start of the run.
void wnExtendPath(std::vector<wxPoint>& path, const wxPoint& g);

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
