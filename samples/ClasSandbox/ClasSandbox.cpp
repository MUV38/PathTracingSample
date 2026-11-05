#include "ClasSandbox.h"
#include <pix3.h>
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
	float mPadding1 = 0.0f;
	std::array<float, 3> mBackground;
	float mPadding2 = 0.0f;
	UINT mMeshletColors = 0;
	float mPadding3[3]{};
};
static_assert(sizeof(SceneConstants) == 24 * sizeof(float));
}

void ClasSandbox::configure(const std::vector<std::wstring>& arguments)
{
	bool enablePreview = true;
	for (size_t index = 0; index < arguments.size(); ++index)
	{
		if (arguments[index] == L"--no-preview")
		{
			enablePreview = false;
		}
		else if (arguments[index] == L"--vertex-colors")
		{
			mMeshletColors = false;
		}
		else if (arguments[index] == L"--model" && index + 1 < arguments.size())
		{
			mModelPath = std::filesystem::absolute(arguments[++index]);
		}
		else
		{
			throw std::runtime_error("Options: --model <file.ptscene>, --vertex-colors, --no-preview.");
		}
	}
	// Must run before the framework creates any D3D12 device.
	if (enablePreview)
	{
		const UUID features[] = {D3D12ExperimentalShaderModels, D3D12RaytracingPreview};
		mCapabilities.mEnablePreview = D3D12EnableExperimentalFeatures(
			static_cast<UINT>(std::size(features)), features, nullptr, nullptr);
	}
	else
	{
		mCapabilities.mEnablePreview = E_ABORT;
	}
}

bool ClasCapabilities::available() const
{
	return SUCCEEDED(mEnablePreview) && SUCCEEDED(mFeatureQuery) && mClustersSupported
		&& SUCCEEDED(mDeviceQuery) && SUCCEEDED(mCommandListQuery)
		&& SUCCEEDED(mShaderModelQuery) && mShaderModel610;
}

bool ClasTraceState::ready() const
{
	return mTlas && mPipeline && mShaderTable;
}

ClasContext ClasSandbox::context(Renderer& renderer)
{
	return {renderer, mScene, mGpu, mCapabilities, mRootSignature.Get(),
		mReferenceInstances, {mTlas.Get(), mPipeline.Get(), mShaderTable.Get()}};
}

