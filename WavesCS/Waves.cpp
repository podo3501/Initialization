//***************************************************************************************
// Waves.cpp by Frank Luna (C) 2011 All Rights Reserved.
//***************************************************************************************

#include "Waves.h"
#include <ppl.h>
#include <algorithm>
#include <vector>
#include <cassert>
#include <d3d12.h>
#include "../Common/d3dx12.h"
#include "../Common/d3dUtil.h"
#include "../Common/Util.h"
#include "../Common/GameTimer.h"
#include "DirectXMath.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

static const bool gGPUCS = false;

Waves::Waves(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,
	int m, int n, float dx, float dt, float speed, float damping)
	: mDevice(device), mCmdList(cmdList)
{
    mNumRows = m;
    mNumCols = n;

    mVertexCount = m*n;
    mTriangleCount = (m - 1)*(n - 1) * 2;

    mTimeStep = dt;
    mSpatialStep = dx;

    float d = damping*dt + 2.0f;
    float e = (speed*speed)*(dt*dt) / (dx*dx);
    mK1 = (damping*dt - 2.0f) / d;
    mK2 = (4.0f - 8.0f*e) / d;
    mK3 = (2.0f*e) / d;

    mPrevSolution.resize(m*n);
    mCurrSolution.resize(m*n);
    mNormals.resize(m*n);
    mTangentX.resize(m*n);

    // Generate grid vertices in system memory.

    float halfWidth = (n - 1)*dx*0.5f;
    float halfDepth = (m - 1)*dx*0.5f;
    for(int i = 0; i < m; ++i)
    {
        float z = halfDepth - i*dx;
        for(int j = 0; j < n; ++j)
        {
            float x = -halfWidth + j*dx;

            mPrevSolution[i*n + j] = XMFLOAT3(x, 0.0f, z);
            mCurrSolution[i*n + j] = XMFLOAT3(x, 0.0f, z);
            mNormals[i*n + j] = XMFLOAT3(0.0f, 1.0f, 0.0f);
            mTangentX[i*n + j] = XMFLOAT3(1.0f, 0.0f, 0.0f);
        }
    }
}

Waves::~Waves()
{
}

void Waves::Update(float dt)
{
	if (gGPUCS)
		return;

	static float t = 0;

	// Accumulate time.
	t += dt;

	// Only update the simulation at the specified time step.
	if( t >= mTimeStep )
	{
		// Only update interior points; we use zero boundary conditions.
		concurrency::parallel_for(1, mNumRows - 1, [this](int i)
		//for(int i = 1; i < mNumRows-1; ++i)
		{
			for(int j = 1; j < mNumCols-1; ++j)
			{
				// After this update we will be discarding the old previous
				// buffer, so overwrite that buffer with the new update.
				// Note how we can do this inplace (read/write to same element) 
				// because we won't need prev_ij again and the assignment happens last.

				// Note j indexes x and i indexes z: h(x_j, z_i, t_k)
				// Moreover, our +z axis goes "down"; this is just to 
				// keep consistent with our row indices going down.

				mPrevSolution[i*mNumCols+j].y = 
					mK1*mPrevSolution[i*mNumCols+j].y +
					mK2*mCurrSolution[i*mNumCols+j].y +
					mK3*(mCurrSolution[(i+1)*mNumCols+j].y + 
					     mCurrSolution[(i-1)*mNumCols+j].y + 
					     mCurrSolution[i*mNumCols+j+1].y + 
						 mCurrSolution[i*mNumCols+j-1].y);
			}
		});

		// We just overwrote the previous buffer with the new data, so
		// this data needs to become the current solution and the old
		// current solution becomes the new previous solution.
		std::swap(mPrevSolution, mCurrSolution);

		t = 0.0f; // reset time

		//
		// Compute normals using finite difference scheme.
		//
		concurrency::parallel_for(1, mNumRows - 1, [this](int i)
		//for(int i = 1; i < mNumRows - 1; ++i)
		{
			for(int j = 1; j < mNumCols-1; ++j)
			{
				float l = mCurrSolution[i*mNumCols+j-1].y;
				float r = mCurrSolution[i*mNumCols+j+1].y;
				float t = mCurrSolution[(i-1)*mNumCols+j].y;
				float b = mCurrSolution[(i+1)*mNumCols+j].y;
				mNormals[i*mNumCols+j].x = -r+l;
				mNormals[i*mNumCols+j].y = 2.0f*mSpatialStep;
				mNormals[i*mNumCols+j].z = b-t;

				XMVECTOR n = XMVector3Normalize(XMLoadFloat3(&mNormals[i*mNumCols+j]));
				XMStoreFloat3(&mNormals[i*mNumCols+j], n);

				mTangentX[i*mNumCols+j] = XMFLOAT3(2.0f*mSpatialStep, r-l, 0.0f);
				XMVECTOR T = XMVector3Normalize(XMLoadFloat3(&mTangentX[i*mNumCols+j]));
				XMStoreFloat3(&mTangentX[i*mNumCols+j], T);
			}
		});
	}
}

