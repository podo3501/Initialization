#include "SobelFilter.h"
#include "../Common/d3dUtil.h"
#include "../Common/Util.h"
#include <set>
#include <unordered_map>

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

struct ResourceData
{
	D3D12_CPU_DESCRIPTOR_HANDLE hCpuSrv{};
	D3D12_CPU_DESCRIPTOR_HANDLE hCpuUav{};

	D3D12_GPU_DESCRIPTOR_HANDLE hGpuSrv{};
	D3D12_GPU_DESCRIPTOR_HANDLE hGpuUav{};

	Microsoft::WRL::ComPtr<ID3D12Resource> Res = nullptr;
};

SobelFilter::SobelFilter(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,
	UINT width, UINT height, DXGI_FORMAT backBufferFormat)
	: mDevice(device), mCmdList(cmdList),
	mWidth(width), mHeight(height), mFormat(backBufferFormat)
{
	mResources["Base"] = std::make_unique<ResourceData>();
	mResources["Output"] = std::make_unique<ResourceData>();
}

SobelFilter::~SobelFilter()
{}

void SobelFilter::OnResize(UINT width, UINT height)
{
	mWidth = width;
	mHeight = height;

	BuildResources();
	BuildDescriptorsView();
}

void SobelFilter::BuildResources()
{
	D3D12_RESOURCE_DESC resDesc{ 0 };
	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resDesc.Alignment = 0;
	resDesc.Width = mWidth;
	resDesc.Height = mHeight;
	resDesc.DepthOrArraySize = 1;
	resDesc.MipLevels = 1;
	resDesc.Format = mFormat;
	resDesc.SampleDesc.Count = 1;
	resDesc.SampleDesc.Quality = 0;
	resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	
	ThrowIfFailed(mDevice->CreateCommittedResource(&RvToLv(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
		D3D12_HEAP_FLAG_NONE,
		&resDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&mResources["Base"]->Res)));

	ThrowIfFailed(mDevice->CreateCommittedResource(&RvToLv(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
		D3D12_HEAP_FLAG_NONE,
		&resDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&mResources["Output"]->Res)));
}

void SobelFilter::BuildSobelFilterRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE srvTable
	{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0 };
	CD3DX12_DESCRIPTOR_RANGE uavTable
	{ D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0 };

	CD3DX12_ROOT_PARAMETER rootParameter[2];
	rootParameter[0].InitAsDescriptorTable(1, &srvTable, D3D12_SHADER_VISIBILITY_ALL);
	rootParameter[1].InitAsDescriptorTable(1, &uavTable, D3D12_SHADER_VISIBILITY_ALL);

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc
	{
		_countof(rootParameter), rootParameter,
		0, nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
	};

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc,
		D3D_ROOT_SIGNATURE_VERSION_1_0, &serializedRootSig, &errorBlob);

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(mDevice->CreateRootSignature(0,
		serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&mRootSig)));
}

void SobelFilter::BuildCompositeRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE srvTable1
	{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0 };
	CD3DX12_DESCRIPTOR_RANGE srvTable2
	{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1 };

	CD3DX12_ROOT_PARAMETER rootParameter[2];
	rootParameter[0].InitAsDescriptorTable(1, &srvTable1, D3D12_SHADER_VISIBILITY_ALL);
	rootParameter[1].InitAsDescriptorTable(1, &srvTable2, D3D12_SHADER_VISIBILITY_ALL);

	auto samplers = d3dUtil::GetStaticSamplers();
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc
	{
		_countof(rootParameter), rootParameter,
		(UINT)samplers.size(), samplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
	};

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc,
		D3D_ROOT_SIGNATURE_VERSION_1_0, &serializedRootSig, &errorBlob);

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(mDevice->CreateRootSignature(0,
		serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&mCompositeRootSig)));
}

void SobelFilter::BuildRootSignature()
{
	BuildSobelFilterRootSignature();
	BuildCompositeRootSignature();
}

UINT SobelFilter::GetNumDescriptors()
{
	return 4;
}

void SobelFilter::BuildDescriptorsView()
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = mFormat;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Format = mFormat;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Texture2D.MipSlice = 0;

	mDevice->CreateShaderResourceView(mResources["Base"]->Res.Get(), &srvDesc, mResources["Base"]->hCpuSrv);
	mDevice->CreateUnorderedAccessView(mResources["Base"]->Res.Get(), nullptr, &uavDesc, mResources["Base"]->hCpuUav);

	mDevice->CreateShaderResourceView(mResources["Output"]->Res.Get(), &srvDesc, mResources["Output"]->hCpuSrv);
	mDevice->CreateUnorderedAccessView(mResources["Output"]->Res.Get(), nullptr, &uavDesc, mResources["Output"]->hCpuUav);
}

