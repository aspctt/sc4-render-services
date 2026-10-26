#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <vector>

#include "cIGZWorldDrawService.h"

/// A point on the ground, in metres.
struct SC4WorldPoint2
{
    float x;
    float z;
};

/// Builds triangles for cIGZWorldDrawService layers and draw callbacks.
///
/// Ground shapes take points on the ground and follow the terrain: they are cut into pieces no
/// longer than SetMaxEdgeLength and each corner is placed at the terrain height, lifted a few
/// centimetres. Call DrapeOnTerrain once with the service, or UseFlatGround for a fixed height.
///
///     SC4WorldMeshBuilder mesh;
///     mesh.DrapeOnTerrain(worldDraw);
///     mesh.AddGroundRectOutline({x, z}, 16.0f, 16.0f, 0.0f, 0.5f, SC4WorldColor(1, 1, 0));
///     worldDraw->SetLayerTriangles(layer, mesh.Data(), mesh.Count());
///
/// Everything runs in your plugin. Nothing here calls the game except terrain heights.
class SC4WorldMeshBuilder
{
public:
    /// Places ground shapes on the terrain, lifted by `lift` metres so they don't sink into it.
    void DrapeOnTerrain(cIGZWorldDrawService* service, const float lift = 0.05f)
    {
        terrain_ = service;
        lift_ = lift;
    }

    /// Places ground shapes on a flat plane at height y instead of the terrain.
    void UseFlatGround(const float y)
    {
        terrain_ = nullptr;
        flatY_ = y;
    }

    /// Ground shapes are cut into pieces no longer than this so they follow hills. Default 4 m.
    void SetMaxEdgeLength(const float meters) { maxEdge_ = (std::max)(0.25f, meters); }

    void Clear() { vertices_.clear(); }
    void Reserve(const size_t vertexCount) { vertices_.reserve(vertexCount); }

    [[nodiscard]] const std::vector<SC4WorldVertex>& Vertices() const { return vertices_; }
    [[nodiscard]] const SC4WorldVertex* Data() const { return vertices_.data(); }
    [[nodiscard]] uint32_t Count() const { return static_cast<uint32_t>(vertices_.size()); }
    [[nodiscard]] bool Empty() const { return vertices_.empty(); }

    /// The height ground shapes use at a point, lift included.
    [[nodiscard]] float GroundHeight(const float x, const float z) const
    {
        float height = 0.0f;
        if (terrain_ && terrain_->GetTerrainHeight(x, z, height)) {
            return height + lift_;
        }
        return terrain_ ? lift_ : flatY_;
    }

    /** @name Shapes in 3D */
    ///@{
    void AddTriangle(const SC4WorldVertex& a, const SC4WorldVertex& b, const SC4WorldVertex& c)
    {
        vertices_.push_back(a);
        vertices_.push_back(b);
        vertices_.push_back(c);
    }

    /// A quad with its corners in order around the edge.
    void AddQuad(const SC4WorldVertex& a, const SC4WorldVertex& b, const SC4WorldVertex& c, const SC4WorldVertex& d)
    {
        AddTriangle(a, b, c);
        AddTriangle(a, c, d);
    }

    /// A flat band between two points, `width` metres wide. A vertical band becomes two
    /// crossed bands so it stays visible from every side.
    void AddLine(const float x0, const float y0, const float z0, const float x1, const float y1, const float z1,
                 const float width, const uint32_t color)
    {
        const SC4WorldVertex points[2] = {{x0, y0, z0, color, 0, 0}, {x1, y1, z1, color, 0, 0}};
        AddPolyline(points, 2, width, false);
    }