void ClasSandbox::initialize(Renderer& renderer)
{
	check(renderer.device()->QueryInterface(IID_PPV_ARGS(&mDevice)), "Query DXR device");
	ComPtr<ID3D12Device16> device16;
	if (SUCCEEDED(mCapabilities.mEnablePreview) && SUCCEEDED(mDevice.As(&device16)))
	{
		D3D12_FEATURE_DATA_D3D12_OPTIONS_RAYTRACING2 options{};
		mCapabilities.mFeatureQuery = device16->CheckFeatureSupport1(
			D3D12_FEATURE_D3D12_OPTIONS_RAYTRACING2, &options, sizeof(options), D3D12_PREVIEW_SDK_VERSION);
		mCapabilities.mClustersSupported = SUCCEEDED(mCapabilities.mFeatureQuery) && options.ClustersAndPTLASSupported;
	}
	ComPtr<ID3D12DeviceRaytracing2> rtasDevice;
	mCapabilities.mDeviceQuery = mDevice.As(&rtasDevice);
	D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{D3D_SHADER_MODEL_6_10};
	mCapabilities.mShaderModelQuery = mDevice->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel));
	mCapabilities.mShaderModel610 = SUCCEEDED(mCapabilities.mShaderModelQuery) && shaderModel.HighestShaderModel >= D3D_SHADER_MODEL_6_10;

	const bool useDefaultScene = mModelPath.empty() && mOptions.mCreateDefaultScene;
	if (useDefaultScene)
	{
		mModelPath = mOptions.mDefaultSceneName;
		mScene = mOptions.mCreateDefaultScene();
	}
	else if (mModelPath.empty())
	{
		mModelPath = executableDirectory() / L"assets/PreviewRoom.ptscene";
		for (auto directory = executableDirectory(); !directory.empty();)
		{
			if (std::filesystem::is_regular_file(directory / L"PathTracingSample.slnx"))
			{
				for (const auto* name : {L"Kitchen_set.ptscene", L"Kitchen_set_instanced.ptscene"})
				{
					const auto kitchen = directory / L"assets/local" / name;
					if (std::filesystem::is_regular_file(kitchen))
					{
						mModelPath = kitchen;
						break;
					}
				}
				break;
			}
			const auto parent = directory.parent_path();
			if (parent == directory)
			{
				break;
			}
			directory = parent;
		}
	}
	if (!useDefaultScene)
	{
		mScene = readSceneFile(mModelPath);
	}
	auto minimum = mScene.mHeader.mMinimum;
	auto maximum = mScene.mHeader.mMaximum;
	mReferenceInstances.resize(useDefaultScene && !mOptions.mDefaultInstanceOffsets.empty() ? mOptions.mDefaultInstanceOffsets.size() : 1);
	for (size_t i = 0; i < mReferenceInstances.size(); ++i)
	{
		auto& instance = mReferenceInstances[i];
		const auto offset = useDefaultScene && !mOptions.mDefaultInstanceOffsets.empty()
			? mOptions.mDefaultInstanceOffsets[i] : std::array<float, 3>{};
		for (size_t axis = 0; axis < offset.size(); ++axis)
		{
			instance.Transform[axis][axis] = 1.0f;
			instance.Transform[axis][3] = offset[axis];
			minimum[axis] = (std::min)(minimum[axis], mScene.mHeader.mMinimum[axis] + offset[axis]);
			maximum[axis] = (std::max)(maximum[axis], mScene.mHeader.mMaximum[axis] + offset[axis]);
		}
		instance.InstanceID = static_cast<UINT>(i);
		instance.InstanceMask = 0xff;
	}
	mFocus = {(minimum[0] + maximum[0]) * 0.5f, (minimum[1] + maximum[1]) * 0.5f, (minimum[2] + maximum[2]) * 0.5f};
	const float x = maximum[0] - minimum[0];
	const float y = maximum[1] - minimum[1];
	const float z = maximum[2] - minimum[2];
	mRadius = (std::max)(0.01f, 0.5f * std::sqrt(x * x + y * y + z * z));
	buildAccelerationStructures(renderer);
	createPipeline();
	createShaderTable();
	mExperiment.mInitialize(context(renderer));
	D3D12_DESCRIPTOR_HEAP_DESC heap{};
	heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heap.NumDescriptors = 1;
	heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	check(mDevice->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&mSrvUavHeap)), "Create DXR UAV heap");
	resizeOutput(renderer.width(), renderer.height());
}