void SobelFilter::BuildDescriptors(
	CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDescriptor,
	CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuDescriptor,
	UINT descriptorSize)
{
	mResources["Base"]->hCpuSrv = hCpuDescriptor;
	mResources["Base"]->hCpuUav = hCpuDescriptor.Offset(1, descriptorSize);
	mResources["Output"]->hCpuSrv = hCpuDescriptor.Offset(1, descriptorSize);
	mResources["Output"]->hCpuUav = hCpuDescriptor.Offset(1, descriptorSize);

	mResources["Base"]->hGpuSrv = hGpuDescriptor;
	mResources["Base"]->hGpuUav = hGpuDescriptor.Offset(1, descriptorSize);
	mResources["Output"]->hGpuSrv = hGpuDescriptor.Offset(1, descriptorSize);
	mResources["Output"]->hGpuUav = hGpuDescriptor.Offset(1, descriptorSize);

	BuildDescriptorsView();
}

void SobelFilter::BuildShaders()
{
	mShaders["SobelFilter"] = d3dUtil::CompileShader(L"Shaders/SobelFilterCS.hlsl", nullptr, "main", "cs_5_0");
	mShaders["CompositeVS"] = d3dUtil::CompileShader(L"Shaders/Composite.hlsl", nullptr, "VS", "vs_5_0");
	mShaders["CompositePS"] = d3dUtil::CompileShader(L"Shaders/Composite.hlsl", nullptr, "PS", "ps_5_0");
}

void SobelFilter::BuildCompositePSO(D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc)
{
	psoDesc.VS = { mShaders["CompositeVS"]->GetBufferPointer(), mShaders["CompositeVS"]->GetBufferSize() };
	psoDesc.PS = { mShaders["CompositePS"]->GetBufferPointer(), mShaders["CompositePS"]->GetBufferSize() };
	psoDesc.pRootSignature = mCompositeRootSig.Get();

	psoDesc.DepthStencilState.DepthEnable = false;
	psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mCompositePSO)));
}

void SobelFilter::BuildPSOs()
{
	D3D12_COMPUTE_PIPELINE_STATE_DESC computePSODesc{};
	computePSODesc.CS = { mShaders["SobelFilter"]->GetBufferPointer(), mShaders["SobelFilter"]->GetBufferSize() };
	computePSODesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	computePSODesc.pRootSignature = mRootSig.Get();

	ThrowIfFailed(mDevice->CreateComputePipelineState(&computePSODesc, IID_PPV_ARGS(&mSobelFilterPSO)));
}

void SobelFilter::Excute(ID3D12Resource* input, 
	D3D12_CPU_DESCRIPTOR_HANDLE hCpuInput, 
	D3D12_CPU_DESCRIPTOR_HANDLE hDepthStencilView)
{
	mCmdList->SetComputeRootSignature(mRootSig.Get());

	mCmdList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(input,
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE)));

	mCmdList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(mResources["Base"]->Res.Get(),
		D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_DEST)));

	mCmdList->CopyResource(mResources["Base"]->Res.Get(), input);

	mCmdList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(mResources["Base"]->Res.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ)));

	mCmdList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(mResources["Output"]->Res.Get(),
		D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)));

	mCmdList->SetPipelineState(mSobelFilterPSO.Get());
	mCmdList->SetComputeRootDescriptorTable(0, mResources["Base"]->hGpuSrv);
	mCmdList->SetComputeRootDescriptorTable(1, mResources["Output"]->hGpuUav);
	UINT numGroupX = (UINT)ceilf(mWidth / 16.0f);
	UINT numGroupY = (UINT)ceilf(mHeight / 16.0f);
	mCmdList->Dispatch(numGroupX, numGroupY, 1);

	mCmdList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(mResources["Output"]->Res.Get(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ)));

	
	mCmdList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(input,
		D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)));

	mCmdList->ClearRenderTargetView(hCpuInput, Colors::LightSteelBlue, 0, nullptr);
	mCmdList->ClearDepthStencilView(hDepthStencilView, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
		1.0f, 0, 0, nullptr);
	mCmdList->OMSetRenderTargets(1, &hCpuInput, true, &hDepthStencilView);

	mCmdList->SetGraphicsRootSignature(mCompositeRootSig.Get());
	mCmdList->SetPipelineState(mCompositePSO.Get());
	mCmdList->SetGraphicsRootDescriptorTable(0, mResources["Base"]->hGpuSrv);
	mCmdList->SetGraphicsRootDescriptorTable(1, mResources["Output"]->hGpuSrv);
	mCmdList->IASetVertexBuffers(0, 1, nullptr);
	mCmdList->IASetIndexBuffer(nullptr);
	mCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	mCmdList->DrawInstanced(6, 1, 0, 0);
}