    /// A flat band through the points, using each point's colour. Corners are mitred.
    void AddPolyline(const SC4WorldVertex* points, const uint32_t count, const float width, const bool closed = false)
    {
        if (!points || count < 2 || width <= 0.0f) {
            return;
        }
        const uint32_t segmentCount = closed ? count : count - 1;
        const float half = width * 0.5f;
        for (uint32_t i = 0; i < segmentCount; ++i) {
            const SC4WorldVertex& a = points[i];
            const SC4WorldVertex& b = points[(i + 1) % count];
            float dx = b.x - a.x;
            float dz = b.z - a.z;
            const float horizontal = std::sqrt(dx * dx + dz * dz);
            const float dy = b.y - a.y;
            if (horizontal < 1e-4f && std::fabs(dy) < 1e-4f) {
                continue;
            }
            if (horizontal < 0.05f * std::fabs(dy)) {
                AddCrossedBand_(a, b, half);
                continue;
            }
            // Each end is pushed out along the corner's mitre, widened so the band keeps its width.
            const float nx = -dz / horizontal;
            const float nz = dx / horizontal;
            float ax = nx, az = nz, bx = nx, bz = nz;
            MiterDirection_(points, count, i, closed, ax, az);
            MiterDirection_(points, count, (i + 1) % count, closed, bx, bz);
            const float ha = half / (std::max)(0.25f, ax * nx + az * nz);
            const float hb = half / (std::max)(0.25f, bx * nx + bz * nz);
            AddQuad({a.x + ax * ha, a.y, a.z + az * ha, a.color, 0, 0},
                    {b.x + bx * hb, b.y, b.z + bz * hb, b.color, 0, 0},
                    {b.x - bx * hb, b.y, b.z - bz * hb, b.color, 0, 0},
                    {a.x - ax * ha, a.y, a.z - az * ha, a.color, 0, 0});
        }
    }

    /// A solid box between two corners.
    void AddBox(const float minX, const float minY, const float minZ, const float maxX, const float maxY,
                const float maxZ, const uint32_t color)
    {
        const SC4WorldVertex p[8] = {
            {minX, minY, minZ, color, 0, 0}, {maxX, minY, minZ, color, 0, 0},
            {maxX, minY, maxZ, color, 0, 0}, {minX, minY, maxZ, color, 0, 0},
            {minX, maxY, minZ, color, 0, 0}, {maxX, maxY, minZ, color, 0, 0},
            {maxX, maxY, maxZ, color, 0, 0}, {minX, maxY, maxZ, color, 0, 0},
        };
        AddQuad(p[0], p[3], p[2], p[1]);
        AddQuad(p[4], p[5], p[6], p[7]);
        AddQuad(p[0], p[1], p[5], p[4]);
        AddQuad(p[1], p[2], p[6], p[5]);
        AddQuad(p[2], p[3], p[7], p[6]);
        AddQuad(p[3], p[0], p[4], p[7]);
    }

    /// A vertical post standing on the ground, `height` metres tall and `width` metres wide.
    void AddMarker(const SC4WorldPoint2 at, const float height, const float width, const uint32_t color)
    {
        const float base = GroundHeight(at.x, at.z);
        AddLine(at.x, base, at.z, at.x, base + height, at.z, width, color);
    }
    ///@}

    /** @name Shapes on the ground */
    ///@{
    /// A filled triangle. UVs run from (0,0) at a to (1,0) at b and (0,1) at c.
    void AddGroundTriangle(const SC4WorldPoint2 a, const SC4WorldPoint2 b, const SC4WorldPoint2 c, const uint32_t color)
    {
        AddGroundTriangle_({a.x, a.z, 0, 0}, {b.x, b.z, 1, 0}, {c.x, c.z, 0, 1}, color, 0);
    }

    /// A band along the ground between two points, `width` metres wide.
    void AddGroundLine(const SC4WorldPoint2 from, const SC4WorldPoint2 to, const float width, const uint32_t color)
    {
        const SC4WorldPoint2 points[2] = {from, to};
        AddGroundPolyline(points, 2, width, color, false);
    }