void Waves::Disturb(int i, int j, float magnitude)
{
	if (gGPUCS)
		return;
	// Don't disturb boundaries.
	assert(i > 1 && i < mNumRows-2);
	assert(j > 1 && j < mNumCols-2);

	float halfMag = 0.5f*magnitude;

	// Disturb the ijth vertex height and its neighbors.
	mCurrSolution[i*mNumCols+j].y     += magnitude;
	mCurrSolution[i*mNumCols+j+1].y   += halfMag;
	mCurrSolution[i*mNumCols+j-1].y   += halfMag;
	mCurrSolution[(i+1)*mNumCols+j].y += halfMag;
	mCurrSolution[(i-1)*mNumCols+j].y += halfMag;
}

////////////////////////////////////
//Compute Shader 

UINT Waves::GetNumDescriptors()
{
	return 6;
}

void Waves::BuildComputeRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE uavTable0, uavTable1, uavTable2;
	uavTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
	uavTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);
	uavTable2.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2);

	CD3DX12_ROOT_PARAMETER rootParameter[4];
	rootParameter[0].InitAsConstants(6, 0);
	rootParameter[1].InitAsDescriptorTable(1, &uavTable0);
	rootParameter[2].InitAsDescriptorTable(1, &uavTable1);
	rootParameter[3].InitAsDescriptorTable(1, &uavTable2);

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc
	{
		_countof(rootParameter), rootParameter,
		0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
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
		IID_PPV_ARGS(&mComputeRootSig)));
}

