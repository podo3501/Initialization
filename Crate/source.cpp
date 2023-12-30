#include "header.h"
#include "../Common/d3dApp.h"
#include "../Common/MathHelper.h"
#include "../Common/UploadBuffer.h"
#include "../Common/GeometryGenerator.h"
#include "FrameResource.h"
#include "../Common/Util.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;

struct RenderItem
{ 
	RenderItem() = default;

	XMFLOAT4X4 World = MathHelper::Identity4x4();
	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	int NumFrameDirty = gNumFrameResources;
	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
};

class CrateApp : public D3DApp
{
public:
	CrateApp(HINSTANCE hInstance);
	CrateApp(const CrateApp& rhs) = delete;
	CrateApp& operator=(const CrateApp& rhs) = delete;
	~CrateApp();

	virtual bool Initialize() override;

private:
	virtual void OnResize() override;
	virtual void Update(const GameTimer& gt) override;
	virtual void Draw(const GameTimer& gt) override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y) override;

	void OnKeyboardInput(const GameTimer& gt);
	void UpdateCamera(const GameTimer& gt);
	void AnimateMaterials(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);

	void LoadTextures();
	void BuildRootSignature();
	void BuildDescriptorHeaps();
	void BuildShadersAndInputLayout();
	void BuildShapeGeometry();
	void BuildMaterials();
	void BuildRenderItems();
	void BuildFrameResources();
	void BuildPSOs();

	void DrawRenderItems(
		ID3D12GraphicsCommandList* cmdList,
		const std::vector<RenderItem*>& ritems);

private:
	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	int mCurrFrameResourceIndex = 0;
	FrameResource* mCurrFrameResource = nullptr;

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;
	
	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

	ComPtr<ID3D12PipelineState> mOpaquePSO = nullptr;

	std::vector<std::unique_ptr<RenderItem>> mAllRitems;
	std::vector<RenderItem*> mOpaqueRitems;

	PassConstants mMainPassCB{};

	UINT mCbvSrvDescriptorSize = 0;

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	float mTheta = 1.3f * XM_PI;
	float mPhi = XM_PIDIV4;
	float mRadius = 2.5f;

	POINT mLastMousePos;
};

CrateApp::CrateApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

CrateApp::~CrateApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool CrateApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	LoadTextures();
	BuildRootSignature();
	BuildDescriptorHeaps();
	BuildShadersAndInputLayout();
	BuildShapeGeometry();
	BuildMaterials();
	BuildRenderItems();
	BuildFrameResources();
	BuildPSOs();

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	FlushCommandQueue();

	return true;
}

void CrateApp::LoadTextures()
{
	auto woodCrateTex = std::make_unique<Texture>();
	woodCrateTex->Name = "flare";
	woodCrateTex->Filename = L"../ExerciseMedia/flare.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), woodCrateTex->Filename.c_str(),
		woodCrateTex->Resource, woodCrateTex->UploadHeap));

	mTextures[woodCrateTex->Name] = std::move(woodCrateTex);

	auto flareAlpha = std::make_unique<Texture>();
	flareAlpha->Name = "flareAlpha";
	flareAlpha->Filename = L"../ExerciseMedia/flareAlpha.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), flareAlpha->Filename.c_str(),
		flareAlpha->Resource, flareAlpha->UploadHeap));

	mTextures[flareAlpha->Name] = std::move(flareAlpha);
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers()
{
	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0,
		D3D12_FILTER_MIN_MAG_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP);

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1,
		D3D12_FILTER_MIN_MAG_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2,
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP);

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3,
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4,
		D3D12_FILTER_ANISOTROPIC,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		0.0f,
		8);

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5,
		D3D12_FILTER_ANISOTROPIC,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		0.0f,
		8);

	return { pointWrap, pointClamp, linearWrap, linearClamp, anisotropicWrap, anisotropicClamp };
}

void CrateApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0); //register 0, SRV

	CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL); 
	slotRootParameter[1].InitAsConstantBufferView(0); //register 0, CBV
	slotRootParameter[2].InitAsConstantBufferView(1);
	slotRootParameter[3].InitAsConstantBufferView(2);
	//register가 같아도 SRV, CBV처럼 레지스터의 종류가 다르기 때문에 충돌이 일어나지 않는다.
	auto staticSamplers = GetStaticSamplers();
	//sampler는 root signature descriptor에 바로 설정을 한다. 그래서 slotRootParameter에는 설정부분이 없다.
	//2천개 이상 만들수 있어서 여유롭게 만들어 놔도 상관없다.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
		static_cast<UINT>(staticSamplers.size()), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;

	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	md3dDevice->CreateRootSignature(0, 
		serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf()));
}

//InitAsDescriptorTable로 만들면 descriptor heap을 만들어야 한다.
//table 즉 여러개의 descriptor를 저장하는 공간을 descriptor heap 이라고 한다.
//srv도 descriptor힙에 저장된다. 
void CrateApp::BuildDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
	srvHeapDesc.NumDescriptors = 2;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));
	//D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
	//CPU handle은 descriptor(SRV포함)를 만들때 호출하는 부분이 cpu에서 이루어지기 때문에 
	//cpu에서 gpu안에있는 힙을 가르키는 포인터가 필요하다
	//그것이 cpu handle. gpu handle은 gpu 안에서 gpu 포인터를 가르킬때 사용된다. 
	//gpu 핸들은 mCommandList에 바인딩 시킬때 즉, gpu(그래픽카드)가 사용하기 때문에 gpu handle이 필요하다.
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor{ mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart() };

	auto woodRes = mTextures["flare"]->Resource;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = woodRes->GetDesc().Format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = woodRes->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	md3dDevice->CreateShaderResourceView(woodRes.Get(), &srvDesc, hDescriptor);	

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	auto flareARes = mTextures["flareAlpha"]->Resource;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvFADesc{};
	srvFADesc.Format = flareARes->GetDesc().Format;
	srvFADesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvFADesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvFADesc.Texture2D.MostDetailedMip = 0;
	srvFADesc.Texture2D.MipLevels = flareARes->GetDesc().MipLevels;
	srvFADesc.Texture2D.ResourceMinLODClamp = 0.0f;
	md3dDevice->CreateShaderResourceView(flareARes.Get(), &srvFADesc, hDescriptor);
}


void CrateApp::BuildShadersAndInputLayout()
{
	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shader\\Default.hlsl", nullptr, "VS", "vs_5_0");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shader\\Default.hlsl", nullptr, "PS", "ps_5_0");
	
	mInputLayout =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};
}

void CrateApp::BuildShapeGeometry()
{
	GeometryGenerator gen;
	GeometryGenerator::MeshData boxMesh = gen.CreateBox(1.0f, 1.0f, 1.0f, 3);

	UINT totalVertexCount = static_cast<UINT>(boxMesh.Vertices.size());
	UINT totalIndexCount = static_cast<UINT>(boxMesh.Indices32.size());
	std::vector<Vertex> vertices(totalVertexCount);
	std::for_each(boxMesh.Vertices.begin(), boxMesh.Vertices.end(), [&vertices, idx = 0](auto& e) mutable {
		vertices[idx].Pos = e.Position;
		vertices[idx].Normal = e.Normal;
		vertices[idx].TexC = e.TexC;
		idx++; });
	std::vector<std::uint16_t> indices = boxMesh.GetIndices16();

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "boxGeo";
	geo->VertexByteStride = sizeof(Vertex);
	UINT vertexByteSize = totalVertexCount * sizeof(Vertex);
	UINT indexByteSize = totalIndexCount * sizeof(std::uint16_t);
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), 
		mCommandList.Get(), vertices.data(), vertexByteSize, geo->VertexBufferUploader);
	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), indexByteSize, geo->IndexBufferUploader);
	geo->VertexBufferByteSize = vertexByteSize;
	geo->IndexBufferByteSize = indexByteSize;
	SubmeshGeometry& subMesh = geo->DrawArgs["box"];
	subMesh.IndexCount = totalIndexCount;
	subMesh.BaseVertexLocation = 0;
	subMesh.StartIndexLocation = 0;
	
	mGeometries[geo->Name] = std::move(geo);
}