    /// A band along the ground through the points, `width` metres wide.
    void AddGroundPolyline(const SC4WorldPoint2* points, const uint32_t count, const float width, const uint32_t color,
                           const bool closed = false)
    {
        if (!points || count < 2 || width <= 0.0f) {
            return;
        }
        // Cut long segments so the band follows the terrain, then build it in the ground plane
        // and drop each corner onto the terrain.
        std::vector<SC4WorldPoint2> dense;
        dense.reserve(count * 2);
        const uint32_t segmentCount = closed ? count : count - 1;
        for (uint32_t i = 0; i < segmentCount; ++i) {
            const SC4WorldPoint2 a = points[i];
            const SC4WorldPoint2 b = points[(i + 1) % count];
            const float length = std::hypot(b.x - a.x, b.z - a.z);
            if (length < 1e-4f) {
                continue;
            }
            const int steps = (std::max)(1, static_cast<int>(std::ceil(length / maxEdge_)));
            for (int s = 0; s < steps; ++s) {
                const float t = static_cast<float>(s) / static_cast<float>(steps);
                dense.push_back({a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t});
            }
        }
        if (!closed) {
            dense.push_back(points[count - 1]);
        }
        const auto n = static_cast<uint32_t>(dense.size());
        if (n < 2) {
            return;
        }

        std::vector<SC4WorldPoint2> left(n);
        std::vector<SC4WorldPoint2> right(n);
        const float half = width * 0.5f;
        for (uint32_t i = 0; i < n; ++i) {
            const bool hasPrev = closed || i > 0;
            const bool hasNext = closed || i + 1 < n;
            const SC4WorldPoint2 p = dense[i];
            float nx = 0.0f;
            float nz = 0.0f;
            float px = 0.0f, pz = 0.0f, qx = 0.0f, qz = 0.0f;
            if (hasPrev) {
                const SC4WorldPoint2 prev = dense[(i + n - 1) % n];
                Normal_(prev.x, prev.z, p.x, p.z, px, pz);
                nx += px;
                nz += pz;
            }
            if (hasNext) {
                const SC4WorldPoint2 next = dense[(i + 1) % n];
                Normal_(p.x, p.z, next.x, next.z, qx, qz);
                nx += qx;
                nz += qz;
            }
            float scale = 1.0f;
            const float len = std::sqrt(nx * nx + nz * nz);
            if (len > 1e-6f) {
                nx /= len;
                nz /= len;
                const float refX = hasNext ? qx : px;
                const float refZ = hasNext ? qz : pz;
                scale = 1.0f / (std::max)(0.25f, nx * refX + nz * refZ);
            }
            left[i] = {p.x + nx * half * scale, p.z + nz * half * scale};
            right[i] = {p.x - nx * half * scale, p.z - nz * half * scale};
        }
        const uint32_t quads = closed ? n : n - 1;
        for (uint32_t i = 0; i < quads; ++i) {
            const uint32_t j = (i + 1) % n;
            AddQuad(Ground_(left[i], color), Ground_(left[j], color), Ground_(right[j], color), Ground_(right[i], color));
        }
    }

    void AddGroundPolyline(const std::initializer_list<SC4WorldPoint2> points, const float width, const uint32_t color,
                           const bool closed = false)
    {
        AddGroundPolyline(points.begin(), static_cast<uint32_t>(points.size()), width, color, closed);
    }

    /// A filled rectangle around a centre, rotated by `rotation` radians. UVs run 0 to 1 across
    /// it, so it can show a texture.
    void AddGroundRect(const SC4WorldPoint2 center, const float width, const float length, const float rotation,
                       const uint32_t color)
    {
        SC4WorldPoint2 c[4];
        RectCorners_(center, width, length, rotation, c);
        AddGroundTriangle_({c[0].x, c[0].z, 0, 0}, {c[1].x, c[1].z, 1, 0}, {c[2].x, c[2].z, 1, 1}, color, 0);
        AddGroundTriangle_({c[0].x, c[0].z, 0, 0}, {c[2].x, c[2].z, 1, 1}, {c[3].x, c[3].z, 0, 1}, color, 0);
    }

    /// The outline of a rectangle, `lineWidth` metres wide.
    void AddGroundRectOutline(const SC4WorldPoint2 center, const float width, const float length, const float rotation,
                              const float lineWidth, const uint32_t color)
    {
        SC4WorldPoint2 c[4];
        RectCorners_(center, width, length, rotation, c);
        AddGroundPolyline(c, 4, lineWidth, color, true);
    }

