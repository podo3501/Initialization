#include "VecAddApp.h"

#include "../Common/Util.h"
#include "../Common/UploadBuffer.h"
#include "FrameResource.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;

VecAddApp::VecAddApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

VecAddApp::~VecAddApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool VecAddApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	BuildRootSignature();
	//BuildVecAddResource();
	//BuildVecLengthResource();
	BuildPSOs();

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists2[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists2), cmdsLists2);

	FlushCommandQueue();

	ExcuteComputeShader();

	return false;
}

void VecAddApp::BuildRootSignature()
{
	const int ParamSize = 5;
	CD3DX12_ROOT_PARAMETER rootParam[ParamSize];
	rootParam[0].InitAsShaderResourceView(0);
	rootParam[1].InitAsShaderResourceView(1);
	rootParam[2].InitAsUnorderedAccessView(0);
	rootParam[3].InitAsUnorderedAccessView(1);
	rootParam[4].InitAsUnorderedAccessView(2);

	CD3DX12_ROOT_SIGNATURE_DESC rootDesc(_countof(rootParam), 
		rootParam, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;

	HRESULT hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());
	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(0,
		serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

struct Data
{
	XMFLOAT3 v1{};
	XMFLOAT2 v2{};
};

const int NumVecAdd = 32;

void VecAddApp::BuildVecAddResource()
{
	std::vector<Data> inputA(NumVecAdd);
	std::vector<Data> inputB(NumVecAdd);
	for (auto i : Range(0, NumVecAdd))
	{
		float fi = static_cast<float>(i);
		inputA[i].v1 = XMFLOAT3(fi, fi, fi);
		inputA[i].v2 = XMFLOAT2(fi, 0);

		inputB[i].v1 = XMFLOAT3(-fi, fi, 0.0f);
		inputB[i].v2 = XMFLOAT2(0, -fi);
	}
	UINT byteSize = (UINT)inputA.size() * sizeof(Data);

	mInputBufferA = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), 
		inputA.data(), byteSize, mInputUploadBufferA);

	mInputBufferB = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		inputB.data(), byteSize, mInputUploadBufferB);

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&RvToLv(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
		D3D12_HEAP_FLAG_NONE,
		&RvToLv(CD3DX12_RESOURCE_DESC::Buffer(byteSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		IID_PPV_ARGS(&mOutputBuffer)));

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&RvToLv(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK)),
		D3D12_HEAP_FLAG_NONE,
		&RvToLv(CD3DX12_RESOURCE_DESC::Buffer(byteSize)),
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&mReadBackBuffer)));
}

struct InputData
{
	XMFLOAT3 vec;
};

struct OutputData
{
	float length;
};

const int NumVecLength = 64;

void VecAddApp::BuildVecLengthResource()
{
	std::vector<XMFLOAT3> vec(NumVecLength);
	for (auto i : Range(0, NumVecLength))
	{
		float fi = static_cast<float>(i);
		vec[i] = XMFLOAT3(0.1f * fi, 0.1f * fi, 0.1f * fi);
	}
	UINT byteSize = (UINT)vec.size() * sizeof(InputData);

	mInputVLBuffer = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		vec.data(), byteSize, mInputUploadVL);

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&RvToLv(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
		D3D12_HEAP_FLAG_NONE,
		&RvToLv(CD3DX12_RESOURCE_DESC::Buffer(byteSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		IID_PPV_ARGS(&mOutputVLBuffer)));

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&RvToLv(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK)),
		D3D12_HEAP_FLAG_NONE,
		&RvToLv(CD3DX12_RESOURCE_DESC::Buffer(byteSize)),
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&mReadBackVLBuffer)));
}

void VecAddApp::BuildPSOs()
{
	mVecAddCS = d3dUtil::CompileShader(L"Shaders/VecAdd.hlsl", nullptr, "main", "cs_5_0");

	D3D12_COMPUTE_PIPELINE_STATE_DESC computePSODesc{};
	computePSODesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	computePSODesc.pRootSignature = mRootSignature.Get();
	computePSODesc.CS = { reinterpret_cast<BYTE*>(mVecAddCS->GetBufferPointer()), 
		mVecAddCS->GetBufferSize() };

	ThrowIfFailed(md3dDevice->CreateComputePipelineState(
		&computePSODesc, IID_PPV_ARGS(&mComputePSO)));

	mVecLengthCS = d3dUtil::CompileShader(L"Shaders/VecLength.hlsl", nullptr, "main", "cs_5_0");

	computePSODesc.CS = { reinterpret_cast<BYTE*>(mVecLengthCS->GetBufferPointer()), 
		mVecLengthCS->GetBufferSize() };

	ThrowIfFailed(md3dDevice->CreateComputePipelineState(
		&computePSODesc, IID_PPV_ARGS(mComputeVecLengthPSO.GetAddressOf())));
}

