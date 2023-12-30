#pragma once

#include<d3d12.h>
#include <wrl.h>
#include <map>
#include <vector>
#include "../Common/d3dx12.h"

class BlurFilter
{
public:
	BlurFilter(ID3D12Device* device,
		UINT width, UINT height,
		DXGI_FORMAT format);
	BlurFilter(const BlurFilter& rhs) = delete;
	BlurFilter& operator=(const BlurFilter& rhs) = delete;
	~BlurFilter() = default;

	UINT GetNumDescriptors();
	ID3D12Resource* Output();
	void BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDescriptor,
		CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuDescriptor,
		UINT descriptorSize);
	void OnResize(UINT newWidth, UINT newHeight);
	void Execute(
		ID3D12GraphicsCommandList* cmdList,
		ID3D12RootSignature* rootSig,
		ID3D12PipelineState* horzBlurPSO,
		ID3D12PipelineState* vertBlurPSO,
		ID3D12Resource* input,
		int blurCount);
	
private:
	void BuildResources();
	void BuildDescriptorsView();
	std::vector<float> CalcGaussWeights(float sigma);

private:
	const int MaxBlurRadius = 5;

	ID3D12Device* md3dDevice = nullptr;
	UINT mWidth = 0;
	UINT mHeight = 0;
	DXGI_FORMAT mFormat = DXGI_FORMAT_UNKNOWN;

	Microsoft::WRL::ComPtr<ID3D12Resource> mBlurMap0 = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> mBlurMap1 = nullptr;

	D3D12_CPU_DESCRIPTOR_HANDLE mBlur0CpuSrv{}, mBlur0CpuUav{}, mBlur1CpuSrv{}, mBlur1CpuUav{};
	D3D12_GPU_DESCRIPTOR_HANDLE mBlur0GpuSrv{}, mBlur0GpuUav{}, mBlur1GpuSrv{}, mBlur1GpuUav{};
};