void CrateApp::BuildMaterials()
{
	auto wood = std::make_unique<Material>();
	wood->Name = "woodCrate";
	wood->MatCBIndex = 0;
	wood->DiffuseSrvHeapIndex = 0;
	wood->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f); //순백색의 빛이 재질에 따라 반사하는 양이 다르다 즉 색깔.
	wood->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f); //재질이 다른 물체의 경계에서 반사하는 정도. 일부는 반사 일부는 굴절된다.
	wood->Roughness = 0.2f;	//재질의 거친정도 

	mMaterials["woodCrate"] = std::move(wood);
}

void CrateApp::BuildRenderItems()
{
	auto boxRitem = std::make_unique<RenderItem>();
	auto& boxMesh = mGeometries["boxGeo"];
	auto& boxSubmesh = boxMesh->DrawArgs["box"];
	boxRitem->Geo = boxMesh.get();
	boxRitem->StartIndexLocation = boxSubmesh.StartIndexLocation;
	boxRitem->BaseVertexLocation = boxSubmesh.BaseVertexLocation;
	boxRitem->IndexCount = boxSubmesh.IndexCount;
	boxRitem->Mat = mMaterials["woodCrate"].get();
	boxRitem->ObjCBIndex = 0;
	boxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	
	mAllRitems.emplace_back(std::move(boxRitem));
	for (auto& e : mAllRitems)
		mOpaqueRitems.emplace_back(e.get());
}

void CrateApp::BuildFrameResources()
{
	for (auto idx : Range(0, gNumFrameResources))
	{
		mFrameResources.emplace_back(std::make_unique<FrameResource>(md3dDevice.Get(), 1,
			static_cast<UINT>(mAllRitems.size()), static_cast<UINT>(mMaterials.size())));
	}
}

void CrateApp::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueDesc{};
	
	opaqueDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
		mShaders["standardVS"]->GetBufferSize()
	};
	opaqueDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
		mShaders["opaquePS"]->GetBufferSize()
	};
	opaqueDesc.DSVFormat = mDepthStencilFormat;
	opaqueDesc.RTVFormats[0] = mBackBufferFormat;
	opaqueDesc.pRootSignature = mRootSignature.Get();
	opaqueDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaqueDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaqueDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaqueDesc.InputLayout = { mInputLayout.data(), static_cast<UINT>(mInputLayout.size()) };
	opaqueDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaqueDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaqueDesc.SampleMask = UINT_MAX;
	opaqueDesc.NumRenderTargets = 1;
	opaqueDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	md3dDevice->CreateGraphicsPipelineState(&opaqueDesc, IID_PPV_ARGS(&mOpaquePSO));
}

void CrateApp::OnResize()
{
	D3DApp::OnResize();

	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);
}

void CrateApp::OnKeyboardInput(const GameTimer& gt)
{
}

void CrateApp::UpdateCamera(const GameTimer& gt)
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

void CrateApp::AnimateMaterials(const GameTimer& gt)
{
}

void CrateApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjCB = mCurrFrameResource->ObjectCB.get();
	for (auto& ri : mAllRitems)
	{
		if (ri->NumFrameDirty <= 0)
			continue;

		XMMATRIX world = XMLoadFloat4x4(&ri->World);
		XMMATRIX texTransform = XMLoadFloat4x4(&ri->TexTransform);

		ObjectConstants objConstants;
		XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
		XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));

		currObjCB->CopyData(ri->ObjCBIndex, objConstants);

		ri->NumFrameDirty--;
	}
}

