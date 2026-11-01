#include "SceneFile.h"

#include <pxr/pxr.h>
#include <pxr/base/gf/vec2d.h>
#include <pxr/base/plug/registry.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/shader.h>

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
std::string utf8(const std::filesystem::path& path)
{
	const auto bytes = path.u8string();
	return {bytes.begin(), bytes.end()};
}

double cross2(const GfVec2d& a, const GfVec2d& b, const GfVec2d& c)
{
	return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
}

// Ear clipping supports concave faces, unlike a triangle fan.
std::vector<std::array<size_t, 3>> triangulate(const std::vector<GfVec3d>& points)
{
	GfVec3d normal(0);
	for (size_t index = 0; index < points.size(); ++index)
	{
		const auto& a = points[index];
		const auto& b = points[(index + 1) % points.size()];
		normal += GfVec3d((a[1] - b[1]) * (a[2] + b[2]), (a[2] - b[2]) * (a[0] + b[0]), (a[0] - b[0]) * (a[1] + b[1]));
	}
	size_t dominant = 0;
	for (size_t axis = 1; axis < 3; ++axis)
	{
		if (std::abs(normal[axis]) > std::abs(normal[dominant]))
		{
			dominant = axis;
		}
	}
	std::vector<GfVec2d> projected;
	for (const auto& point : points)
	{
		projected.emplace_back(point[(dominant + 1) % 3], point[(dominant + 2) % 3]);
	}
	double area = 0;
	for (size_t index = 0; index < projected.size(); ++index)
	{
		const auto& a = projected[index];
		const auto& b = projected[(index + 1) % projected.size()];
		area += a[0] * b[1] - a[1] * b[0];
	}
	const double sign = area < 0 ? -1.0 : 1.0;
	const double epsilon = (std::max)(1e-18, std::abs(area) * 1e-12);
	std::vector<size_t> ring(points.size());
	std::iota(ring.begin(), ring.end(), size_t(0));
	std::vector<std::array<size_t, 3>> triangles;
	while (ring.size() > 3)
	{
		bool clipped = false;
		for (size_t index = 0; index < ring.size(); ++index)
		{
			const size_t a = ring[(index + ring.size() - 1) % ring.size()];
			const size_t b = ring[index];
			const size_t c = ring[(index + 1) % ring.size()];
			const double corner = cross2(projected[a], projected[b], projected[c]) * sign;
			if (std::abs(corner) <= epsilon)
			{
				ring.erase(ring.begin() + static_cast<ptrdiff_t>(index));
				clipped = true;
				break;
			}
			if (corner < 0)
			{
				continue;
			}
			bool containsPoint = false;
			for (const size_t point : ring)
			{
				if (point == a || point == b || point == c)
				{
					continue;
				}
				if (cross2(projected[a], projected[b], projected[point]) * sign >= -epsilon
					&& cross2(projected[b], projected[c], projected[point]) * sign >= -epsilon
					&& cross2(projected[c], projected[a], projected[point]) * sign >= -epsilon)
				{
					containsPoint = true;
					break;
				}
			}
			if (!containsPoint)
			{
				triangles.push_back({a, b, c});
				ring.erase(ring.begin() + static_cast<ptrdiff_t>(index));
				clipped = true;
				break;
			}
		}
		if (!clipped)
		{
			throw std::runtime_error("Cannot triangulate a self-intersecting or non-simple USD face.");
		}
	}
	if (ring.size() == 3)
	{
		triangles.push_back({ring[0], ring[1], ring[2]});
	}
	return triangles;
}

GfVec3f samplePrimvar(const VtVec3fArray& values, const TfToken& interpolation, size_t face,
	size_t point, size_t corner, const GfVec3f& fallback)
{
	size_t index = 0;
	if (interpolation == UsdGeomTokens->uniform)
	{
		index = face;
	}
	else if (interpolation == UsdGeomTokens->vertex || interpolation == UsdGeomTokens->varying)
	{
		index = point;
	}
	else if (interpolation == UsdGeomTokens->faceVarying)
	{
		index = corner;
	}
	return index < values.size() ? values[index] : fallback;
}

std::optional<GfVec3f> materialColor(const UsdPrim& prim, UsdTimeCode time)
{
	const UsdShadeMaterial material = UsdShadeMaterialBindingAPI(prim).ComputeBoundMaterial();
	if (!material)
	{
		return std::nullopt;
	}
	const UsdShadeShader shader = material.ComputeSurfaceSource();
	if (shader)
	{
		for (const char* inputName : {"diffuseColor", "baseColor"})
		{
			GfVec3f color;
			const auto input = shader.GetInput(TfToken(inputName));
			if (input && input.Get(&color, time))
			{
				return color;
			}
		}
	}
	return std::nullopt;
}

