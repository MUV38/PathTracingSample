#include "DxrCube.h"
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
	std::array<float, 3> mAlbedo;
	float mPadding3 = 0.0f;
};
static_assert(sizeof(SceneConstants) == 24 * sizeof(float));
}

void DxrCube::initialize(Renderer& renderer)
{
	check(renderer.device()->QueryInterface(IID_PPV_ARGS(&mDevice)), "Query DXR device");
	buildAccelerationStructures(renderer);
	createPipeline();
	createShaderTable();
	D3D12_DESCRIPTOR_HEAP_DESC heap{};
	heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heap.NumDescriptors = 1;
	heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	check(mDevice->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&mSrvUavHeap)), "Create DXR UAV heap");
	resizeOutput(renderer.width(), renderer.height());
}

void DxrCube::buildAccelerationStructures(Renderer& renderer)
{
	constexpr float vertices[][3] = {
		{-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}, {1, -1, -1},
		{-1, -1, 1}, {-1, 1, 1}, {1, 1, 1}, {1, -1, 1}
	};
	constexpr UINT indices[] = {
		0, 1, 2, 0, 2, 3, // -Z
		4, 7, 6, 4, 6, 5, // +Z
		0, 4, 5, 0, 5, 1, // -X
		3, 2, 6, 3, 6, 7, // +X
		0, 3, 7, 0, 7, 4, // -Y
		1, 5, 6, 1, 6, 2  // +Y
	};
	mVertices = uploadBuffer(mDevice.Get(), vertices, sizeof(vertices));
	mIndices = uploadBuffer(mDevice.Get(), indices, sizeof(indices));
	D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
	geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
	geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
	geometry.Triangles.VertexBuffer.StartAddress = mVertices->GetGPUVirtualAddress();
	geometry.Triangles.VertexBuffer.StrideInBytes = sizeof(vertices[0]);
	geometry.Triangles.VertexCount = static_cast<UINT>(std::size(vertices));
	geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
	geometry.Triangles.IndexBuffer = mIndices->GetGPUVirtualAddress();
	geometry.Triangles.IndexCount = static_cast<UINT>(std::size(indices));
	geometry.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blasInputs{};
	blasInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	blasInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	blasInputs.NumDescs = 1;
	blasInputs.pGeometryDescs = &geometry;
	blasInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blasInfo{};
	mDevice->GetRaytracingAccelerationStructurePrebuildInfo(&blasInputs, &blasInfo);
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlasInputs{};
	tlasInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	tlasInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	tlasInputs.NumDescs = 1;
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
	D3D12_RAYTRACING_INSTANCE_DESC instance{};
	instance.Transform[0][0] = 1.0f;
	instance.Transform[1][1] = 1.0f;
	instance.Transform[2][2] = 1.0f;
	instance.InstanceMask = 0xff;
	instance.AccelerationStructure = mBlas->GetGPUVirtualAddress();
	auto instances = uploadBuffer(mDevice.Get(), &instance, sizeof(instance));
	tlasInputs.InstanceDescs = instances->GetGPUVirtualAddress();

	ComPtr<ID3D12CommandAllocator> allocator;
	ComPtr<ID3D12GraphicsCommandList4> commands;
	check(mDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "Create AS allocator");
	check(mDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
		IID_PPV_ARGS(&commands)), "Create AS command list");
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
	build.Inputs = blasInputs;
	build.DestAccelerationStructureData = mBlas->GetGPUVirtualAddress();
	build.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
	commands->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	barrier.UAV.pResource = nullptr; // Order BLAS writes and reuse of the scratch buffer before the TLAS build.
	commands->ResourceBarrier(1, &barrier);
	build.Inputs = tlasInputs;
	build.DestAccelerationStructureData = mTlas->GetGPUVirtualAddress();
	commands->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
	barrier.UAV.pResource = mTlas.Get();
	commands->ResourceBarrier(1, &barrier);
	check(commands->Close(), "Close AS command list");
	ID3D12CommandList* lists[] = {commands.Get()};
	renderer.commandQueue()->ExecuteCommandLists(1, lists);
	renderer.waitForGpu();
}

