#include "Screenshot.h"
#include <pix3.h>

#include <wincodec.h>

#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
void check(HRESULT result, const char* operation)
{
	if (FAILED(result))
	{
		char message[256]{};
		sprintf_s(message, "%s failed (HRESULT 0x%08X).", operation, static_cast<unsigned>(result));
		throw std::runtime_error(message);
	}
}

struct ComApartment
{
	HRESULT mResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	~ComApartment()
	{
		if (SUCCEEDED(mResult))
		{
			CoUninitialize();
		}
	}
};
}

void Screenshot::record(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, ID3D12Resource* backBuffer)
{
	PIXScopedEvent(commandList, PIX_COLOR(200, 140, 200), "Screenshot / GPU readback");
	const D3D12_RESOURCE_DESC textureDescription = backBuffer->GetDesc();
	if (textureDescription.Format != DXGI_FORMAT_R8G8B8A8_UNORM || textureDescription.SampleDesc.Count != 1)
	{
		throw std::runtime_error("Screenshot requires an RGBA8, single-sample back buffer.");
	}
	device->GetCopyableFootprints(&textureDescription, 0, 1, 0, &mFootprint, nullptr, nullptr, &mBufferSize);

	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_READBACK;
	D3D12_RESOURCE_DESC bufferDescription{};
	bufferDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufferDescription.Width = mBufferSize;
	bufferDescription.Height = 1;
	bufferDescription.DepthOrArraySize = 1;
	bufferDescription.MipLevels = 1;
	bufferDescription.SampleDesc.Count = 1;
	bufferDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	check(device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &bufferDescription,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&mReadback)), "Create screenshot readback");

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = backBuffer;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
	commandList->ResourceBarrier(1, &barrier);

	D3D12_TEXTURE_COPY_LOCATION source{};
	source.pResource = backBuffer;
	source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	D3D12_TEXTURE_COPY_LOCATION destination{};
	destination.pResource = mReadback.Get();
	destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	destination.PlacedFootprint = mFootprint;
	commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	commandList->ResourceBarrier(1, &barrier);
}

void Screenshot::savePng(const std::filesystem::path& path) const
{
	PIXScopedEvent(PIX_COLOR(200, 140, 200), "Screenshot / PNG encoding");
	if (!mReadback)
	{
		throw std::runtime_error("No screenshot frame has been recorded.");
	}
	const UINT width = mFootprint.Footprint.Width;
	const UINT height = mFootprint.Footprint.Height;
	const UINT64 pixelSize = static_cast<UINT64>(width) * height * 4;
	if (pixelSize > (std::numeric_limits<UINT>::max)())
	{
		throw std::runtime_error("Screenshot is too large for the PNG encoder.");
	}
	// Remove the D3D12 row-pitch padding, then convert RGBA to WIC PNG's native BGRA.
	std::vector<BYTE> pixels(static_cast<size_t>(pixelSize));
	const UINT stride = width * 4;
	void* mapped = nullptr;
	const D3D12_RANGE readRange{0, static_cast<SIZE_T>(mBufferSize)};
	check(mReadback->Map(0, &readRange, &mapped), "Map screenshot readback");
	const auto* source = static_cast<const BYTE*>(mapped) + mFootprint.Offset;
	for (UINT row = 0; row < height; ++row)
	{
		std::memcpy(pixels.data() + static_cast<size_t>(row) * stride,
			source + static_cast<size_t>(row) * mFootprint.Footprint.RowPitch, stride);
	}
	const D3D12_RANGE writtenRange{0, 0};
	mReadback->Unmap(0, &writtenRange);
	for (size_t offset = 0; offset < pixels.size(); offset += 4)
	{
		std::swap(pixels[offset], pixels[offset + 2]);
	}

	ComApartment apartment;
	if (apartment.mResult != RPC_E_CHANGED_MODE)
	{
		check(apartment.mResult, "Initialize COM for PNG encoding");
	}
	Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
	check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&factory)), "Create WIC factory");
	if (!path.parent_path().empty())
	{
		std::filesystem::create_directories(path.parent_path());
	}
	Microsoft::WRL::ComPtr<IWICStream> stream;
	check(factory->CreateStream(&stream), "Create PNG stream");
	check(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE), "Open screenshot file");
	Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
	check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), "Create PNG encoder");
	check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "Initialize PNG encoder");
	Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
	check(encoder->CreateNewFrame(&frame, nullptr), "Create PNG frame");
	check(frame->Initialize(nullptr), "Initialize PNG frame");
	check(frame->SetSize(width, height), "Set PNG size");
	WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
	check(frame->SetPixelFormat(&format), "Set PNG pixel format");
	if (!IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA))
	{
		throw std::runtime_error("PNG encoder does not support BGRA8.");
	}
	check(frame->WritePixels(height, stride, static_cast<UINT>(pixels.size()), pixels.data()), "Write PNG pixels");
	check(frame->Commit(), "Commit PNG frame");
	check(encoder->Commit(), "Commit PNG file");
}