class Converter final
{
public:
	SceneData convert(const UsdStageRefPtr& stage)
	{
		mTime = UsdTimeCode(stage->GetStartTimeCode());
		mTransforms.SetTime(mTime);
		mZUp = UsdGeomGetStageUpAxis(stage) == UsdGeomTokens->z;
		mMetersPerUnit = UsdGeomGetStageMetersPerUnit(stage);
		if (!std::isfinite(mMetersPerUnit) || mMetersPerUnit <= 0)
		{
			throw std::runtime_error("USD stage has invalid metersPerUnit metadata.");
		}
		for (const auto& prim : UsdPrimRange::Stage(stage, UsdTraverseInstanceProxies()))
		{
			const UsdGeomImageable imageable(prim);
			if (imageable && (imageable.ComputeVisibility(mTime) == UsdGeomTokens->invisible
				|| imageable.ComputePurpose() == UsdGeomTokens->guide || imageable.ComputePurpose() == UsdGeomTokens->proxy))
			{
				continue;
			}
			if (prim.IsA<UsdGeomPointInstancer>())
			{
				throw std::runtime_error("PointInstancer is not supported yet; realize its instances before conversion.");
			}
			if (prim.IsA<UsdGeomMesh>())
			{
				appendMesh(UsdGeomMesh(prim));
			}
		}
		if (!stage->GetCompositionErrors().empty())
		{
			throw std::runtime_error("USD composition has errors. Check that all references and payloads are present.");
		}
		if (mScene.mVertices.empty())
		{
			throw std::runtime_error("No visible mesh triangles were found in the USD stage.");
		}
		std::printf("Converted %llu mesh occurrences, %llu triangles; units converted to meters, up axis %s -> Y.\n",
			static_cast<unsigned long long>(mScene.mHeader.mMeshCount),
			static_cast<unsigned long long>(mScene.mVertices.size() / 3), mZUp ? "Z" : "Y");
		if (mSubdivisionMeshes)
		{
			std::printf("Note: %zu subdivision meshes exported as their polygon control cages (no subdivision evaluation).\n", mSubdivisionMeshes);
		}
		std::puts("Static mesh preview: colors use displayColor or constant diffuse/base color; texture/shader networks, animation and transparency are not baked.");
		return std::move(mScene);
	}

private:
	GfVec3d yUp(const GfVec3d& value) const
	{
		return mZUp ? GfVec3d(value[0], value[2], -value[1]) : value;
	}

