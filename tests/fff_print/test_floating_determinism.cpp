#include <catch2/catch.hpp>

#include <algorithm>
#include <cstring>
#include <cstdint>
#include <new>

#include "libslic3r/Arachne/utils/ExtrusionLine.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/EdgeGrid.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Fill/FillFloatingConcentric.hpp"
#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/PrintConfig.hpp"

using namespace Slic3r;

namespace {

// An 8 mm square loop (124..132 mm) with a collinear junction in the middle of every edge, rotated to begin at
// junction `start` and closed the way Arachne closes its loops: the last point duplicates the first.
Points square_loop(size_t start)
{
    const coord_t a = scaled<coord_t>(124.), m = scaled<coord_t>(128.), b = scaled<coord_t>(132.);
    const Points ring { {a, a}, {m, a}, {b, a}, {b, m}, {b, b}, {m, b}, {a, b}, {a, m} };
    Points out;
    for (size_t i = 0; i <= ring.size(); ++i)
        out.push_back(ring[(start + i) % ring.size()]);
    return out;
}

struct FloatingFillProbe : public FillFloatingConcentric
{
    using FillFloatingConcentric::resplit_order_loops;
    void set_config(const PrintObjectConfig *config) { this->print_object_config = config; }
};

} // namespace

TEST_CASE("EdgeGrid: a closed loop is not self-intersecting whatever its start vertex", "[EdgeGrid][FloatingConcentric]")
{
    for (size_t start = 0; start < 8; ++start) {
        const Points loop = square_loop(start);
        EdgeGrid::Grid grid;
        grid.set_bbox(BoundingBox(loop));
        // The call FillFloatingConcentric::resplit_order_loops makes after the fix.
        const std::vector<Points> contours{ loop };
        grid.create(contours, scaled<coord_t>(10.), true);
        INFO("start junction " << start);
        CHECK_FALSE(grid.has_intersecting_edges());
    }
}

TEST_CASE("Floating vertical shell detection does not depend on where the Arachne loop starts", "[FloatingConcentric]")
{
    PrintObjectConfig config;
    config.detect_floating_vertical_shell.value = true;
    const coord_t lo = scaled<coord_t>(123.), hi = scaled<coord_t>(133.);
    // The whole loop lies over the lower layer's sparse-infill area, so every vertex must be marked floating.
    const ExPolygons floating { ExPolygon(Polygon({ {lo, lo}, {hi, lo}, {hi, hi}, {lo, hi} })) };
    const coord_t width = scaled<coord_t>(0.34);
    for (size_t start = 0; start < 8; ++start) {
        Arachne::ExtrusionLine line(0, false, true);
        for (const Point &p : square_loop(start))
            line.junctions.emplace_back(p, width, 0);
        FloatingFillProbe fill;
        fill.set_config(&config);
        const FloatingThickPolylines out = fill.resplit_order_loops(Point(0, 0), { &line }, floating, Polygons(), width);
        REQUIRE(out.size() == 1);
        INFO("start junction " << start);
        CHECK(std::all_of(out.front().is_floating.begin(), out.front().is_floating.end(), [](bool f) { return f; }));
    }
}

TEST_CASE("PlateBBoxData::first_layer_time is initialised by default initialisation", "[ThumbnailData]")
{
    // PartPlate holds a PlateBBoxData member and is created with `new PartPlate(...)` (default initialisation),
    // which is exactly what this placement new over a poisoned buffer reproduces.
    alignas(PlateBBoxData) unsigned char buffer[sizeof(PlateBBoxData)];
    std::memset(buffer, 0xAA, sizeof(buffer));
    PlateBBoxData *data = new (buffer) PlateBBoxData;
    // Observe the object representation, avoiding an lvalue-to-rvalue read of the old indeterminate float.
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    std::uint32_t bits;
    std::memcpy(&bits, &data->first_layer_time, sizeof(bits));
    CHECK(bits == 0);
    data->~PlateBBoxData();
}

TEST_CASE("EdgeGrid still recognises a genuinely intersecting closed path", "[EdgeGrid][FloatingConcentric]")
{
    const coord_t a = scaled<coord_t>(124.), b = scaled<coord_t>(132.);
    const Points bow_tie { {a, a}, {b, b}, {a, b}, {b, a}, {a, a} };
    const std::vector<Points> contours{ bow_tie };
    EdgeGrid::Grid grid;
    grid.set_bbox(BoundingBox(bow_tie));
    grid.create(contours, scaled<coord_t>(10.), true);
    CHECK(grid.has_intersecting_edges());
}
