#include "MeshRaytracer.h"
#include "Renderer.h"
#include "Sample.h"

#include <DirectXMath.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace
{
using Microsoft::WRL::ComPtr;

void check(HRESULT result, const char* operation)
{
	if (FAILED(result))
	{
		char message[256]{};
		sprintf_s(message, "%s failed (HRESULT 0x%08X).", operation, static_cast<unsigned>(result));
		throw std::runtime_error(message);
	}
}

ComPtr<ID3D12Resource> createBuffer(ID3D12Device* device, UINT64 size, D3D12_HEAP_TYPE heapType,
	D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state)
{
	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = heapType;
	D3D12_RESOURCE_DESC description{};
	description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	description.Width = size;
	description.Height = 1;
	description.DepthOrArraySize = 1;
	description.MipLevels = 1;
	description.SampleDesc.Count = 1;
	description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	description.Flags = flags;
	ComPtr<ID3D12Resource> buffer;
	check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
		state, nullptr, IID_PPV_ARGS(&buffer)), "Create DXR buffer");
	return buffer;
}

ComPtr<ID3D12Resource> uploadBuffer(ID3D12Device* device, const void* data, size_t size)
{
	auto buffer = createBuffer(device, size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
	void* mapped = nullptr;
	const D3D12_RANGE readRange{0, 0};
	check(buffer->Map(0, &readRange, &mapped), "Map DXR upload");
	std::memcpy(mapped, data, size);
	buffer->Unmap(0, nullptr);
	return buffer;
}

struct SceneConstants
{
	DirectX::XMFLOAT3 mPosition;
	float mTanHalfFov;
	DirectX::XMFLOAT3 mRight;
	float mAspect;
	DirectX::XMFLOAT3 mUp;
	float mViewportLeft = 0.0f;
	DirectX::XMFLOAT3 mForward;
	float mMeshletColors = 0.0f;
	std::array<float, 3> mBackground;
	float mExposure = 1.0f;
	std::array<float, 3> mLightDirection;
	float mShadows = 1.0f;
	DirectX::XMFLOAT3 mLightPosition;
	float mPointLight = 0.0f;
	std::array<float, 3> mLightColor;
	float mLightStrength = 4.0f;
	float mAmbient = 0.15f;
	float mShadowBias = 0.0002f;
	std::array<float, 2> mPadding{};
};
static_assert(sizeof(SceneConstants) == 36 * sizeof(float));
}

void MeshRaytracer::initialize(Renderer& renderer)
{
	check(renderer.device()->QueryInterface(IID_PPV_ARGS(&mDevice)), "Query DXR device");
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
	check(mDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options)), "Query DXR tier");
	if (options.RaytracingTier < D3D12_RAYTRACING_TIER_1_1)
	{
		throw std::runtime_error("The meshlet viewer requires DXR Tier 1.1 for GeometryIndex().");
	}
	createPipeline();
	createShaderTable();
	D3D12_DESCRIPTOR_HEAP_DESC heap{};
	heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heap.NumDescriptors = 1;
	heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	check(mDevice->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&mSrvUavHeap)), "Create scene UAV heap");
	resizeOutput(renderer.width(), renderer.height());
}