void ClasSandbox::buildAccelerationStructures(Renderer& renderer)
{
	PIXScopedEvent(PIX_COLOR(160, 110, 220), "CLAS sandbox / build acceleration structures");
	const auto indices = meshletTriangleIndices(mScene);
	// Stage immutable scene data into device-local buffers. Keep raw meshlet layout intact.
	std::vector<ComPtr<ID3D12Resource>> uploads;
	std::vector<D3D12_RESOURCE_BARRIER> uploadBarriers;
	ComPtr<ID3D12CommandAllocator> allocator;
	ComPtr<ID3D12GraphicsCommandList4> commands;
	check(mDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "Create AS allocator");
	check(mDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
		IID_PPV_ARGS(&commands)), "Create AS command list");
	ComPtr<ID3D12CommandListRaytracing2> rtasCommands;
	mCapabilities.mCommandListQuery = commands.As(&rtasCommands);
	auto upload = [&](const void* data, size_t size, const wchar_t* name)
	{
		auto staging = uploadBuffer(mDevice.Get(), data, size);
		auto buffer = createBuffer(mDevice.Get(), size, D3D12_HEAP_TYPE_DEFAULT,
			D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
		check(buffer->SetName(name), "Name meshlet buffer");
		commands->CopyBufferRegion(buffer.Get(), 0, staging.Get(), 0, size);
		uploads.push_back(std::move(staging));
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition = {buffer.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
			D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
		uploadBarriers.push_back(barrier);
		return buffer;
	};
	{
		PIXScopedEvent(commands.Get(), PIX_COLOR(160, 180, 110), "Upload meshlet inputs");
		mGpu.mVertices = upload(mScene.mVertices.data(), mScene.mVertices.size() * sizeof(SceneVertex), L"Scene vertices (40-byte stride)");
		mGpu.mMeshlets = upload(mScene.mMeshlets.data(), mScene.mMeshlets.size() * sizeof(SceneMeshlet), L"Meshlet descriptors (16-byte stride)");
		mGpu.mVertexReferences = upload(mScene.mMeshletVertices.data(), mScene.mMeshletVertices.size() * sizeof(uint32_t), L"Meshlet vertex references (uint32)");
		auto triangles = mScene.mMeshletTriangles;
		triangles.resize((triangles.size() + 3) & ~size_t(3), 0);
		mGpu.mTriangleBytes = upload(triangles.data(), triangles.size(), L"Meshlet triangle bytes (uint8, padded to 4 bytes)");
		mGpu.mMaterials = upload(mScene.mMaterials.data(), mScene.mMaterials.size() * sizeof(SceneMaterial), L"PBR materials (48-byte stride)");
		mIndices = upload(indices.data(), indices.size() * sizeof(uint32_t), L"Reference DXR global indices");
		commands->ResourceBarrier(static_cast<UINT>(uploadBarriers.size()), uploadBarriers.data());
	}
	// Reference only: one conventional geometry per meshlet preserves GeometryIndex().
	std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries;
	geometries.reserve(mScene.mMeshlets.size());
	for (const auto& meshlet : mScene.mMeshlets)
	{
		D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
		geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
		geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
		geometry.Triangles.VertexBuffer = {mGpu.mVertices->GetGPUVirtualAddress(), sizeof(SceneVertex)};
		geometry.Triangles.VertexCount = static_cast<UINT>(mScene.mVertices.size());
		geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
		geometry.Triangles.IndexBuffer = mIndices->GetGPUVirtualAddress() + UINT64(meshlet.mTriangleOffset) * sizeof(uint32_t);
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
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blasInfo{};
	mDevice->GetRaytracingAccelerationStructurePrebuildInfo(&blasInputs, &blasInfo);
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlasInputs{};
	tlasInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	tlasInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	tlasInputs.NumDescs = static_cast<UINT>(mReferenceInstances.size());
	tlasInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlasInfo{};
	mDevice->GetRaytracingAccelerationStructurePrebuildInfo(&tlasInputs, &tlasInfo);
	if (!blasInfo.ResultDataMaxSizeInBytes || !tlasInfo.ResultDataMaxSizeInBytes)
	{
		throw std::runtime_error("DXR acceleration structure prebuild returned an invalid size.");
	}
	mBlas = createBuffer(mDevice.Get(), blasInfo.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
	mTlas = createBuffer(mDevice.Get(), tlasInfo.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
	auto scratch = createBuffer(mDevice.Get(), (std::max)(blasInfo.ScratchDataSizeInBytes, tlasInfo.ScratchDataSizeInBytes),
		D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	for (auto& instance : mReferenceInstances)
	{
		instance.AccelerationStructure = mBlas->GetGPUVirtualAddress();
	}
	auto instances = uploadBuffer(mDevice.Get(), mReferenceInstances.data(),
		mReferenceInstances.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
	tlasInputs.InstanceDescs = instances->GetGPUVirtualAddress();

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
	build.Inputs = blasInputs;
	build.DestAccelerationStructureData = mBlas->GetGPUVirtualAddress();
	build.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
	{
		PIXScopedEvent(commands.Get(), PIX_COLOR(160, 110, 220), "Build reference BLAS");
		commands->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
	}
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	barrier.UAV.pResource = nullptr; // Order BLAS writes and reuse of the scratch buffer before the TLAS build.
	commands->ResourceBarrier(1, &barrier);
	build.Inputs = tlasInputs;
	build.DestAccelerationStructureData = mTlas->GetGPUVirtualAddress();
	{
		PIXScopedEvent(commands.Get(), PIX_COLOR(160, 110, 220), "Build reference TLAS");
		commands->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
	}
	barrier.UAV.pResource = mTlas.Get();
	commands->ResourceBarrier(1, &barrier);
	check(commands->Close(), "Close AS command list");
	ID3D12CommandList* lists[] = {commands.Get()};
	renderer.commandQueue()->ExecuteCommandLists(1, lists);
	renderer.waitForGpu();
}

void ClasSandbox::createPipeline()
{
	D3D12_DESCRIPTOR_RANGE outputRange{};
	outputRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	outputRange.NumDescriptors = 1;
	D3D12_ROOT_PARAMETER parameters[8]{};
	parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	parameters[0].DescriptorTable.NumDescriptorRanges = 1;
	parameters[0].DescriptorTable.pDescriptorRanges = &outputRange;
	parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	parameters[1].Descriptor.ShaderRegister = 0;
	parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	parameters[2].Constants.Num32BitValues = sizeof(SceneConstants) / sizeof(UINT);
	parameters[2].Constants.ShaderRegister = 0;
	for (UINT index = 3; index < std::size(parameters); ++index)
	{
		parameters[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		parameters[index].Descriptor.ShaderRegister = index - 2;
	}
	D3D12_ROOT_SIGNATURE_DESC rootDescription{};
	rootDescription.NumParameters = static_cast<UINT>(std::size(parameters));
	rootDescription.pParameters = parameters;
	ComPtr<ID3DBlob> rootBlob;
	ComPtr<ID3DBlob> errors;
	check(D3D12SerializeRootSignature(&rootDescription, D3D_ROOT_SIGNATURE_VERSION_1, &rootBlob, &errors), "Serialize DXR root signature");
	check(mDevice->CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(),
		IID_PPV_ARGS(&mRootSignature)), "Create DXR root signature");

	std::ifstream file(executableDirectory() / L"Reference.dxil", std::ios::binary | std::ios::ate);
	if (!file || file.tellg() <= 0)
	{
		throw std::runtime_error("Cannot read Reference.dxil. Build ClasSandbox including its HLSL custom build step.");
	}
	std::vector<char> shader(static_cast<size_t>(file.tellg()));
	file.seekg(0);
	if (!file.read(shader.data(), static_cast<std::streamsize>(shader.size())))
	{
		throw std::runtime_error("Reading Reference.dxil failed.");
	}
	D3D12_EXPORT_DESC exports[] = {
		{L"RGS_Main", nullptr, D3D12_EXPORT_FLAG_NONE},
		{L"RMS_Main", nullptr, D3D12_EXPORT_FLAG_NONE},
		{L"CHS_Main", nullptr, D3D12_EXPORT_FLAG_NONE}
	};
	D3D12_DXIL_LIBRARY_DESC library{};
	library.DXILLibrary = {shader.data(), shader.size()};
	library.NumExports = static_cast<UINT>(std::size(exports));
	library.pExports = exports;
	D3D12_HIT_GROUP_DESC hitGroup{};
	hitGroup.HitGroupExport = L"ReferenceHitGroup";
	hitGroup.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
	hitGroup.ClosestHitShaderImport = L"CHS_Main";
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

void ClasSandbox::createShaderTable()
{
	ComPtr<ID3D12StateObjectProperties> properties;
	check(mPipeline.As(&properties), "Query DXR shader identifiers");
	constexpr UINT recordSize = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
	std::array<BYTE, recordSize * 3> records{};
	const wchar_t* names[] = {L"RGS_Main", L"RMS_Main", L"ReferenceHitGroup"};
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

void ClasSandbox::resizeOutput(UINT width, UINT height)
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

void ClasSandbox::frameModel()
{
	mCamera.reset();
	mCamera.focus(mFocus);
	const float aspect = (std::max)(0.2f, (static_cast<float>(mWidth) - mViewportLeft) / static_cast<float>(mHeight));
	const float angle = std::atan(mCamera.tanHalfFov() * (std::min)(1.0f, aspect));
	mCamera.setOrbit(0.65f, 0.45f, mRadius / std::sin(angle) * 1.08f);
	mCamera.setMoveSpeed(std::clamp(mRadius, 0.05f, 100.0f));
}

void ClasSandbox::update(Renderer& renderer, float deltaTime)
{
	mCameraController.update(mCamera, renderer, deltaTime, mFocus);
}

void ClasSandbox::drawGui()
{
	const float previousViewportLeft = mViewportLeft;
	mViewportLeft = std::clamp(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() + 16.0f,
		0.0f, (std::max)(0.0f, ImGui::GetIO().DisplaySize.x - 64.0f));
	if (mNeedsFrame)
	{
		frameModel();
		// ImGui auto-sizing can change the panel width after its first layout.
		mNeedsFrame = previousViewportLeft != mViewportLeft;
	}
	ImGui::Text("%s / Agility SDK 1.722-preview", mOptions.mTitle);
	ImGui::TextWrapped("Rendering: %s", mUseExperiment ? "User experiment" : "Reference DXR (traditional BLAS/TLAS)");
	const auto filename = mModelPath.filename().u8string();
	ImGui::Text("Scene: %s", reinterpret_cast<const char*>(filename.c_str()));
	ImGui::Text("%zu meshlets / %zu triangles", mScene.mMeshlets.size(), mScene.mMeshletTriangles.size() / 3);
	ImGui::TextWrapped("%zu vertices / max %u vertices, %u triangles per meshlet",
		mScene.mVertices.size(), sceneMeshletMaxVertices, sceneMeshletMaxTriangles);
	ImGui::Text("Reference instances: %zu", mReferenceInstances.size());
	ImGui::Checkbox("Meshlet colors", &mMeshletColors);
	const bool canTrace = mCapabilities.available() && mExperiment.mTraceState().ready();
	ImGui::BeginDisabled(!canTrace);
	ImGui::Checkbox("Use user implementation", &mUseExperiment);
	ImGui::EndDisabled();
	ImGui::TextWrapped("Preview environment: %s", mCapabilities.available() ? "Available" : "Unavailable (see diagnostics)");
	mExperiment.mDrawGui();
	if (ImGui::CollapsingHeader("Raytracing preview diagnostics", ImGuiTreeNodeFlags_DefaultOpen))
	{
		auto status = [](const char* name, HRESULT result)
		{
			ImGui::Text("%s: 0x%08X", name, static_cast<unsigned>(result));
		};
		status("Enable experimental features", mCapabilities.mEnablePreview);
		status("Raytracing2 feature query", mCapabilities.mFeatureQuery);
		status("ID3D12DeviceRaytracing2", mCapabilities.mDeviceQuery);
		status("ID3D12CommandListRaytracing2", mCapabilities.mCommandListQuery);
		ImGui::Text("ClustersAndPTLASSupported: %s", mCapabilities.mClustersSupported ? "yes" : "no");
		status("Shader Model query", mCapabilities.mShaderModelQuery);
		ImGui::Text("Shader Model 6.10: %s", mCapabilities.mShaderModel610 ? "yes" : "no");
		ImGui::TextWrapped("Preview requires Windows Developer Mode and a supporting driver. No OS settings or drivers are changed.");
	}
	ImGui::Separator();
	ImGui::TextWrapped("RMB + WASD / Q,E: fly | Wheel: speed | Alt + LMB/MMB/RMB: orbit/pan/dolly");
	if (ImGui::Button("Frame model"))
	{
		frameModel();
	}
	float speed = mCamera.moveSpeed();
	if (ImGui::SliderFloat("Move speed", &speed, 0.05f, 100.0f, "%.2f", ImGuiSliderFlags_Logarithmic))
	{
		mCamera.setMoveSpeed(speed);
	}
}

void ClasSandbox::render(Renderer& renderer, ID3D12GraphicsCommandList* commandList, ID3D12Resource* backBuffer)
{
	PIXScopedEvent(commandList, PIX_COLOR(80, 160, 220), "%s", mOptions.mTitle);
	if (mWidth != renderer.width() || mHeight != renderer.height())
	{
		// The framework waits for the previous frame before resizing or reusing resources.
		resizeOutput(renderer.width(), renderer.height());
	}
	SceneConstants constants{};
	constants.mPosition = mCamera.position();
	constants.mRight = mCamera.right();
	constants.mUp = mCamera.up();
	constants.mViewportLeft = mViewportLeft;
	constants.mForward = mCamera.forward();
	constants.mTanHalfFov = mCamera.tanHalfFov();
	constants.mAspect = (std::max)(1.0f, static_cast<float>(mWidth) - mViewportLeft) / static_cast<float>(mHeight);
	std::copy_n(renderer.clearColor().begin(), 3, constants.mBackground.begin());
	constants.mMeshletColors = mMeshletColors ? 1u : 0u;
	ComPtr<ID3D12GraphicsCommandList4> commands;
	check(commandList->QueryInterface(IID_PPV_ARGS(&commands)), "Query DXR command list");
	if (mCapabilities.available())
	{
		PIXScopedEvent(commandList, PIX_COLOR(200, 140, 80), "User experiment preparation hook");
		mExperiment.mRecord(context(renderer), commandList);
	}
	const auto experiment = mExperiment.mTraceState();
	const bool useExperiment = mUseExperiment && mCapabilities.available() && experiment.ready();
	if (!useExperiment)
	{
		mUseExperiment = false;
	}
	ID3D12Resource* tlas = useExperiment ? experiment.mTlas : mTlas.Get();
	ID3D12StateObject* pipeline = useExperiment ? experiment.mPipeline : mPipeline.Get();
	ID3D12Resource* shaderTable = useExperiment ? experiment.mShaderTable : mShaderTable.Get();
	ID3D12DescriptorHeap* heaps[] = {mSrvUavHeap.Get()};
	commands->SetDescriptorHeaps(1, heaps);
	commands->SetComputeRootSignature(mRootSignature.Get());
	commands->SetComputeRootDescriptorTable(0, mSrvUavHeap->GetGPUDescriptorHandleForHeapStart());
	commands->SetComputeRootShaderResourceView(1, tlas->GetGPUVirtualAddress());
	commands->SetComputeRoot32BitConstants(2, sizeof(constants) / sizeof(UINT), &constants, 0);
	commands->SetComputeRootShaderResourceView(3, mGpu.mVertices->GetGPUVirtualAddress());
	commands->SetComputeRootShaderResourceView(4, mGpu.mMeshlets->GetGPUVirtualAddress());
	commands->SetComputeRootShaderResourceView(5, mGpu.mVertexReferences->GetGPUVirtualAddress());
	commands->SetComputeRootShaderResourceView(6, mGpu.mTriangleBytes->GetGPUVirtualAddress());
	commands->SetComputeRootShaderResourceView(7, mGpu.mMaterials->GetGPUVirtualAddress());
	commands->SetPipelineState1(pipeline);
	constexpr UINT recordSize = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
	const D3D12_GPU_VIRTUAL_ADDRESS table = shaderTable->GetGPUVirtualAddress();
	D3D12_DISPATCH_RAYS_DESC dispatch{};
	dispatch.RayGenerationShaderRecord = {table, recordSize};
	dispatch.MissShaderTable = {table + recordSize, recordSize, recordSize};
	dispatch.HitGroupTable = {table + recordSize * 2, recordSize, recordSize};
	dispatch.Width = mWidth;
	dispatch.Height = mHeight;
	dispatch.Depth = 1;
	{
		PIXScopedEvent(commands.Get(), PIX_COLOR(80, 160, 220), useExperiment ? "Trace user experiment" : "Trace reference meshlets");
		commands->DispatchRays(&dispatch);
	}

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
	{
		PIXScopedEvent(commands.Get(), PIX_COLOR(100, 180, 180), "Copy to back buffer");
		commands->CopyResource(backBuffer, mOutput.Get());
	}
	for (auto& barrier : barriers)
	{
		std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
	}
	commands->ResourceBarrier(2, barriers);
}

int runClasSandbox(HINSTANCE instance, int showCommand, const ClasSandboxOptions& options, const ClasExperimentHooks& experiment)
{
	ClasSandbox sandbox(options, experiment);
	Sample sample;
	sample.mName = options.mWindowTitle;
	sample.mRequireDxr = true;
	sample.mConfigure = [&sandbox](const std::vector<std::wstring>& arguments) { sandbox.configure(arguments); };
	sample.mInitialize = [&sandbox](Renderer& renderer) { sandbox.initialize(renderer); };
	sample.mUpdate = [&sandbox](Renderer& renderer, float deltaTime) { sandbox.update(renderer, deltaTime); };
	sample.mDrawGui = [&sandbox]() { sandbox.drawGui(); };
	sample.mRender = [&sandbox](Renderer& renderer, ID3D12GraphicsCommandList* commands, ID3D12Resource* backBuffer)
	{
		sandbox.render(renderer, commands, backBuffer);
	};
	return runSample(instance, showCommand, sample);
}
