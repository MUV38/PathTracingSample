#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <filesystem>

// One readback owns the copied frame until the caller has waited for the GPU.
class Screenshot final
{
public:
	void record(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, ID3D12Resource* backBuffer);
	void savePng(const std::filesystem::path& path) const;

private:
	Microsoft::WRL::ComPtr<ID3D12Resource> mReadback;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT mFootprint{};
	UINT64 mBufferSize = 0;
};