void CrateApp::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMatCB = mCurrFrameResource->MaterialCB.get();
	for (auto& m : mMaterials)
	{
		auto mat = m.second.get();
		//if (mat->NumFramesDirty <= 0)
//			continue;

		XMMATRIX transform = XMLoadFloat4x4(&mat->MatTransform);
		//transform = XMMatrixMultiply(transform, 
		//	XMMatrixScaling(3.0f, 3.0f, 0.0f) * XMMatrixTranslation(-0.5f, -0.5f, 0.0f));
		static float z = 0.0f;
		z += 1.0f * gt.DeltaTime();
		transform = XMMatrixMultiply(transform, XMMatrixTranslation(-0.5f, -0.5f, 0.0f));
		transform = XMMatrixMultiply(transform, XMMatrixRotationZ(z));
		transform = XMMatrixMultiply(transform, XMMatrixTranslation(0.5f, 0.5f, 0.0f));

		MaterialConstants matConstants;
		matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
		matConstants.FresnelR0 = mat->FresnelR0;
		matConstants.Roughness = mat->Roughness;
		XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(transform));

		currMatCB->CopyData(mat->MatCBIndex, matConstants);

		//mat->NumFramesDirty--;
	}
}

void CrateApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);
	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(nullptr, view);
	XMMATRIX invProj = XMMatrixInverse(nullptr, proj);
	XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mMainPassCB.EyePosW = mEyePos;
	mMainPassCB.NearZ = 0.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.RenderTargetSize = { (float)mClientWidth, (float)mClientHeight };
	mMainPassCB.InvRenderTargetSize = { 1.0f / (float)mClientWidth, 1.0f / (float)mClientHeight };
	mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
	mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[0].Strength = { 0.6f, 0.6f, 0.6f };
	mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
	mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	mMainPassCB.Lights[2].Strength = { 0.15f, 0.15f, 0.15f };

	auto currCB = mCurrFrameResource->PassCB.get();
	currCB->CopyData(0, mMainPassCB);
}

void CrateApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);
	UpdateCamera(gt);

	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		HANDLE eventHandle = ::CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialCBs(gt);
	UpdateMainPassCB(gt);
}

void CrateApp::DrawRenderItems(
	ID3D12GraphicsCommandList* cmdList,
	const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();

	for (auto ri : ritems)
	{
		cmdList->IASetVertexBuffers(0, 1, &RvToLv(ri->Geo->VertexBufferView()));
		cmdList->IASetIndexBuffer(&RvToLv(ri->Geo->IndexBufferView()));
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);
		D3D12_GPU_VIRTUAL_ADDRESS objAddress = 
			objectCB->GetGPUVirtualAddress() + (ri->ObjCBIndex * objCBByteSize);
		D3D12_GPU_VIRTUAL_ADDRESS matAddress =
			matCB->GetGPUVirtualAddress() + (ri->Mat->MatCBIndex * matCBByteSize);

		cmdList->SetGraphicsRootDescriptorTable(0, tex);
		//tex.Offset(1, mCbvSrvDescriptorSize);
		//cmdList->SetGraphicsRootDescriptorTable(1, tex);
		cmdList->SetGraphicsRootConstantBufferView(1, objAddress);
		cmdList->SetGraphicsRootConstantBufferView(3, matAddress);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

void CrateApp::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;
	ThrowIfFailed(cmdListAlloc->Reset()); 
	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mOpaquePSO.Get()));

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	mCommandList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(
		CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET)));

	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), 
		Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(),
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	mCommandList->OMSetRenderTargets(1, 
		&RvToLv(CurrentBackBufferView()), true, &RvToLv(DepthStencilView()));

	ID3D12DescriptorHeap* srvDescHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(srvDescHeaps), srvDescHeaps);
	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	auto currPassCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(2, currPassCB->GetGPUVirtualAddress());

	DrawRenderItems(mCommandList.Get(), mOpaqueRitems);

	mCommandList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(
		CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT)));

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	mCurrFrameResource->Fence = ++mCurrentFence;
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void CrateApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}
void CrateApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}
void CrateApp::OnMouseMove(WPARAM btnState, int x, int y)
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
		float dx = 0.05f * static_cast<float>(x - mLastMousePos.x);
		float dy = 0.05f * static_cast<float>(y - mLastMousePos.y);

		mRadius += dx - dy;

		mRadius = MathHelper::Clamp(mRadius, 1.0f, 100.0f);
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
		CrateApp theApp(hInstance);
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