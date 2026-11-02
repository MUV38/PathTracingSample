#include "SceneFile.h"
#include "../samples/UsdKitchen/EnvironmentMap.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

extern "C"
{
	__declspec(dllexport) extern const UINT D3D12SDKVersion = 722;
	__declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\";
}

namespace
{
using Microsoft::WRL::ComPtr;
constexpr UINT samplesPerThread = 2048;
constexpr UINT sampleCount = samplesPerThread * 64;
struct CaseData
{
	std::array<float,3> mBaseColor{1,1,1};
	float mMetallic = 0;
	float mRoughness = 0.5f;
	float mSpecular = 0.5f;
	float mNoV = 1;
	uint32_t mFlags = 0;
};
static_assert(sizeof(CaseData) == 32);
struct Result
{
	std::array<float,3> mMean;
	uint32_t mInvalid;
	std::array<float,3> mVariance;
	float mMaxWeight;
	std::array<float,3> mReference;
	uint32_t mReferenceInvalid;
	std::array<float,3> mReferenceVariance;
	float mPadding;
};
static_assert(sizeof(Result) == 64);
struct Test
{
	CaseData mData;
	std::string mName;
	std::optional<std::array<float,3>> mExpected;
};
void check(HRESULT result, const char* operation)
{
	if (FAILED(result))
	{
		throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(static_cast<uint32_t>(result)));
	}
}
ComPtr<ID3D12Resource> buffer(ID3D12Device* device, UINT64 size, D3D12_HEAP_TYPE type, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state)
{
	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = type;
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = size;
	desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.Flags = flags;
	ComPtr<ID3D12Resource> result;
	check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&result)), "Create buffer");
	return result;
}
std::filesystem::path executableDirectory()
{
	std::array<wchar_t,32768> path{};
	const DWORD size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
	if (!size || size >= path.size())
	{
		throw std::runtime_error("Cannot locate the test shader.");
	}
	return std::filesystem::path(path.data()).parent_path();
}
std::vector<Result> integrate(const std::vector<Test>& tests, bool warp)
{
	ComPtr<IDXGIFactory6> factory;
	check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "Create DXGI factory");
	ComPtr<ID3D12Device> device;
	ComPtr<IDXGIAdapter1> adapter;
	if (!warp)
	{
		for (UINT index = 0;; ++index)
		{
			const HRESULT result = factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
			if (result == DXGI_ERROR_NOT_FOUND)
			{
				break;
			}
			check(result, "Enumerate furnace adapter");
			DXGI_ADAPTER_DESC1 desc{};
			check(adapter->GetDesc1(&desc), "Query adapter");
			if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
			{
				break;
			}
			adapter.Reset();
		}
	}
	if (!device)
	{
		check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)), "Get WARP adapter");
		check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), "Create WARP device");
	}
	DXGI_ADAPTER_DESC1 adapterDesc{};
	check(adapter->GetDesc1(&adapterDesc), "Read adapter name");
	std::wcout << L"White furnace adapter: " << adapterDesc.Description << L'\n';
	D3D12_FEATURE_DATA_SHADER_MODEL model{D3D_SHADER_MODEL_6_0};
	check(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model)), "Check Shader Model 6.0");
	if (model.HighestShaderModel < D3D_SHADER_MODEL_6_0)
	{
		throw std::runtime_error("WhiteFurnaceTests requires Shader Model 6.0.");
	}
	D3D12_ROOT_PARAMETER parameters[4]{};
	parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	parameters[0].Descriptor.ShaderRegister = 0;
	parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
	parameters[1].Descriptor.ShaderRegister = 0;
	parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	parameters[2].Constants.Num32BitValues = 1;
	parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	parameters[3].Descriptor.ShaderRegister = 1;
	D3D12_ROOT_SIGNATURE_DESC rootDesc{};
	rootDesc.NumParameters = 4;
	rootDesc.pParameters = parameters;
	ComPtr<ID3DBlob> rootBlob, errors;
	check(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rootBlob, &errors), "Serialize furnace root signature");
	ComPtr<ID3D12RootSignature> root;
	check(device->CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(), IID_PPV_ARGS(&root)), "Create furnace root signature");
	std::ifstream file(executableDirectory() / L"WhiteFurnace.dxil", std::ios::binary | std::ios::ate);
	if (!file || file.tellg() <= 0)
	{
		throw std::runtime_error("WhiteFurnace.dxil is missing; build WhiteFurnaceTests first.");
	}
	std::vector<char> shader(static_cast<size_t>(file.tellg()));
	file.seekg(0);
	if (!file.read(shader.data(), shader.size()))
	{
		throw std::runtime_error("Reading the furnace shader failed.");
	}
	D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};
	pipelineDesc.pRootSignature = root.Get();
	pipelineDesc.CS = {shader.data(), shader.size()};
	ComPtr<ID3D12PipelineState> pipeline;
	check(device->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(&pipeline)), "Create furnace compute pipeline");
	std::vector<CaseData> cases;
	for (const auto& test : tests)
	{
		cases.push_back(test.mData);
	}
	auto input = buffer(device.Get(), cases.size() * sizeof(CaseData), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
	void* mapped = nullptr;
	const D3D12_RANGE noRead{0,0};
	check(input->Map(0, &noRead, &mapped), "Map furnace cases");
	std::memcpy(mapped, cases.data(), cases.size() * sizeof(CaseData));
	input->Unmap(0, nullptr);
	// A deliberately nonuniform proposal over a unit-white target tests MIS
	// normalization, 2D CDF inversion, rotation, and PDF agreement together.
	EnvironmentMap proposal{16, 8, std::vector<std::array<float,4>>(128, {1,1,1,1})};
	proposal.mPixels[37] = {1000,1000,1000,1};
	const auto distribution = createEnvironmentDistribution(proposal);
	const size_t distributionBytes = distribution.size() * sizeof(distribution[0]);
	auto environment = buffer(device.Get(), distributionBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
	check(environment->Map(0, &noRead, &mapped), "Map furnace environment");
	std::memcpy(mapped, distribution.data(), distributionBytes);
	environment->Unmap(0, nullptr);
	const size_t bytes = tests.size() * sizeof(Result);
	auto output = buffer(device.Get(), bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	auto readback = buffer(device.Get(), bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
	D3D12_COMMAND_QUEUE_DESC queueDesc{};
	ComPtr<ID3D12CommandQueue> queue;
	check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)), "Create furnace queue");
	ComPtr<ID3D12CommandAllocator> allocator;
	ComPtr<ID3D12GraphicsCommandList> commands;
	check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "Create furnace allocator");
	check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), pipeline.Get(), IID_PPV_ARGS(&commands)), "Create furnace commands");
	commands->SetComputeRootSignature(root.Get());
	commands->SetComputeRootShaderResourceView(0, input->GetGPUVirtualAddress());
	commands->SetComputeRootShaderResourceView(3, environment->GetGPUVirtualAddress());
	commands->SetComputeRootUnorderedAccessView(1, output->GetGPUVirtualAddress());
	commands->SetComputeRoot32BitConstant(2, samplesPerThread, 0);
	commands->Dispatch(static_cast<UINT>(tests.size()), 1, 1);
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition = {output.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE};
	commands->ResourceBarrier(1, &barrier);
	commands->CopyResource(readback.Get(), output.Get());
	check(commands->Close(), "Close furnace commands");
	ID3D12CommandList* lists[] = {commands.Get()};
	queue->ExecuteCommandLists(1, lists);
	ComPtr<ID3D12Fence> fence;
	check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "Create furnace fence");
	struct Event
	{
		HANDLE mHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		~Event()
		{
			if (mHandle)
			{
				CloseHandle(mHandle);
			}
		}
	} event;
	if (!event.mHandle)
	{
		throw std::runtime_error("Cannot create the furnace fence event.");
	}
	check(queue->Signal(fence.Get(), 1), "Signal furnace fence");
	check(fence->SetEventOnCompletion(1, event.mHandle), "Wait for furnace fence");
	if (WaitForSingleObject(event.mHandle, 30000) != WAIT_OBJECT_0)
	{
		throw std::runtime_error("White furnace GPU execution timed out.");
	}
	const D3D12_RANGE range{0,bytes};
	check(readback->Map(0, &range, &mapped), "Map furnace results");
	std::vector<Result> result(tests.size());
	std::memcpy(result.data(), mapped, bytes);
	readback->Unmap(0, &noRead);
	return result;
}
std::vector<Test> testCases()
{
	std::vector<Test> tests;
	for (float metallic : {0.0f, 0.5f, 1.0f})
	{
		for (float roughness : {0.0f, 0.02f, 0.1f, 0.3f, 0.6f, 1.0f})
		{
			for (float noV : {1.0f, 0.5f, 0.1f, 0.02f})
			{
				Test test{{{1,1,1}, metallic, roughness, 0.5f, noV, roughness == 0 || roughness >= 0.6f}, "white", {}};
				if (metallic == 1 && roughness == 0)
				{
					test.mExpected = {1,1,1};
				}
				tests.push_back(test);
			}
		}
	}
	for (float noV : {1.0f, 0.5f, 0.1f, 0.02f})
	{
		tests.push_back({{{1,1,1}, 0, 0.5f, 0, noV, 1}, "lambert-white", std::array<float,3>{1,1,1}});
		tests.push_back({{{0.2f,0.5f,0.8f}, 0, 0.5f, 0, noV, 1}, "lambert-color", std::array<float,3>{0.2f,0.5f,0.8f}});
		tests.push_back({{{0,0,0}, 0, 0.5f, 0, noV, 1}, "absorber", std::array<float,3>{0,0,0}});
		tests.push_back({{{1,1,1}, 0, 0.3f, 1, noV, 0}, "high-specular", {}});
	}
	const size_t environmentCount = tests.size();
	// Exact Lambert form factor for a centered 2x2 rectangle at distance 2.
	const float lambertFactor = static_cast<float>(4 / 3.141592653589793 * (0.5 / std::sqrt(1.25)) * std::atan(0.5 / std::sqrt(1.25)));
	for (size_t index = 0; index < environmentCount; ++index)
	{
		auto rectangle = tests[index];
		rectangle.mData.mFlags |= 4u;
		rectangle.mName = "rectangle-" + rectangle.mName;
		if (rectangle.mName == "rectangle-lambert-white" || rectangle.mName == "rectangle-lambert-color")
		{
			for (float& channel : *rectangle.mExpected)
			{
				channel *= lambertFactor;
			}
		}
		else if (rectangle.mData.mMetallic == 1 && rectangle.mData.mRoughness == 0)
		{
			const float value = rectangle.mData.mNoV == 1 ? 1.0f : 0.0f;
			rectangle.mExpected = {value,value,value};
		}
		tests.push_back(rectangle);
	}
	const size_t count = tests.size();
	for (size_t index = 0; index < count; ++index)
	{
		auto mis = tests[index];
		mis.mData.mFlags |= 2u;
		mis.mName = "mis-" + mis.mName;
		tests.push_back(mis);
	}
	return tests;
}
void writeRectangleFixtures(const std::filesystem::path& directory)
{
	SceneData scene;
	scene.mHeader.mMeshCount = 1;
	scene.mMaterials[0].mSpecular = 0;
	auto quad = [&](std::array<float,3> a, std::array<float,3> b, std::array<float,3> c, std::array<float,3> d, std::array<float,3> normal)
	{
		const auto first = static_cast<uint8_t>(scene.mVertices.size());
		for (const auto& position : {a,b,c,d})
		{
			scene.mMeshletVertices.push_back(static_cast<uint32_t>(scene.mVertices.size()));
			scene.mVertices.push_back({position, normal, {1,1,1}});
		}
		for (uint8_t index : std::array<uint8_t,6>{0,2,1,0,3,2})
		{
			scene.mMeshletTriangles.push_back(first + index);
		}
	};
	quad({-2,0,-2}, {2,0,-2}, {2,0,2}, {-2,0,2}, {0,1,0});
	scene.mMeshlets = {{0,0,4,2}};
	writeSceneFile(directory / L"rectangle-floor.ptscene", scene);
	scene.mMaterials[0].mMetallic = 1;
	scene.mMaterials[0].mRoughness = 0;
	writeSceneFile(directory / L"rectangle-mirror.ptscene", scene);
	scene.mMaterials[0] = SceneMaterial{};
	scene.mMaterials[0].mSpecular = 0;
	quad({-0.4f,1,-0.4f}, {0.4f,1,-0.4f}, {0.4f,1,0.4f}, {-0.4f,1,0.4f}, {0,1,0});
	quad({-0.4f,0,-0.4f}, {0.4f,0,-0.4f}, {0.4f,1,-0.4f}, {-0.4f,1,-0.4f}, {0,0,-1});
	quad({-0.4f,0,0.4f}, {0.4f,0,0.4f}, {0.4f,1,0.4f}, {-0.4f,1,0.4f}, {0,0,1});
	quad({-0.4f,0,-0.4f}, {-0.4f,0,0.4f}, {-0.4f,1,0.4f}, {-0.4f,1,-0.4f}, {-1,0,0});
	quad({0.4f,0,-0.4f}, {0.4f,0,0.4f}, {0.4f,1,0.4f}, {0.4f,1,-0.4f}, {1,0,0});
	scene.mMeshlets = {{0,0,static_cast<uint32_t>(scene.mVertices.size()),static_cast<uint32_t>(scene.mMeshletTriangles.size() / 3)}};
	writeSceneFile(directory / L"rectangle-shadow.ptscene", scene);
}
void writeFixtures(const std::filesystem::path& directory)
{
	SceneData plane;
	plane.mHeader.mMeshCount = 1;
	plane.mVertices = {{{-1,0,-1},{0,1,0},{1,1,1}}, {{1,0,-1},{0,1,0},{1,1,1}}, {{1,0,1},{0,1,0},{1,1,1}}, {{-1,0,1},{0,1,0},{1,1,1}}};
	plane.mMeshlets = {{0,0,4,2}};
	plane.mMeshletVertices = {0,1,2,3};
	plane.mMeshletTriangles = {0,2,1,0,3,2};
	plane.mMaterials[0].mMetallic = 1;
	plane.mMaterials[0].mRoughness = 0;
	writeSceneFile(directory / L"mirror.ptscene", plane);
	plane.mMaterials[0].mMetallic = 0;
	plane.mMaterials[0].mSpecular = 0;
	plane.mMaterials[0].mRoughness = 0.5f;
	writeSceneFile(directory / L"diffuse.ptscene", plane);
	plane.mMaterials[0].mBaseColor = {0,0,0};
	writeSceneFile(directory / L"absorber.ptscene", plane);
	std::ofstream hdr(directory / L"white.hdr", std::ios::binary);
	hdr << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 1\n";
	const std::array<uint8_t,4> pixel{128,128,128,129};
	hdr.write(reinterpret_cast<const char*>(pixel.data()), pixel.size());
	if (!hdr)
	{
		throw std::runtime_error("Cannot write the white environment fixture.");
	}
}
}