	void appendMesh(const UsdGeomMesh& mesh)
	{
		VtVec3fArray points;
		VtIntArray counts, indices, holes;
		if (!mesh.GetPointsAttr().Get(&points, mTime) || !mesh.GetFaceVertexCountsAttr().Get(&counts, mTime)
			|| !mesh.GetFaceVertexIndicesAttr().Get(&indices, mTime))
		{
			throw std::runtime_error("USD mesh is missing topology: " + mesh.GetPath().GetString());
		}
		mesh.GetHoleIndicesAttr().Get(&holes, mTime);
		const std::set<int> holeSet(holes.begin(), holes.end());
		const GfMatrix4d world = mTransforms.GetLocalToWorldTransform(mesh.GetPrim());
		if (std::abs(world.GetDeterminant3()) < 1e-16)
		{
			std::fprintf(stderr, "Skipping singular transform: %s\n", mesh.GetPath().GetText());
			return;
		}
		const GfMatrix4d normalTransform = world.GetInverse().GetTranspose();
		TfToken orientation;
		mesh.GetOrientationAttr().Get(&orientation, mTime);
		const bool flip = (orientation == UsdGeomTokens->leftHanded) != (world.GetDeterminant3() < 0);
		TfToken subdivision;
		mesh.GetSubdivisionSchemeAttr().Get(&subdivision, mTime);
		if (subdivision != UsdGeomTokens->none)
		{
			++mSubdivisionMeshes;
		}
		const auto colorPrimvar = UsdGeomPrimvarsAPI(mesh).FindPrimvarWithInheritance(TfToken("displayColor"));
		VtVec3fArray colors, normals;
		TfToken colorInterpolation = UsdGeomTokens->constant;
		if (colorPrimvar)
		{
			colorPrimvar.ComputeFlattened(&colors, mTime);
			colorInterpolation = colorPrimvar.GetInterpolation();
		}
		const auto normalPrimvar = UsdGeomPrimvarsAPI(mesh).GetPrimvar(TfToken("normals"));
		TfToken normalInterpolation = mesh.GetNormalsInterpolation();
		if (normalPrimvar)
		{
			normalPrimvar.ComputeFlattened(&normals, mTime);
			normalInterpolation = normalPrimvar.GetInterpolation();
		}
		else
		{
			mesh.GetNormalsAttr().Get(&normals, mTime);
		}
		const auto boundColor = materialColor(mesh.GetPrim(), mTime);
		std::vector<std::optional<GfVec3f>> faceColors(counts.size(), boundColor);
		for (const auto& child : mesh.GetPrim().GetChildren())
		{
			const UsdGeomSubset subset(child);
			if (!subset)
			{
				continue;
			}
			TfToken elementType;
			subset.GetElementTypeAttr().Get(&elementType, mTime);
			const auto color = materialColor(child, mTime);
			if (elementType == UsdGeomTokens->face && color)
			{
				VtIntArray faces;
				subset.GetIndicesAttr().Get(&faces, mTime);
				for (const int face : faces)
				{
					if (face >= 0 && static_cast<size_t>(face) < faceColors.size())
					{
						faceColors[face] = color;
					}
				}
			}
		}
		size_t offset = 0;
		const size_t firstVertex = mScene.mVertices.size();
		for (size_t face = 0; face < counts.size(); ++face)
		{
			if (counts[face] < 0 || static_cast<size_t>(counts[face]) > indices.size() - offset)
			{
				throw std::runtime_error("Invalid USD face counts: " + mesh.GetPath().GetString());
			}
			const size_t count = static_cast<size_t>(counts[face]);
			if (count < 3 || holeSet.contains(static_cast<int>(face)))
			{
				offset += count;
				continue;
			}
			std::vector<GfVec3d> facePoints;
			for (size_t corner = 0; corner < count; ++corner)
			{
				const int pointIndex = indices[offset + corner];
				if (pointIndex < 0 || static_cast<size_t>(pointIndex) >= points.size())
				{
					throw std::runtime_error("USD mesh index is out of range: " + mesh.GetPath().GetString());
				}
				facePoints.push_back(yUp(world.Transform(GfVec3d(points[pointIndex]))) * mMetersPerUnit);
			}
			for (auto triangle : triangulate(facePoints))
			{
				if (flip)
				{
					std::swap(triangle[1], triangle[2]);
				}
				GfVec3d faceNormal = GfCross(facePoints[triangle[1]] - facePoints[triangle[0]], facePoints[triangle[2]] - facePoints[triangle[0]]);
				if (faceNormal.Normalize() < 1e-14)
				{
					continue;
				}
				for (const size_t corner : triangle)
				{
					const size_t pointIndex = static_cast<size_t>(indices[offset + corner]);
					const GfVec3f sourceNormal = samplePrimvar(normals, normalInterpolation, face, pointIndex, offset + corner, GfVec3f(0));
					GfVec3d normal = yUp(normalTransform.TransformDir(GfVec3d(sourceNormal)));
					if (normal.Normalize() < 1e-10)
					{
						normal = faceNormal;
					}
					const GfVec3f color = faceColors[face].value_or(samplePrimvar(colors, colorInterpolation, face, pointIndex, offset + corner, GfVec3f(0.65f)));
					SceneVertex vertex;
					for (size_t axis = 0; axis < 3; ++axis)
					{
						vertex.mPosition[axis] = static_cast<float>(facePoints[corner][axis]);
						vertex.mNormal[axis] = static_cast<float>(normal[axis]);
						vertex.mColor[axis] = std::clamp(color[axis], 0.0f, 1.0f);
					}
					if (mScene.mVertices.size() >= 30000000)
					{
						throw std::runtime_error("Scene exceeds the 30-million-vertex preview limit.");
					}
					mScene.mVertices.push_back(vertex);
				}
			}
			offset += count;
		}
		if (offset != indices.size())
		{
			throw std::runtime_error("USD face counts do not match indices: " + mesh.GetPath().GetString());
		}
		if (mScene.mVertices.size() != firstVertex)
		{
			++mScene.mHeader.mMeshCount;
		}
	}

	SceneData mScene;
	UsdTimeCode mTime;
	UsdGeomXformCache mTransforms;
	bool mZUp = false;
	double mMetersPerUnit = 1.0;
	size_t mSubdivisionMeshes = 0;
};
}

int wmain(int argumentCount, wchar_t** arguments)
{
	try
	{
		if (argumentCount != 3)
		{
			throw std::runtime_error("Usage: UsdConverter.exe <source.usd/usda/usdc/usdz> <output.ptscene>");
		}
		std::array<wchar_t, 32768> executable{};
		const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
		if (length == 0 || length >= executable.size())
		{
			throw std::runtime_error("Cannot locate USD converter resources.");
		}
		PlugRegistry::GetInstance().RegisterPlugins(utf8(std::filesystem::path(executable.data()).parent_path() / L"usd"));
		const auto stage = UsdStage::Open(utf8(std::filesystem::path(arguments[1])), UsdStage::LoadAll);
		if (!stage)
		{
			throw std::runtime_error("Cannot open the USD stage.");
		}
		Converter converter;
		auto scene = converter.convert(stage);
		writeSceneFile(arguments[2], scene);
		std::printf("Wrote %s (%llu vertices).\n", utf8(arguments[2]).c_str(), static_cast<unsigned long long>(scene.mVertices.size()));
		return 0;
	}
	catch (const std::exception& error)
	{
		std::fprintf(stderr, "USD conversion failed: %s\n", error.what());
		return 1;
	}
}