void DxrCube::createPipeline()
{
	D3D12_DESCRIPTOR_RANGE outputRange{};
	outputRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	outputRange.NumDescriptors = 1;
	D3D12_ROOT_PARAMETER parameters[3]{};
	parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	parameters[0].DescriptorTable.NumDescriptorRanges = 1;
	parameters[0].DescriptorTable.pDescriptorRanges = &outputRange;
	parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	parameters[1].Descriptor.ShaderRegister = 0;
	parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	parameters[2].Constants.Num32BitValues = sizeof(SceneConstants) / sizeof(UINT);
	parameters[2].Constants.ShaderRegister = 0;
	D3D12_ROOT_SIGNATURE_DESC rootDescription{};
	rootDescription.NumParameters = static_cast<UINT>(std::size(parameters));
	rootDescription.pParameters = parameters;
	ComPtr<ID3DBlob> rootBlob;
	ComPtr<ID3DBlob> errors;
	check(D3D12SerializeRootSignature(&rootDescription, D3D_ROOT_SIGNATURE_VERSION_1, &rootBlob, &errors), "Serialize DXR root signature");
	check(mDevice->CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(),
		IID_PPV_ARGS(&mRootSignature)), "Create DXR root signature");

	std::ifstream file(executableDirectory() / L"Cube.dxil", std::ios::binary | std::ios::ate);
	if (!file || file.tellg() <= 0)
	{
		throw std::runtime_error("Cannot read Cube.dxil. Build DxrCube including its HLSL custom build step.");
	}
	std::vector<char> shader(static_cast<size_t>(file.tellg()));
	file.seekg(0);
	if (!file.read(shader.data(), static_cast<std::streamsize>(shader.size())))
	{
		throw std::runtime_error("Reading Cube.dxil failed.");
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

void DxrCube::createShaderTable()
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

void DxrCube::resizeOutput(UINT width, UINT height)
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

void DxrCube::update(Renderer& renderer, float deltaTime)
{
	const ImGuiIO& io = ImGui::GetIO();
	if (io.AppFocusLost)
	{
		mCameraDrag = CameraMode::None;
		mRestoreCursor = false;
		return;
	}
	const ImGuiMouseButton dragButton = mCameraDrag == CameraMode::Orbit ? ImGuiMouseButton_Left
		: mCameraDrag == CameraMode::Pan ? ImGuiMouseButton_Middle : ImGuiMouseButton_Right;
	if (mCameraDrag != CameraMode::None && !ImGui::IsMouseDown(dragButton))
	{
		mCameraDrag = CameraMode::None;
		if (mRestoreCursor)
		{
			SetCursorPos(mDragCursorPosition.x, mDragCursorPosition.y);
			mRestoreCursor = false;
		}
	}
	bool startedDrag = false;
	if (mCameraDrag == CameraMode::None && !io.WantCaptureMouse)
	{
		if (io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			mCameraDrag = CameraMode::Orbit;
		}
		else if (io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
		{
			mCameraDrag = CameraMode::Pan;
		}
		else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
		{
			mCameraDrag = io.KeyAlt ? CameraMode::Dolly : CameraMode::FreeLook;
		}
		if (mCameraDrag != CameraMode::None)
		{
			startedDrag = true;
			ImGui::SetWindowFocus(nullptr);
			mRestoreCursor = GetCursorPos(&mDragCursorPosition) != FALSE;
		}
	}
	CameraInput input;
	input.mMode = mCameraDrag;
	input.mViewportHeight = static_cast<float>(renderer.height());
	if (mCameraDrag != CameraMode::None)
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_None);
		if (!startedDrag)
		{
			const auto& motion = renderer.mouseMotion();
			input.mMouseDeltaX = motion.mUseRawMouse ? motion.mDeltaX : io.MouseDelta.x;
			input.mMouseDeltaY = motion.mUseRawMouse ? motion.mDeltaY : io.MouseDelta.y;
		}
	}
	if (mCameraDrag == CameraMode::FreeLook && !io.WantTextInput && !ImGui::IsAnyItemActive())
	{
		input.mMoveForward = (ImGui::IsKeyDown(ImGuiKey_W) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_S) ? 1.0f : 0.0f);
		input.mMoveRight = (ImGui::IsKeyDown(ImGuiKey_D) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_A) ? 1.0f : 0.0f);
		input.mMoveUp = (ImGui::IsKeyDown(ImGuiKey_E) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_Q) ? 1.0f : 0.0f);
		input.mFast = io.KeyShift;
	}
	if (!io.WantCaptureMouse || mCameraDrag != CameraMode::None)
	{
		input.mSpeedWheel = io.KeyAlt ? 0.0f : io.MouseWheel;
		input.mDollyWheel = io.KeyAlt ? io.MouseWheel : 0.0f;
	}
	if (!io.WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_F, false))
	{
		mCamera.focus({0, 0, 0});
	}
	mCamera.update(input, deltaTime);
}

