#pragma once

#include "../Common/d3dUtil.h"
#include "FrameResource.h"

class Ssao
{
public:
	Ssao(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, UINT width, UINT height);
	Ssao(const Ssao& rhs) = delete;
	Ssao& operator=(const Ssao& rhs) = delete;
	~Ssao() = delete;

	static const DXGI_FORMAT AmbientMapFormat = DXGI_FORMAT_R16_UNORM;
	static const DXGI_FORMAT NormalMapFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

	static const int MaxBlurRadius = 5;

	UINT SsaoMapWidth() const;
	UINT SsaoMapHeight() const;

	void GetOffsetVectors(DirectX::XMFLOAT4 offsets[14]);
	std::vector<float> CalcGaussWeights(float sigma);

	ID3D12Resource* NormalMap();
	ID3D12Resource* AmbientMap();

	CD3DX12_CPU_DESCRIPTOR_HANDLE NormalMapRtv() const;
	CD3DX12_GPU_DESCRIPTOR_HANDLE NormalMapSrv() const;
	CD3DX12_GPU_DESCRIPTOR_HANDLE AmbientMapSrv() const;

	void BuildDescriptors(
		ID3D12Resource* depthStencilBuffer,
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
		CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuRtv,
		UINT cbvSrvUavDescriptorSize,
		UINT RtvDescriptorSize);

	void RebuildDescriptors(ID3D12Resource* depthStencilBuffer);

	void SetPSOs(ID3D12PipelineState* ssaoPso, ID3D12PipelineState* ssaoBlurPso);

	void OnResize(UINT newWidth, UINT newHeight);

	void ComputeSsao(
		ID3D12GraphicsCommandList* cmdList,
		FrameResource* currFrame,
		int blurCount);

private:
	void BlurAmbientMap(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrame, int blurCount);
	void BlurAmbientMap(ID3D12GraphicsCommandList* cmdList, bool horzBlur);

	void BuildResources();
	void BuildRandomVectorTexture(ID3D12GraphicsCommandList* cmdList);

	void BuildOffsetVectors();

private:
	ID3D12Device* md3dDevice{ nullptr };

	Microsoft::WRL::ComPtr<ID3D12RootSignature> mSsaoRootSig{ nullptr };

	ID3D12PipelineState* mSsaoPso{ nullptr };
	ID3D12PipelineState* mBlurPso{ nullptr };

	Microsoft::WRL::ComPtr<ID3D12Resource> mRandomVectorMap{ nullptr };
	Microsoft::WRL::ComPtr<ID3D12Resource> mRandomVectorMapUploadBuffer{ nullptr };
	Microsoft::WRL::ComPtr<ID3D12Resource> mNormalMap{ nullptr };
	Microsoft::WRL::ComPtr<ID3D12Resource> mAmbientMap0;
	Microsoft::WRL::ComPtr<ID3D12Resource> mAmbientMap1;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhNormalMapCpuSrv{};
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhNormalMapGpuSrv{};
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhNormalMapCpuRtv{};

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhDepthMapCpuSrv{};
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhDepthMapGpuSrv{};

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhRandomVectorMapCpuSrv{};
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhRandomVectorMapGpuSrv{};

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhAmbientMap0CpuSrv{};
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhAmbientMap0GpuSrv{};
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhAmbientMap0CpuRtv{};

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhAmbientMap1CpuSrv{};
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhAmbientMap1GpuSrv{};
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhAmbientMap1CpuRtv{};

	UINT mRenderTargetWidth{ 0 };
	UINT mRenderTargetHeight{ 0 };

	DirectX::XMFLOAT4 mOffsets[14]{};

	D3D12_VIEWPORT mViewport{};
	D3D12_RECT mScissorRect{};
};