int wmain(int count, wchar_t** arguments)
{
	try
	{
		if (count < 2 || count > 3 || (count == 3 && std::wstring(arguments[2]) != L"--warp"))
		{
			throw std::runtime_error("Usage: WhiteFurnaceTests <output-directory> [--warp]");
		}
		const std::filesystem::path directory = arguments[1];
		std::filesystem::create_directories(directory);
		writeFixtures(directory);
		writeRectangleFixtures(directory);
		const auto tests = testCases();
		const auto results = integrate(tests, count == 3);
		std::ofstream csv(directory / L"furnace.csv");
		csv << "case,metallic,roughness,specular,noV,channel,reflectance,standard_error,reference,reference_error,max_weight,invalid,pass\n";
		csv << std::setprecision(9);
		size_t failures = 0;
		for (size_t index = 0; index < tests.size(); ++index)
		{
			const auto& test = tests[index];
			const auto& result = results[index];
			for (size_t axis = 0; axis < 3; ++axis)
			{
				const double mean = result.mMean[axis];
				const double error = std::sqrt(result.mVariance[axis] / sampleCount);
				const double referenceError = std::sqrt(result.mReferenceVariance[axis] / sampleCount);
				bool pass = std::isfinite(mean) && std::isfinite(error) && std::isfinite(result.mMaxWeight)
					&& result.mInvalid == 0 && result.mReferenceInvalid == 0
					&& mean >= -0.0001 && mean <= 1.002 + 6 * error;
				if (test.mExpected)
				{
					pass &= std::abs(mean - (*test.mExpected)[axis]) <= 0.0005 + ((test.mData.mFlags & 6u) ? 6 * error : 0);
				}
				if (test.mData.mFlags & 1u)
				{
					pass &= std::isfinite(result.mReference[axis]) && std::isfinite(referenceError)
						&& std::abs(mean - result.mReference[axis]) <= 0.005 + 6 * std::hypot(error, referenceError);
				}
				if (test.mData.mFlags & 2u)
				{
					// MIS cases follow the corresponding BSDF-only cases in the same order.
					const auto& baseline = results[index - tests.size() / 2];
					const double combinedError = std::sqrt((result.mVariance[axis] + baseline.mVariance[axis]) / sampleCount);
					pass &= std::abs(mean - baseline.mMean[axis]) <= 0.005 + 6 * combinedError;
				}
				csv << test.mName << ',' << test.mData.mMetallic << ',' << test.mData.mRoughness << ',' << test.mData.mSpecular << ',' << test.mData.mNoV << ','
					<< axis << ',' << mean << ',' << error << ',' << result.mReference[axis] << ',' << referenceError << ',' << result.mMaxWeight << ',' << result.mInvalid << ',' << pass << '\n';
				if (!pass)
				{
					++failures;
					std::cerr << "FAIL " << test.mName << " metal=" << test.mData.mMetallic << " roughness=" << test.mData.mRoughness
						<< " noV=" << test.mData.mNoV << " channel=" << axis << " reflectance=" << mean << " stderr=" << error << " reference=" << result.mReference[axis] << '\n';
				}
			}
		}
		csv.close();
		if (!csv || failures)
		{
			throw std::runtime_error("White furnace failed; inspect furnace.csv (" + std::to_string(failures) + " failed channels).");
		}
		std::cout << "White furnace passed: " << tests.size() << " cases, " << sampleCount << " samples/case. CSV: " << (directory / L"furnace.csv").string() << '\n';
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
