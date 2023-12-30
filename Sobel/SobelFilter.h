#pragma once

#include <vector>
#include <DirectXMath.h>
#include <map>
#include <unordered_map>
#include <array>
#include <wrl.h>
#include <d3dcommon.h>
#include <d3d12.h>
#include "../Common/d3dx12.h"
#include <string>
#include <memory>

struct ResourceData;

class SobelFilter
{
public:
    SobelFilter(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,
        UINT width, UINT height, DXGI_FORMAT backBufferFormat);
    SobelFilter(const SobelFilter& rhs) = delete;
    SobelFilter& operator=(const SobelFilter& rhs) = delete;
    ~SobelFilter();

    void OnResize(UINT width, UINT height);
    void BuildResources();
    void BuildRootSignature();
    UINT GetNumDescriptors();
    void BuildDescriptors(
        CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDescriptor,
        CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuDescriptor,
        UINT descriptorSize);
    void BuildShaders();
    void BuildPSOs();
    void BuildCompositePSO(D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc);
    void Excute(ID3D12Resource* input,
        D3D12_CPU_DESCRIPTOR_HANDLE hCpuInput,
        D3D12_CPU_DESCRIPTOR_HANDLE hDepthStencilView);

private:
    void BuildSobelFilterRootSignature();
    void BuildCompositeRootSignature();
    void BuildDescriptorsView();

private:
    ID3D12Device* mDevice = nullptr;
    ID3D12GraphicsCommandList* mCmdList = nullptr;

    std::unordered_map<std::string, std::unique_ptr<ResourceData>> mResources;
    
    UINT mWidth = 0;
    UINT mHeight = 0;
    DXGI_FORMAT mFormat{};

    Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSig = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mCompositeRootSig = nullptr;
    
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3DBlob>> mShaders;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> mSobelFilterPSO = nullptr;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mCompositePSO = nullptr;
};