    /// A filled game tile, 16 by 16 metres. `inset` shrinks it on every side.
    void AddGroundTile(const int32_t tileX, const int32_t tileZ, const uint32_t color, const float inset = 0.0f)
    {
        const float x0 = static_cast<float>(tileX) * kTileSize + inset;
        const float z0 = static_cast<float>(tileZ) * kTileSize + inset;
        const float size = kTileSize - 2.0f * inset;
        AddGroundRect({x0 + size * 0.5f, z0 + size * 0.5f}, size, size, 0.0f, color);
    }

    /// A filled circle. `segments` 0 picks a count that looks round at this size.
    void AddGroundCircle(const SC4WorldPoint2 center, const float radius, const uint32_t color, uint32_t segments = 0)
    {
        if (radius <= 0.0f) {
            return;
        }
        segments = segments ? segments : AutoSegments_(radius);
        for (uint32_t i = 0; i < segments; ++i) {
            const SC4WorldPoint2 a = CirclePoint_(center, radius, i, segments);
            const SC4WorldPoint2 b = CirclePoint_(center, radius, i + 1, segments);
            AddGroundTriangle_({center.x, center.z, 0.5f, 0.5f},
                               {a.x, a.z, 0.5f + (a.x - center.x) / (2 * radius), 0.5f + (a.z - center.z) / (2 * radius)},
                               {b.x, b.z, 0.5f + (b.x - center.x) / (2 * radius), 0.5f + (b.z - center.z) / (2 * radius)},
                               color, 0);
        }
    }

    /// The outline of a circle, `lineWidth` metres wide.
    void AddGroundRing(const SC4WorldPoint2 center, const float radius, const float lineWidth, const uint32_t color,
                       uint32_t segments = 0)
    {
        if (radius <= 0.0f) {
            return;
        }
        segments = segments ? segments : AutoSegments_(radius);
        std::vector<SC4WorldPoint2> points(segments);
        for (uint32_t i = 0; i < segments; ++i) {
            points[i] = CirclePoint_(center, radius, i, segments);
        }
        AddGroundPolyline(points.data(), segments, lineWidth, color, true);
    }

    /// A filled polygon. Any simple outline works, convex or not, in either direction.
    void AddGroundPolygon(const SC4WorldPoint2* points, const uint32_t count, const uint32_t color)
    {
        if (!points || count < 3) {
            return;
        }
        float minX = points[0].x, maxX = points[0].x, minZ = points[0].z, maxZ = points[0].z;
        for (uint32_t i = 1; i < count; ++i) {
            minX = (std::min)(minX, points[i].x);
            maxX = (std::max)(maxX, points[i].x);
            minZ = (std::min)(minZ, points[i].z);
            maxZ = (std::max)(maxZ, points[i].z);
        }
        const float spanX = (std::max)(maxX - minX, 1e-4f);
        const float spanZ = (std::max)(maxZ - minZ, 1e-4f);
        auto uv = [&](const SC4WorldPoint2 p) {
            return GroundPoint_{p.x, p.z, (p.x - minX) / spanX, (p.z - minZ) / spanZ};
        };

        // Ear clipping on a counter-clockwise copy of the outline.
        std::vector<uint32_t> index(count);
        for (uint32_t i = 0; i < count; ++i) {
            index[i] = i;
        }
        float area = 0.0f;
        for (uint32_t i = 0; i < count; ++i) {
            const SC4WorldPoint2 a = points[i];
            const SC4WorldPoint2 b = points[(i + 1) % count];
            area += a.x * b.z - b.x * a.z;
        }
        if (area < 0.0f) {
            std::reverse(index.begin(), index.end());
        }
        uint32_t guard = count * count;
        while (index.size() > 3 && guard-- > 0) {
            bool clipped = false;
            const auto n = static_cast<uint32_t>(index.size());
            for (uint32_t i = 0; i < n; ++i) {
                const SC4WorldPoint2 a = points[index[(i + n - 1) % n]];
                const SC4WorldPoint2 b = points[index[i]];
                const SC4WorldPoint2 c = points[index[(i + 1) % n]];
                if (Cross_(a, b, c) <= 0.0f) {
                    continue;
                }
                bool contains = false;
                for (uint32_t k = 0; k < n && !contains; ++k) {
                    const uint32_t v = index[k];
                    if (v == index[(i + n - 1) % n] || v == index[i] || v == index[(i + 1) % n]) {
                        continue;
                    }
                    contains = InTriangle_(points[v], a, b, c);
                }
                if (contains) {
                    continue;
                }
                AddGroundTriangle_(uv(a), uv(b), uv(c), color, 0);
                index.erase(index.begin() + i);
                clipped = true;
                break;
            }
            if (!clipped) {
                break;
            }
        }
        // What is left is a triangle, or an outline that crosses itself. Fan it.
        for (size_t i = 1; i + 1 < index.size(); ++i) {
            AddGroundTriangle_(uv(points[index[0]]), uv(points[index[i]]), uv(points[index[i + 1]]), color, 0);
        }
    }