void VecAddApp::ExcuteComputeShader()
{
	ThrowIfFailed(mDirectCmdListAlloc->Reset());
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), mComputePSO.Get()));

	mCommandList->SetComputeRootSignature(mRootSignature.Get());

	mCommandList->SetComputeRootShaderResourceView(0, mInputBufferA->GetGPUVirtualAddress());
	mCommandList->SetComputeRootShaderResourceView(1, mInputBufferB->GetGPUVirtualAddress());
	mCommandList->SetComputeRootUnorderedAccessView(2, mOutputBuffer->GetGPUVirtualAddress());

	mCommandList->Dispatch(1, 1, 1);

	mCommandList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(mOutputBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE)));

	mCommandList->CopyResource(mReadBackBuffer.Get(), mOutputBuffer.Get());

	mCommandList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(mOutputBuffer.Get(),
		D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON)));

	//vector length
	mCommandList->SetPipelineState(mComputeVecLengthPSO.Get());
	mCommandList->SetComputeRootShaderResourceView(3, mInputVLBuffer->GetGPUVirtualAddress());
	mCommandList->SetComputeRootUnorderedAccessView(4, mOutputVLBuffer->GetGPUVirtualAddress());

	mCommandList->Dispatch(1, 1, 1);

	mCommandList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(mOutputVLBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE)));

	mCommandList->CopyResource(mReadBackVLBuffer.Get(), mOutputVLBuffer.Get());

	mCommandList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(mOutputVLBuffer.Get(),
		D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON)));

	ThrowIfFailed(mCommandList->Close());

	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	FlushCommandQueue();

	Data* mappedData = nullptr;
	ThrowIfFailed(mReadBackBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedData)));

	std::ofstream fout("results.txt");
	for (auto i : Range(0, NumVecAdd))
	{
		fout << "(" <<
			mappedData[i].v1.x << ", " <<
			mappedData[i].v1.y << ", " <<
			mappedData[i].v1.z << ", " <<
			mappedData[i].v2.x << ", " <<
			mappedData[i].v2.y << ")" << std::endl;
	}
	mReadBackBuffer->Unmap(0, nullptr);

	float* vlMappedData = nullptr;
	ThrowIfFailed(mReadBackVLBuffer->Map(0, nullptr, reinterpret_cast<void**>(&vlMappedData)));
}

void VecAddApp::OnResize()
{
	D3DApp::OnResize();

	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);
}

void VecAddApp::OnKeyboardInput(const GameTimer& gt)
{
}

void VecAddApp::UpdateCamera(const GameTimer& gt)
{
	mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);
	mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);
	mEyePos.y = mRadius * cosf(mPhi);

	XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);
}

void VecAddApp::AnimateMaterials(const GameTimer& gt)
{
}

void VecAddApp::UpdateObjectCBs(const GameTimer& gt)
{
}

void VecAddApp::UpdateMaterialCBs(const GameTimer& gt)
{
}

void VecAddApp::UpdateMainPassCB(const GameTimer& gt)
{
}

void VecAddApp::UpdateWaves(const GameTimer& gt)
{
}

void VecAddApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);
	UpdateCamera(gt);

	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialCBs(gt);
	UpdateMainPassCB(gt);
	UpdateWaves(gt);
}

void VecAddApp::Draw(const GameTimer& gt)
{

}

void VecAddApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}
void VecAddApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}
void VecAddApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		mTheta += dx;
		mPhi += dy;

		mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0)
	{
		float dx = 0.2f * static_cast<float>(x - mLastMousePos.x);
		float dy = 0.2f * static_cast<float>(y - mLastMousePos.y);

		mRadius += dx - dy;
		mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.f);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd)
{
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try
	{
		VecAddApp theApp(hInstance);
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();
	}
	catch (DxException e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}

	return 1;
}