void MeshRaytracer::load(Renderer& renderer, const SceneData& scene)
{
	// Keep the old scene alive until the replacement has finished building.
	const UINT64 byteSize = scene.mVertices.size() * sizeof(SceneVertex);
	auto staging = uploadBuffer(mDevice.Get(), scene.mVertices.data(), static_cast<size_t>(byteSize));
	auto vertices = createBuffer(mDevice.Get(), byteSize, D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
	const auto triangleIndices = meshletTriangleIndices(scene);
	auto indexStaging = uploadBuffer(mDevice.Get(), triangleIndices.data(), triangleIndices.size() * sizeof(uint32_t));
	auto indices = createBuffer(mDevice.Get(), triangleIndices.size() * sizeof(uint32_t), D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
	auto meshletStaging = uploadBuffer(mDevice.Get(), scene.mMeshlets.data(), scene.mMeshlets.size() * sizeof(SceneMeshlet));
	auto meshlets = createBuffer(mDevice.Get(), scene.mMeshlets.size() * sizeof(SceneMeshlet), D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
	auto referenceStaging = uploadBuffer(mDevice.Get(), scene.mMeshletVertices.data(), scene.mMeshletVertices.size() * sizeof(uint32_t));
	auto references = createBuffer(mDevice.Get(), scene.mMeshletVertices.size() * sizeof(uint32_t), D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
	// ByteAddressBuffer loads 32-bit words; only the final word needs padding.
	auto paddedTriangles = scene.mMeshletTriangles;
	paddedTriangles.resize((paddedTriangles.size() + 3) & ~size_t(3), 0);
	auto triangleStaging = uploadBuffer(mDevice.Get(), paddedTriangles.data(), paddedTriangles.size());
	auto triangles = createBuffer(mDevice.Get(), paddedTriangles.size(), D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
	std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries;
	geometries.reserve(scene.mMeshlets.size());
	for (const auto& meshlet : scene.mMeshlets)
	{
		D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
		geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
		geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
		geometry.Triangles.VertexBuffer = {vertices->GetGPUVirtualAddress(), sizeof(SceneVertex)};
		geometry.Triangles.VertexCount = static_cast<UINT>(scene.mVertices.size());
		geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
		geometry.Triangles.IndexBuffer = indices->GetGPUVirtualAddress() + meshlet.mTriangleOffset * sizeof(uint32_t);
		geometry.Triangles.IndexCount = meshlet.mTriangleCount * 3;
		geometry.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
		geometries.push_back(geometry);
	}
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blasInputs{};
	blasInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	blasInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	blasInputs.NumDescs = static_cast<UINT>(geometries.size());
	blasInputs.pGeometryDescs = geometries.data();
	blasInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlasInputs{};
	tlasInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	tlasInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	tlasInputs.NumDescs = 1;
	tlasInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blasInfo{}, tlasInfo{};
	mDevice->GetRaytracingAccelerationStructurePrebuildInfo(&blasInputs, &blasInfo);
	mDevice->GetRaytracingAccelerationStructurePrebuildInfo(&tlasInputs, &tlasInfo);
	if (!blasInfo.ResultDataMaxSizeInBytes || !tlasInfo.ResultDataMaxSizeInBytes)
	{
		throw std::runtime_error("DXR scene prebuild returned an invalid size.");
	}
	auto blas = createBuffer(mDevice.Get(), blasInfo.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
	auto tlas = createBuffer(mDevice.Get(), tlasInfo.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
	auto scratch = createBuffer(mDevice.Get(), (std::max)(blasInfo.ScratchDataSizeInBytes, tlasInfo.ScratchDataSizeInBytes),
		D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	D3D12_RAYTRACING_INSTANCE_DESC instance{};
	instance.Transform[0][0] = instance.Transform[1][1] = instance.Transform[2][2] = 1.0f;
	instance.InstanceMask = 0xff;
	instance.AccelerationStructure = blas->GetGPUVirtualAddress();
	auto instances = uploadBuffer(mDevice.Get(), &instance, sizeof(instance));
	tlasInputs.InstanceDescs = instances->GetGPUVirtualAddress();
	ComPtr<ID3D12CommandAllocator> allocator;
	ComPtr<ID3D12GraphicsCommandList4> commands;
	check(mDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "Create scene allocator");
	check(mDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
		IID_PPV_ARGS(&commands)), "Create scene command list");
	commands->CopyResource(vertices.Get(), staging.Get());
	commands->CopyResource(indices.Get(), indexStaging.Get());
	commands->CopyResource(meshlets.Get(), meshletStaging.Get());
	commands->CopyResource(references.Get(), referenceStaging.Get());
	commands->CopyResource(triangles.Get(), triangleStaging.Get());
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition = {vertices.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
	commands->ResourceBarrier(1, &barrier);
	barrier.Transition.pResource = indices.Get();
	commands->ResourceBarrier(1, &barrier);
	barrier.Transition.pResource = meshlets.Get();
	commands->ResourceBarrier(1, &barrier);
	barrier.Transition.pResource = references.Get();
	commands->ResourceBarrier(1, &barrier);
	barrier.Transition.pResource = triangles.Get();
	commands->ResourceBarrier(1, &barrier);
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
	build.Inputs = blasInputs;
	build.DestAccelerationStructureData = blas->GetGPUVirtualAddress();
	build.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
	commands->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
	barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	commands->ResourceBarrier(1, &barrier);
	build.Inputs = tlasInputs;
	build.DestAccelerationStructureData = tlas->GetGPUVirtualAddress();
	commands->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
	barrier.UAV.pResource = tlas.Get();
	commands->ResourceBarrier(1, &barrier);
	check(commands->Close(), "Close scene command list");
	ID3D12CommandList* lists[] = {commands.Get()};
	renderer.commandQueue()->ExecuteCommandLists(1, lists);
	renderer.waitForGpu();
	mVertices = std::move(vertices);
	mIndices = std::move(indices);
	mMeshlets = std::move(meshlets);
	mMeshletVertices = std::move(references);
	mMeshletTriangles = std::move(triangles);
	mBlas = std::move(blas);
	mTlas = std::move(tlas);
}

void MeshRaytracer::createPipeline()
{
	D3D12_DESCRIPTOR_RANGE outputRange{};
	outputRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	outputRange.NumDescriptors = 1;
	D3D12_ROOT_PARAMETER parameters[7]{};
	parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	parameters[0].DescriptorTable.NumDescriptorRanges = 1;
	parameters[0].DescriptorTable.pDescriptorRanges = &outputRange;
	parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	parameters[1].Descriptor.ShaderRegister = 0;
	parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	parameters[2].Descriptor.ShaderRegister = 1;
	parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	parameters[3].Constants.Num32BitValues = sizeof(SceneConstants) / sizeof(UINT);
	parameters[3].Constants.ShaderRegister = 0;
	parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	parameters[4].Descriptor.ShaderRegister = 2;
	parameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	parameters[5].Descriptor.ShaderRegister = 3;
	parameters[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	parameters[6].Descriptor.ShaderRegister = 4;
	D3D12_ROOT_SIGNATURE_DESC rootDescription{};
	rootDescription.NumParameters = static_cast<UINT>(std::size(parameters));
	rootDescription.pParameters = parameters;
	ComPtr<ID3DBlob> rootBlob;
	ComPtr<ID3DBlob> errors;
	check(D3D12SerializeRootSignature(&rootDescription, D3D_ROOT_SIGNATURE_VERSION_1, &rootBlob, &errors), "Serialize DXR root signature");
	check(mDevice->CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(),
		IID_PPV_ARGS(&mRootSignature)), "Create DXR root signature");

	std::ifstream file(executableDirectory() / L"Scene.dxil", std::ios::binary | std::ios::ate);
	if (!file || file.tellg() <= 0)
	{
		throw std::runtime_error("Cannot read Scene.dxil. Build UsdKitchen including its HLSL custom build step.");
	}
	std::vector<char> shader(static_cast<size_t>(file.tellg()));
	file.seekg(0);
	if (!file.read(shader.data(), static_cast<std::streamsize>(shader.size())))
	{
		throw std::runtime_error("Reading Scene.dxil failed.");
	}
	D3D12_EXPORT_DESC exports[] = {
		{L"RGSMain", nullptr, D3D12_EXPORT_FLAG_NONE},
		{L"RMSMain", nullptr, D3D12_EXPORT_FLAG_NONE},
		{L"CHSMain", nullptr, D3D12_EXPORT_FLAG_NONE}
	};
	D3D12_DXIL_LIBRARY_DESC library{};
	library.DXILLibrary = {shader.data(), shader.size()};
	library.NumExports = static_cast<UINT>(std::size(exports));
	library.pExports = exports;
	D3D12_HIT_GROUP_DESC hitGroup{};
	hitGroup.HitGroupExport = L"CubeHitGroup";
	hitGroup.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
	hitGroup.ClosestHitShaderImport = L"CHSMain";
	D3D12_RAYTRACING_SHADER_CONFIG shaderConfig{3 * sizeof(float), 2 * sizeof(float)};
	D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig{1};
	ID3D12RootSignature* globalRoot = mRootSignature.Get();
	const D3D12_STATE_SUBOBJECT subobjects[] = {
		{D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &library},
		{D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &hitGroup},
		{D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &shaderConfig},
		{D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &globalRoot},
		{D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &pipelineConfig}
	};
	D3D12_STATE_OBJECT_DESC description{};
	description.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
	description.NumSubobjects = static_cast<UINT>(std::size(subobjects));
	description.pSubobjects = subobjects;
	check(mDevice->CreateStateObject(&description, IID_PPV_ARGS(&mPipeline)), "Create DXR pipeline");
}

void MeshRaytracer::createShaderTable()
{
	ComPtr<ID3D12StateObjectProperties> properties;
	check(mPipeline.As(&properties), "Query DXR shader identifiers");
	constexpr UINT recordSize = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
	std::array<BYTE, recordSize * 3> records{};
	const wchar_t* names[] = {L"RGSMain", L"RMSMain", L"CubeHitGroup"};
	for (size_t index = 0; index < std::size(names); ++index)
	{
		const void* identifier = properties->GetShaderIdentifier(names[index]);
		if (!identifier)
		{
			throw std::runtime_error("A DXR shader identifier was not found.");
		}
		std::memcpy(records.data() + index * recordSize, identifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
	}
	mShaderTable = uploadBuffer(mDevice.Get(), records.data(), records.size());
}

void MeshRaytracer::resizeOutput(UINT width, UINT height)
{
	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC description{};
	description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	description.Width = width;
	description.Height = height;
	description.DepthOrArraySize = 1;
	description.MipLevels = 1;
	description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	description.SampleDesc.Count = 1;
	description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	mOutput.Reset();
	check(mDevice->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mOutput)), "Create DXR output texture");
	D3D12_UNORDERED_ACCESS_VIEW_DESC view{};
	view.Format = description.Format;
	view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	mDevice->CreateUnorderedAccessView(mOutput.Get(), nullptr, &view, mSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
	mWidth = width;
	mHeight = height;
}

void MeshRaytracer::render(Renderer& renderer, const Camera& camera, float viewportLeft, const Lighting& lighting, float exposure, bool meshletColors,
	ID3D12GraphicsCommandList* commandList, ID3D12Resource* backBuffer)
{
	if (mWidth != renderer.width() || mHeight != renderer.height())
	{
		// The framework waits for the previous frame before resizing or reusing resources.
		resizeOutput(renderer.width(), renderer.height());
	}
	SceneConstants constants{};
	constants.mPosition = camera.position();
	constants.mRight = camera.right();
	constants.mUp = camera.up();
	constants.mViewportLeft = viewportLeft;
	constants.mForward = camera.forward();
	constants.mTanHalfFov = camera.tanHalfFov();
	constants.mAspect = (std::max)(1.0f, static_cast<float>(mWidth) - viewportLeft) / static_cast<float>(mHeight);
	std::copy_n(renderer.clearColor().begin(), 3, constants.mBackground.begin());
	const auto forward = camera.forward();
	const float azimuth = DirectX::XMConvertToRadians(lighting.mAzimuth);
	const float elevation = DirectX::XMConvertToRadians(lighting.mElevation);
	constants.mLightDirection = lighting.mHeadlight ? std::array<float, 3>{-forward.x, -forward.y, -forward.z}
		: std::array<float, 3>{std::cos(elevation) * std::sin(azimuth), std::sin(elevation), std::cos(elevation) * std::cos(azimuth)};
	constants.mLightPosition = lighting.mPosition;
	constants.mPointLight = lighting.mPointLight ? 1.0f : 0.0f;
	constants.mShadows = lighting.mShadows ? 1.0f : 0.0f;
	constants.mLightColor = lighting.mColor;
	constants.mLightStrength = lighting.mPointLight ? lighting.mPointStrength : lighting.mDirectionalStrength;
	constants.mAmbient = lighting.mAmbient;
	constants.mShadowBias = lighting.mShadowBias;
	constants.mExposure = exposure;
	constants.mMeshletColors = meshletColors ? 1.0f : 0.0f;
	ComPtr<ID3D12GraphicsCommandList4> commands;
	check(commandList->QueryInterface(IID_PPV_ARGS(&commands)), "Query DXR command list");
	ID3D12DescriptorHeap* heaps[] = {mSrvUavHeap.Get()};
	commands->SetDescriptorHeaps(1, heaps);
	commands->SetComputeRootSignature(mRootSignature.Get());
	commands->SetComputeRootDescriptorTable(0, mSrvUavHeap->GetGPUDescriptorHandleForHeapStart());
	commands->SetComputeRootShaderResourceView(1, mTlas->GetGPUVirtualAddress());
	commands->SetComputeRootShaderResourceView(2, mVertices->GetGPUVirtualAddress());
	commands->SetComputeRootShaderResourceView(4, mMeshletVertices->GetGPUVirtualAddress());
	commands->SetComputeRootShaderResourceView(5, mMeshlets->GetGPUVirtualAddress());
	commands->SetComputeRootShaderResourceView(6, mMeshletTriangles->GetGPUVirtualAddress());
	commands->SetComputeRoot32BitConstants(3, sizeof(constants) / sizeof(UINT), &constants, 0);
	commands->SetPipelineState1(mPipeline.Get());
	constexpr UINT recordSize = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
	const D3D12_GPU_VIRTUAL_ADDRESS table = mShaderTable->GetGPUVirtualAddress();
	D3D12_DISPATCH_RAYS_DESC dispatch{};
	dispatch.RayGenerationShaderRecord = {table, recordSize};
	dispatch.MissShaderTable = {table + recordSize, recordSize, recordSize};
	dispatch.HitGroupTable = {table + recordSize * 2, recordSize, recordSize};
	dispatch.Width = mWidth;
	dispatch.Height = mHeight;
	dispatch.Depth = 1;
	commands->DispatchRays(&dispatch);

	D3D12_RESOURCE_BARRIER barriers[2]{};
	for (auto& barrier : barriers)
	{
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	}
	barriers[0].Transition = {mOutput.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE};
	barriers[1].Transition = {backBuffer, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST};
	commands->ResourceBarrier(2, barriers);
	commands->CopyResource(backBuffer, mOutput.Get());
	for (auto& barrier : barriers)
	{
		std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
	}
	commands->ResourceBarrier(2, barriers);
}