    void AddGroundPolygon(const std::initializer_list<SC4WorldPoint2> points, const uint32_t color)
    {
        AddGroundPolygon(points.begin(), static_cast<uint32_t>(points.size()), color);
    }

    /// Grid lines every `spacing` metres between two corners, `lineWidth` metres wide.
    void AddGroundGrid(const SC4WorldPoint2 min, const SC4WorldPoint2 max, const float spacing, const float lineWidth,
                       const uint32_t color)
    {
        if (spacing <= 0.0f || max.x <= min.x || max.z <= min.z) {
            return;
        }
        for (float x = min.x; x <= max.x + 1e-3f; x += spacing) {
            AddGroundLine({x, min.z}, {x, max.z}, lineWidth, color);
        }
        for (float z = min.z; z <= max.z + 1e-3f; z += spacing) {
            AddGroundLine({min.x, z}, {max.x, z}, lineWidth, color);
        }
    }
    ///@}

    static constexpr float kTileSize = 16.0f;

private:
    struct GroundPoint_
    {
        float x;
        float z;
        float u;
        float v;
    };

    [[nodiscard]] SC4WorldVertex Ground_(const SC4WorldPoint2 p, const uint32_t color) const
    {
        return {p.x, GroundHeight(p.x, p.z), p.z, color, 0, 0};
    }

    [[nodiscard]] SC4WorldVertex Ground_(const GroundPoint_& p, const uint32_t color) const
    {
        return {p.x, GroundHeight(p.x, p.z), p.z, color, p.u, p.v};
    }

    void AddGroundTriangle_(const GroundPoint_& a, const GroundPoint_& b, const GroundPoint_& c, const uint32_t color,
                            const int depth)
    {
        // Split the longest edge until every edge is short enough to follow the terrain.
        const float ab = Distance2_(a, b);
        const float bc = Distance2_(b, c);
        const float ca = Distance2_(c, a);
        const float longest = (std::max)(ab, (std::max)(bc, ca));
        if (longest > maxEdge_ * maxEdge_ && depth < 12) {
            auto mid = [](const GroundPoint_& p, const GroundPoint_& q) {
                return GroundPoint_{(p.x + q.x) * 0.5f, (p.z + q.z) * 0.5f, (p.u + q.u) * 0.5f, (p.v + q.v) * 0.5f};
            };
            if (longest == ab) {
                const GroundPoint_ m = mid(a, b);
                AddGroundTriangle_(a, m, c, color, depth + 1);
                AddGroundTriangle_(m, b, c, color, depth + 1);
            }
            else if (longest == bc) {
                const GroundPoint_ m = mid(b, c);
                AddGroundTriangle_(a, b, m, color, depth + 1);
                AddGroundTriangle_(a, m, c, color, depth + 1);
            }
            else {
                const GroundPoint_ m = mid(c, a);
                AddGroundTriangle_(a, b, m, color, depth + 1);
                AddGroundTriangle_(m, b, c, color, depth + 1);
            }
            return;
        }
        AddTriangle(Ground_(a, color), Ground_(b, color), Ground_(c, color));
    }