void CreateCommittedUARes(ID3D12Device* device, UINT nRows, UINT nCols, ComPtr<ID3D12Resource>& outUA)
{
	D3D12_RESOURCE_DESC texDesc{ 0 };
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Format = DXGI_FORMAT_R32_FLOAT;
	texDesc.MipLevels = 1;
	texDesc.Alignment = 0;
	texDesc.DepthOrArraySize = 1;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	texDesc.Width = nRows;
	texDesc.Height = nCols;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

	ThrowIfFailed(device->CreateCommittedResource(
		&RvToLv(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		IID_PPV_ARGS(&outUA)));
}

void CreateCommittedUploadRes(ID3D12Device* device, UINT nRows, UINT nCols, ComPtr<ID3D12Resource>& outUpload)
{
	D3D12_RESOURCE_DESC uploadDesc{ 0 };
	uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
	uploadDesc.MipLevels = 1;
	uploadDesc.Alignment = 0;
	uploadDesc.DepthOrArraySize = 1;
	uploadDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	uploadDesc.Width = nRows * nCols * sizeof(float);
	uploadDesc.Height = 1;
	uploadDesc.SampleDesc.Count = 1;
	uploadDesc.SampleDesc.Quality = 0;
	uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	//CD3DX12_RESOURCE_DESC
	ThrowIfFailed(device->CreateCommittedResource(
		&RvToLv(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
		D3D12_HEAP_FLAG_NONE,
		//&uploadDesc,
		&RvToLv(CD3DX12_RESOURCE_DESC::Buffer(nRows * nCols * sizeof(float))),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&outUpload)));
}

void InitialUnorderedAccess(ID3D12GraphicsCommandList* cmdList, UINT nRows, UINT nCols, ComPtr<ID3D12Resource>& dest, ComPtr<ID3D12Resource>& src)
{
	std::vector<float> zero(nRows * nCols, 0.0f);
	
	D3D12_SUBRESOURCE_DATA subResourceData = {};
	subResourceData.pData = zero.data();
	subResourceData.RowPitch = static_cast<LONG_PTR>(nRows * sizeof(float));
	subResourceData.SlicePitch = subResourceData.RowPitch * nCols;

	CD3DX12_RESOURCE_BARRIER barrierDest(
		CD3DX12_RESOURCE_BARRIER::Transition(dest.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST));
	cmdList->ResourceBarrier(1, &barrierDest);

	UpdateSubresources<1>(cmdList, dest.Get(), src.Get(), 0, 0, 1, &subResourceData);

	CD3DX12_RESOURCE_BARRIER barrierRead(
		CD3DX12_RESOURCE_BARRIER::Transition(dest.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
	cmdList->ResourceBarrier(1, &barrierRead);
}

void Waves::BuildWavesCSResource()
{	
	CreateCommittedUARes(mDevice, mNumRows, mNumCols, mPrevSolInput);
	CreateCommittedUARes(mDevice, mNumRows, mNumCols, mCurrSolInput);
	CreateCommittedUARes(mDevice, mNumRows, mNumCols, mOutput);

	CreateCommittedUploadRes(mDevice, mNumRows, mNumCols, mUploadPrevSol);

	InitialUnorderedAccess(mCmdList, mNumRows, mNumCols, mPrevSolInput, mUploadPrevSol);
	InitialUnorderedAccess(mCmdList, mNumRows, mNumCols, mCurrSolInput, mUploadPrevSol);
	InitialUnorderedAccess(mCmdList, mNumRows, mNumCols, mOutput, mUploadPrevSol);

	mWavesCS = d3dUtil::CompileShader(L"Shaders/WavesCS.hlsl", nullptr, "main", "cs_5_0");
	mDisturbCS = d3dUtil::CompileShader(L"Shaders/WavesCS.hlsl", nullptr, "disturbCS", "cs_5_0");

	CD3DX12_RESOURCE_BARRIER barrierRead(
		CD3DX12_RESOURCE_BARRIER::Transition(mOutput.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ));
	mCmdList->ResourceBarrier(1, &barrierRead);
}

void Waves::BuildDescriptorHeaps(
	D3D12_CPU_DESCRIPTOR_HANDLE hCpu, 
	D3D12_GPU_DESCRIPTOR_HANDLE hGpu,
	UINT descSize)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;

	mDevice->CreateShaderResourceView(mPrevSolInput.Get(), &srvDesc, hCpu);
	hCpu.ptr += descSize;
	mDevice->CreateShaderResourceView(mCurrSolInput.Get(), &srvDesc, hCpu);
	hCpu.ptr += descSize;
	mDevice->CreateShaderResourceView(mOutput.Get(), &srvDesc, hCpu);
	hCpu.ptr += descSize;

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{ 0 };
	uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
	uavDesc.Texture2D.MipSlice = 0;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	
	mDevice->CreateUnorderedAccessView(mPrevSolInput.Get(), nullptr, &uavDesc, hCpu);
	hCpu.ptr += descSize;
	mDevice->CreateUnorderedAccessView(mCurrSolInput.Get(), nullptr, &uavDesc, hCpu);
	hCpu.ptr += descSize;
	mDevice->CreateUnorderedAccessView(mOutput.Get(), nullptr, &uavDesc, hCpu);

	mGHPrevSolSrv = hGpu;
	hGpu.ptr += descSize;
	mGHCurrSolSrv = hGpu;
	hGpu.ptr += descSize;
	mGHOutputSrv = hGpu;
	hGpu.ptr += descSize;
	mGHPrevSolUav = hGpu;
	hGpu.ptr += descSize;
	mGHCurrSolUav = hGpu;
	hGpu.ptr += descSize;
	mGHOutputUav = hGpu;
}

void Waves::MakePSOPipelineState(ComputePSO psoType)
{
	D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc{};
	computePsoDesc.pRootSignature = mComputeRootSig.Get();
	computePsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

	switch (psoType)
	{
	case ComputePSO::WavesCS:
		computePsoDesc.CS = { reinterpret_cast<BYTE*>(mWavesCS->GetBufferPointer()),
			mWavesCS->GetBufferSize() };
		break;
	case ComputePSO::DisturbCS:
		computePsoDesc.CS = { reinterpret_cast<BYTE*>(mDisturbCS->GetBufferPointer()),
			mDisturbCS->GetBufferSize() };
		break;
	default: assert(!"wrong type");
	}

	ThrowIfFailed(mDevice->CreateComputePipelineState(&computePsoDesc,
		IID_PPV_ARGS(&mComputePSOs[psoType])));
}

void Waves::BuildPSOs()
{
	for (auto cPso : ComputePSO_ALL)
		MakePSOPipelineState(cPso);
}

D3D12_GPU_DESCRIPTOR_HANDLE Waves::DisplacementMap()
{
	return mGHOutputSrv;
}

void Waves::SetComputeRoot(int* row, int* col, float* magnitude)
{
	std::vector<float> mk = { mK1, mK2, mK3 };
	mCmdList->SetComputeRoot32BitConstants(0, (UINT)mk.size(), mk.data(), 0);
	if (row != nullptr) mCmdList->SetComputeRoot32BitConstants(0, 1, row, 3);
	if (col != nullptr) mCmdList->SetComputeRoot32BitConstants(0, 1, col, 4);
	if (magnitude != nullptr) mCmdList->SetComputeRoot32BitConstants(0, 1, magnitude, 5);

	mCmdList->SetComputeRootDescriptorTable(1, mGHPrevSolUav);
	mCmdList->SetComputeRootDescriptorTable(2, mGHCurrSolUav);
	mCmdList->SetComputeRootDescriptorTable(3, mGHOutputUav);
}

void Waves::DisturbCS(int row, int col, float magnitude)
{
	//// Don't disturb boundaries.
	assert(row > 1 && row < mNumRows - 2);
	assert(col > 1 && col < mNumCols - 2);

	mCmdList->SetPipelineState(mComputePSOs[ComputePSO::DisturbCS].Get());
	SetComputeRoot(&row, &col, &magnitude);
	
	mCmdList->Dispatch(1, 1, 1);
}

void Waves::DisturbCS(const GameTimer& gt)
{
	static float disturbT = 0.0f;
	disturbT += gt.DeltaTime();
	if (disturbT < 0.25f)
		return;

	int i = MathHelper::Rand(4, RowCount() - 5);
	int j = MathHelper::Rand(4, ColumnCount() - 5);
	float r = MathHelper::RandF(0.2f, 0.5f);

	DisturbCS(i, j, r);

	disturbT = 0.0f;
}

void Waves::WavesUpdateCS(const GameTimer& gt)
{
	static float t = 0;
	t += gt.DeltaTime();
	if (t < mTimeStep)
		return;

	t = 0.0f;

	CD3DX12_RESOURCE_BARRIER barrierUA(
		CD3DX12_RESOURCE_BARRIER::Transition(mOutput.Get(),
			D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
	mCmdList->ResourceBarrier(1, &barrierUA);

	mCmdList->SetPipelineState(mComputePSOs[ComputePSO::WavesCS].Get());
	SetComputeRoot();

	UINT dispatchX = (UINT)ceilf((float)mNumRows / 16.0f);
	UINT dispatchY = (UINT)ceilf((float)mNumCols / 16.0f);
	mCmdList->Dispatch(dispatchX, dispatchY, 1);

	CD3DX12_RESOURCE_BARRIER barrierRead(
		CD3DX12_RESOURCE_BARRIER::Transition(mOutput.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ));
	mCmdList->ResourceBarrier(1, &barrierRead);

	auto resTemp = mPrevSolInput;
	mPrevSolInput = mCurrSolInput;
	mCurrSolInput = mOutput;
	mOutput = resTemp;

	auto srvTemp = mGHPrevSolSrv;
	mGHPrevSolSrv = mGHCurrSolSrv;
	mGHCurrSolSrv = mGHOutputSrv;
	mGHOutputSrv = srvTemp;

	auto uavTemp = mGHPrevSolUav;
	mGHPrevSolUav = mGHCurrSolUav;
	mGHCurrSolUav = mGHOutputUav;
	mGHOutputUav = uavTemp;
}

void Waves::MakeWavesCS(const GameTimer& gt)
{
	if (!gGPUCS)
		return;

	mCmdList->SetComputeRootSignature(mComputeRootSig.Get());
	DisturbCS(gt);
	WavesUpdateCS(gt);
}
	