void DxrCube::drawGui()
{
	mViewportLeft = std::clamp(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() + 16.0f,
		0.0f, (std::max)(0.0f, ImGui::GetIO().DisplaySize.x - 64.0f));
	ImGui::TextUnformatted("DXR cube: 12 triangles / 1 BLAS / 1 TLAS");
	ImGui::TextDisabled("RMB+WASD / Wheel: speed / Alt+drag");
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("RMB: free look + WASD, Q/E: down/up, Shift: faster\nWheel: movement speed\nAlt+LMB: orbit, Alt+MMB: pan, Alt+RMB or Alt+wheel: dolly\nF: focus origin. Start mouse operations outside this panel.");
	}
	float yaw = mCamera.yaw();
	float pitch = mCamera.pitch();
	float distance = mCamera.distance();
	bool orbitChanged = ImGui::SliderAngle("Camera azimuth", &yaw, -180.0f, 180.0f, "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
	orbitChanged |= ImGui::SliderAngle("Camera elevation", &pitch, -89.0f, 89.0f, "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
	orbitChanged |= ImGui::SliderFloat("Focus distance", &distance, 0.1f, 100.0f, "%.2f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
	if (orbitChanged)
	{
		mCamera.setOrbit(yaw, pitch, distance);
	}
	float speed = mCamera.moveSpeed();
	if (ImGui::SliderFloat("Move speed", &speed, 0.05f, 100.0f, "%.2f units/s", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp))
	{
		mCamera.setMoveSpeed(speed);
	}
	ImGui::ColorEdit3("Cube color", mAlbedo.data());
	if (ImGui::Button("Reset camera"))
	{
		mCamera.reset();
	}
	ImGui::SameLine();
	if (ImGui::Button("Focus origin (F)"))
	{
		mCamera.focus({0, 0, 0});
	}
	if (ImGui::CollapsingHeader("Camera details"))
	{
		const auto position = mCamera.position();
		const auto pivot = mCamera.pivot();
		ImGui::Text("Position: %.2f, %.2f, %.2f", position.x, position.y, position.z);
		ImGui::Text("Pivot: %.2f, %.2f, %.2f", pivot.x, pivot.y, pivot.z);
	}
}

void DxrCube::render(Renderer& renderer, ID3D12GraphicsCommandList* commandList, ID3D12Resource* backBuffer)
{
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
	constants.mAlbedo = mAlbedo;
	ComPtr<ID3D12GraphicsCommandList4> commands;
	check(commandList->QueryInterface(IID_PPV_ARGS(&commands)), "Query DXR command list");
	ID3D12DescriptorHeap* heaps[] = {mSrvUavHeap.Get()};
	commands->SetDescriptorHeaps(1, heaps);
	commands->SetComputeRootSignature(mRootSignature.Get());
	commands->SetComputeRootDescriptorTable(0, mSrvUavHeap->GetGPUDescriptorHandleForHeapStart());
	commands->SetComputeRootShaderResourceView(1, mTlas->GetGPUVirtualAddress());
	commands->SetComputeRoot32BitConstants(2, sizeof(constants) / sizeof(UINT), &constants, 0);
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