    void AddCrossedBand_(const SC4WorldVertex& a, const SC4WorldVertex& b, const float half)
    {
        AddQuad({a.x - half, a.y, a.z, a.color, 0, 0}, {b.x - half, b.y, b.z, b.color, 0, 0},
                {b.x + half, b.y, b.z, b.color, 0, 0}, {a.x + half, a.y, a.z, a.color, 0, 0});
        AddQuad({a.x, a.y, a.z - half, a.color, 0, 0}, {b.x, b.y, b.z - half, b.color, 0, 0},
                {b.x, b.y, b.z + half, b.color, 0, 0}, {a.x, a.y, a.z + half, a.color, 0, 0});
    }

    // Unit normal of segment a to b in the ground plane.
    static void Normal_(const float ax, const float az, const float bx, const float bz, float& nx, float& nz)
    {
        const float dx = bx - ax;
        const float dz = bz - az;
        const float len = std::sqrt(dx * dx + dz * dz);
        nx = len > 1e-6f ? -dz / len : 0.0f;
        nz = len > 1e-6f ? dx / len : 0.0f;
    }

    // Averages the normals of the segments meeting at point i, for a mitred corner.
    static void MiterDirection_(const SC4WorldVertex* p, const uint32_t count, const uint32_t i, const bool closed,
                                float& nx, float& nz)
    {
        const bool hasPrev = closed || i > 0;
        const bool hasNext = closed || i + 1 < count;
        if (!hasPrev || !hasNext) {
            return;
        }
        const SC4WorldVertex& prev = p[(i + count - 1) % count];
        const SC4WorldVertex& next = p[(i + 1) % count];
        float ax, az, bx, bz;
        Normal_(prev.x, prev.z, p[i].x, p[i].z, ax, az);
        Normal_(p[i].x, p[i].z, next.x, next.z, bx, bz);
        const float sx = ax + bx;
        const float sz = az + bz;
        const float len = std::sqrt(sx * sx + sz * sz);
        if (len > 1e-6f && (ax != 0.0f || az != 0.0f) && (bx != 0.0f || bz != 0.0f)) {
            nx = sx / len;
            nz = sz / len;
        }
    }

    static void RectCorners_(const SC4WorldPoint2 center, const float width, const float length, const float rotation,
                             SC4WorldPoint2 out[4])
    {
        const float c = std::cos(rotation);
        const float s = std::sin(rotation);
        const float hw = width * 0.5f;
        const float hl = length * 0.5f;
        const float corners[4][2] = {{-hw, -hl}, {hw, -hl}, {hw, hl}, {-hw, hl}};
        for (int i = 0; i < 4; ++i) {
            out[i] = {center.x + corners[i][0] * c - corners[i][1] * s, center.z + corners[i][0] * s + corners[i][1] * c};
        }
    }

    static uint32_t AutoSegments_(const float radius)
    {
        return static_cast<uint32_t>(std::clamp(radius * 1.5f, 16.0f, 128.0f));
    }

    static SC4WorldPoint2 CirclePoint_(const SC4WorldPoint2 center, const float radius, const uint32_t i,
                                       const uint32_t segments)
    {
        const float angle = 6.28318530718f * static_cast<float>(i % segments) / static_cast<float>(segments);
        return {center.x + std::cos(angle) * radius, center.z + std::sin(angle) * radius};
    }

    static float Distance2_(const GroundPoint_& a, const GroundPoint_& b)
    {
        return (a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z);
    }

    static float Cross_(const SC4WorldPoint2 a, const SC4WorldPoint2 b, const SC4WorldPoint2 c)
    {
        return (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
    }

    static bool InTriangle_(const SC4WorldPoint2 p, const SC4WorldPoint2 a, const SC4WorldPoint2 b,
                            const SC4WorldPoint2 c)
    {
        return Cross_(a, b, p) >= 0.0f && Cross_(b, c, p) >= 0.0f && Cross_(c, a, p) >= 0.0f;
    }

    std::vector<SC4WorldVertex> vertices_;
    cIGZWorldDrawService* terrain_ = nullptr;
    float lift_ = 0.05f;
    float flatY_ = 0.0f;
    float maxEdge_ = 4.0f;
};
