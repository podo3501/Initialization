#include "source.h"
#include "../Common/d3dApp.h"
#include "../Common/MathHelper.h"
#include "../Common/UploadBuffer.h"
#include "../Common/GeometryGenerator.h"
#include "FrameResource.h"
#include "../Common/Util.h"
#include "Waves.h"

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

enum class RenderLayer : int
{
	Opaque = 0,
	Count
};

class TexWavesApp : public D3DApp
{
public:
	TexWavesApp(HINSTANCE hInstance);
	TexWavesApp(const TexWavesApp& rhs) = delete;
	TexWavesApp& operator=(const TexWavesApp& rhs) = delete;
	~TexWavesApp();

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
	void UpdateWaves(const GameTimer& gt);

	void LoadTextures();
	void BuildRootSignature();
	void BuildDescriptorHeaps();
	void BuildShadersAndInputLayout();
	void BuildLandGeometry();
	void BuildWavesGeometry();
	void BuildBoxGeometry();
	void BuildMaterials();
	void BuildRenderItems();
	void BuildFrameResources();
	void BuildPSOs();

	void DrawRenderItems(
		ID3D12GraphicsCommandList* cmdList,
		const std::vector<RenderItem*>& ritems);
	std::unique_ptr<MeshGeometry> CreateMeshGeometry(std::string&& name,
		std::vector<Vertex> vertices, std::vector<std::uint16_t> indices);

private:
	UINT mCbvSrvDescriptorSize = 0;

private:
	std::unique_ptr<Waves> mWaves = nullptr;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	ComPtr<ID3D12DescriptorHeap> mDescriptorHeap = nullptr;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputElements;
	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count]{};
	RenderItem* mWavesRitem = nullptr;
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;
	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12PipelineState> mOpaquePSOs = nullptr;
	FrameResource* mCurrFrameResource = nullptr;
	int mCurrFrameResIndex = 0;

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	float mTheta = 1.3f * XM_PI;
	float mPhi = XM_PIDIV4;
	float mRadius = 2.5f;

	POINT mLastMousePos;
};

TexWavesApp::TexWavesApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

TexWavesApp::~TexWavesApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool TexWavesApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	mWaves = std::make_unique<Waves>(128, 128, 1.0f, 0.03f, 4.0f, 0.2f);

	LoadTextures();
	BuildRootSignature();
	BuildDescriptorHeaps();
	BuildShadersAndInputLayout();
	BuildLandGeometry();
	BuildWavesGeometry();
	BuildBoxGeometry();
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

void TexWavesApp::LoadTextures()
{
	auto LoadTexture = [&](std::string&& name, std::wstring&& filename)
	{
		auto tex = std::make_unique<Texture>();
		ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(), 
			mCommandList.Get(), filename.c_str(),
			tex->Resource, tex->UploadHeap));
		mTextures[name] = std::move(tex);
	};
	LoadTexture("grassTex", L"../Textures/grass.dds");
	LoadTexture("waterTex", L"../Textures/Water1.dds");
	LoadTexture("fenceTex", L"../Textures/WoodCrate01.dds");
}

const int staticSamplerSize = 6;
std::array<CD3DX12_STATIC_SAMPLER_DESC, staticSamplerSize> GetStaticSamplers()
{
	std::array<CD3DX12_STATIC_SAMPLER_DESC, staticSamplerSize> samplerList{};
	auto SetSampler = [&, idx = 0](D3D12_FILTER filter, D3D12_TEXTURE_ADDRESS_MODE addressMode,
		float mipLODBias, UINT maxAnisotropy) mutable
	{
		samplerList[idx].Init(idx, filter, addressMode, addressMode, addressMode, mipLODBias, maxAnisotropy);
		++idx;
	};

	SetSampler(D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_WRAP, 0.0f, 8);
	SetSampler(D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 0.0f, 8);
	SetSampler(D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP, 0.0f, 8);
	SetSampler(D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 0.0f, 8);
	SetSampler(D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_WRAP, 0.0f, 8);
	SetSampler(D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 0.0f, 8);

	return samplerList;
}

void TexWavesApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable{};
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	const int rootParameterSize = 4;
	CD3DX12_ROOT_PARAMETER rootParameter[rootParameterSize];
	rootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
	rootParameter[1].InitAsConstantBufferView(0);
	rootParameter[2].InitAsConstantBufferView(1);
	rootParameter[3].InitAsConstantBufferView(2);

	auto staticSamplers = GetStaticSamplers();

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc{};
	rootSigDesc.Init(rootParameterSize, rootParameter, (UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr; 
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, 
		D3D_ROOT_SIGNATURE_VERSION_1, serializedSig.GetAddressOf(), errorBlob.GetAddressOf());
	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(0, serializedSig->GetBufferPointer(),
		serializedSig->GetBufferSize(), IID_PPV_ARGS(&mRootSignature)));
}

void TexWavesApp::BuildDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC descHeap{};
	descHeap.NumDescriptors = 3;
	descHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	descHeap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	md3dDevice->CreateDescriptorHeap(&descHeap, IID_PPV_ARGS(&mDescriptorHeap));

	auto CreateShaderResourceView = [&, idx = 0](auto resource) mutable
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Format = resource->GetDesc().Format;
		srvDesc.Texture2D.MipLevels = -1;
		srvDesc.Texture2D.MostDetailedMip = 0;

		CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(
			mDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), idx++, mCbvSrvDescriptorSize);
		md3dDevice->CreateShaderResourceView(resource.Get(), &srvDesc, hDescriptor);
	};

	CreateShaderResourceView(mTextures["grassTex"]->Resource);
	CreateShaderResourceView(mTextures["waterTex"]->Resource);
	CreateShaderResourceView(mTextures["fenceTex"]->Resource);
}

void TexWavesApp::BuildShadersAndInputLayout()
{
	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders/Default.hlsl", nullptr, "VS", "vs_5_0");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders/Default.hlsl", nullptr, "PS", "ps_5_0");

	mInputElements =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 1, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 1, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 1, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	};
}

float GetHillsHeight(float x, float z)
{
	return 0.3f * (z * sinf(0.1f * x) + x * cosf(0.1f * z));
}

XMFLOAT3 GetHillsNormal(float x, float z)
{
	// n = (-df/dx, 1, -df/dz)
	XMFLOAT3 n(
		-0.03f * z * cosf(0.1f * x) - 0.3f * cosf(0.1f * z),
		1.0f,
		-0.3f * sinf(0.1f * x) + 0.03f * x * sinf(0.1f * z));

	XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
	XMStoreFloat3(&n, unitNormal);

	return n;
}

std::unique_ptr<MeshGeometry> TexWavesApp::CreateMeshGeometry(std::string&& name,
	std::vector<Vertex> vertices, std::vector<std::uint16_t> indices)
{
	UINT totalICnt = (UINT)indices.size();
	UINT totalVCnt = (UINT)vertices.size();
	UINT indexByteSize = totalICnt * sizeof(std::int16_t);
	UINT vertexByteSize = totalVCnt * sizeof(Vertex);
	auto geo = std::make_unique<MeshGeometry>();

	SubmeshGeometry submesh;
	submesh.BaseVertexLocation = 0;
	submesh.StartIndexLocation = 0;
	submesh.IndexCount = totalICnt;

	geo->Name = name;
	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vertexByteSize, geo->VertexBufferUploader);
	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), indexByteSize, geo->IndexBufferUploader);
	geo->VertexBufferByteSize = vertexByteSize;
	geo->IndexBufferByteSize = indexByteSize;
	geo->VertexByteStride = sizeof(Vertex);
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->DrawArgs["grid"] = submesh;

	return geo;
}

void TexWavesApp::BuildLandGeometry()
{
	GeometryGenerator gen;
	GeometryGenerator::MeshData grid = gen.CreateGrid(160.0f, 160.0f, 50, 50);

	UINT totalVCnt = (UINT)grid.Vertices.size();
	UINT totalICnt = (UINT)grid.Indices32.size();
	UINT vertexByteSize = totalVCnt * (UINT)sizeof(Vertex);
	UINT indexByteSize = totalICnt* (UINT)sizeof(std::uint16_t);

	std::vector<Vertex> vertices(totalVCnt);
	for_each(grid.Vertices.begin(), grid.Vertices.end(), [&vertices, idx = 0](auto& v) mutable {
		vertices[idx].Pos = v.Position;
		vertices[idx].Pos.y = GetHillsHeight(v.Position.x, v.Position.z);
		vertices[idx].Normal = GetHillsNormal(v.Position.x, v.Position.z);
		vertices[idx].TexC = v.TexC;
		++idx;	});
	std::vector<std::uint16_t> indices(totalICnt);
	indices.insert(indices.end(), grid.GetIndices16().begin(), grid.GetIndices16().end());

	auto land = CreateMeshGeometry("landGeo", vertices, indices);
	mGeometries[land->Name] = std::move(land);
}

void TexWavesApp::BuildWavesGeometry()
{
	std::vector<std::uint16_t> indices(mWaves->TriangleCount() * 3);
	assert(mWaves->VertexCount() < 0x0000ffff);

	int idx = 0;
	int n = mWaves->ColumnCount();
	for (auto i : Range(0, mWaves->RowCount() - 1))
	{
		for (auto j : Range(0, n - 1))
		{
			indices[idx] = i * n + j;
			indices[idx + 1] = i * n + j + 1;
			indices[idx + 2] = (i + 1) * n + j;

			indices[idx + 3] = (i + 1) * n + j;
			indices[idx + 4] = i * n + j + 1;
			indices[idx + 5] = (i + 1) * n + j + 1;

			idx += 6; // next quad
		}
	}

	UINT VertexCount = (UINT)mWaves->VertexCount();
	UINT VertexByteSize = VertexCount * sizeof(Vertex);
	UINT indexCount = (UINT)indices.size();
	UINT indexByteSize = indexCount * sizeof(std::uint16_t);

	SubmeshGeometry smWater;
	smWater.BaseVertexLocation = 0;
	smWater.StartIndexLocation = 0;
	smWater.IndexCount = indexCount;

	auto geoWater = std::make_unique<MeshGeometry>();
	geoWater->Name = "waterGeo";
	geoWater->VertexBufferGPU = nullptr;
	geoWater->VertexBufferCPU = nullptr;
	geoWater->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		indices.data(), indexByteSize, geoWater->IndexBufferUploader);
	geoWater->DrawArgs["grid"] = smWater;
	geoWater->IndexFormat = DXGI_FORMAT_R16_UINT;
	geoWater->IndexBufferByteSize = indexByteSize;
	geoWater->VertexByteStride = sizeof(Vertex);
	geoWater->VertexBufferByteSize = VertexByteSize;

	mGeometries[geoWater->Name] = std::move(geoWater);
}

void TexWavesApp::BuildBoxGeometry()
{
	GeometryGenerator gen;
	GeometryGenerator::MeshData box = gen.CreateBox(8.0f, 8.0f, 8.0f, 3);

	std::vector<Vertex> vertices(box.Vertices.size());
	for_each(box.Vertices.begin(), box.Vertices.end(), [&vertices, idx = 0](auto& v) mutable {
		vertices[idx].Pos = v.Position;
		vertices[idx].Normal = v.Normal;
		vertices[idx].TexC = v.TexC;
		++idx;	});
	std::vector<std::uint16_t> indices(box.Indices32.size());
	indices.insert(indices.end(), box.GetIndices16().begin(), box.GetIndices16().end());

	auto geoBox = CreateMeshGeometry("boxGeo", vertices, indices);
	mGeometries[geoBox->Name] = std::move(geoBox);
}

void TexWavesApp::BuildMaterials()
{
	auto grass = std::make_unique<Material>();
	grass->Name = "grass";
	grass->MatCBIndex = 0;
	grass->DiffuseSrvHeapIndex = 0;
	grass->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	grass->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	grass->Roughness = 0.125f;
	
	auto water = std::make_unique<Material>();
	water->Name = "water";
	water->MatCBIndex = 1;
	water->DiffuseSrvHeapIndex = 1;
	water->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	water->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
	water->Roughness = 0.0f;

	auto wirefence = std::make_unique<Material>();
	wirefence->Name = "wirefence";
	wirefence->MatCBIndex = 2;
	wirefence->DiffuseSrvHeapIndex = 2;
	wirefence->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	wirefence->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	wirefence->Roughness = 0.25f;

	mMaterials[grass->Name] = std::move(grass);
	mMaterials[water->Name] = std::move(water);
	mMaterials[wirefence->Name] = std::move(wirefence);
}

void TexWavesApp::BuildRenderItems()
{
	auto& smGrid = mGeometries["waterGeo"]->DrawArgs["grid"];
	auto riWaves = std::make_unique<RenderItem>();
	riWaves->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&riWaves->TexTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f));
	riWaves->ObjCBIndex = 0;
	riWaves->Mat = mMaterials["water"].get();
	riWaves->Geo = mGeometries["waterGeo"].get();
	riWaves->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	riWaves->IndexCount = smGrid.IndexCount;
	riWaves->BaseVertexLocation = smGrid.BaseVertexLocation;
	riWaves->StartIndexLocation = smGrid.StartIndexLocation;

	mWavesRitem = riWaves.get();

	mRitemLayer[(int)RenderLayer::Opaque].emplace_back(riWaves.get());

	auto& smLand = mGeometries["landGeo"]->DrawArgs["grid"];
	auto riLand = std::make_unique<RenderItem>();
	riLand->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&riLand->TexTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f));
	riLand->ObjCBIndex = 1;
	riLand->Mat = mMaterials["grass"].get();
	riLand->Geo = mGeometries["landGeo"].get();
	riLand->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	riLand->StartIndexLocation = smLand.StartIndexLocation;
	riLand->BaseVertexLocation = smLand.BaseVertexLocation;
	riLand->IndexCount = smLand.IndexCount;

	mRitemLayer[(int)RenderLayer::Opaque].emplace_back(riLand.get());

	auto& smBox = mGeometries["boxGeo"]->DrawArgs["box"];
	auto riBox = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&riBox->World, XMMatrixTranslation(3.0f, 2.0f, -9.0f));
	riBox->ObjCBIndex = 2;
	riBox->Mat = mMaterials["wirefence"].get();
	riBox->Geo = mGeometries["boxGeo"].get();
	riBox->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	riBox->IndexCount = smBox.IndexCount;
	riBox->StartIndexLocation = smBox.StartIndexLocation;
	riBox->BaseVertexLocation = smBox.BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Opaque].emplace_back(riBox.get());
	
	mAllRitems.emplace_back(std::move(riWaves));
	mAllRitems.emplace_back(std::move(riLand));
	mAllRitems.emplace_back(std::move(riBox));
}

void TexWavesApp::BuildFrameResources()
{
	for (auto i : Range(0, gNumFrameResources))
	{
		auto frameRes = std::make_unique<FrameResource>(md3dDevice.Get(), 1, 
			(UINT)mAllRitems.size(), (UINT)mMaterials.size(), mWaves->VertexCount());
		mFrameResources.emplace_back(std::move(frameRes));
	}
}

void TexWavesApp::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{0};

	psoDesc.InputLayout = { mInputElements.data(), (UINT)mInputElements.size() };
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
		mShaders["standardVS"]->GetBufferSize()
	};
	psoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
		mShaders["opaquePS"]->GetBufferSize()
	};
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.NumRenderTargets = 1;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.pRootSignature = mRootSignature.Get();
	psoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	psoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	psoDesc.RTVFormats[0] = mBackBufferFormat;
	psoDesc.DSVFormat = mDepthStencilFormat;

	md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mOpaquePSOs));
}

void TexWavesApp::OnResize()
{
	D3DApp::OnResize();

	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);
}

void TexWavesApp::OnKeyboardInput(const GameTimer& gt)
{
}

void TexWavesApp::UpdateCamera(const GameTimer& gt)
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

void TexWavesApp::AnimateMaterials(const GameTimer& gt)
{
	auto waterMat = mMaterials["water"].get();

	float& tu = waterMat->MatTransform(3, 0);
	float& tv = waterMat->MatTransform(3, 1);

	tu += 0.1f * gt.DeltaTime();
	tv += 0.02f * gt.DeltaTime();

	if (tu >= 1.0f) tu = -1.0f;
	if (tv >= 1.0f) tv = -1.0f;

	waterMat->MatTransform(3, 0) = tu;
	waterMat->MatTransform(3, 1) = tv;

	waterMat->NumFramesDirty = gNumFrameResources;
}

void TexWavesApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto& currObjCB = mCurrFrameResource->ObjectCB;

	for (auto& ri : mAllRitems)
	{
		if (ri->NumFrameDirty <= 0)
			continue;

		XMMATRIX world = XMLoadFloat4x4(&ri->World);
		XMMATRIX tex = XMLoadFloat4x4(&ri->TexTransform);

		ObjectConstants objConstants;
		XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
		XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(tex));

		currObjCB->CopyData(ri->ObjCBIndex, objConstants);

		ri->NumFrameDirty--;
	}
}

void TexWavesApp::UpdateMaterialCBs(const GameTimer& gt)
{
	auto& currMatCB = mCurrFrameResource->MaterialCB;

	for (auto& m : mMaterials)
	{
		auto& currMat = m.second;
		if (currMat->NumFramesDirty <= 0)
			continue;
		XMMATRIX matTransform = XMLoadFloat4x4(&currMat->MatTransform);

		MaterialConstants matConstants;
		XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));
		matConstants.DiffuseAlbedo = currMat->DiffuseAlbedo;
		matConstants.FresnelR0 = currMat->FresnelR0;
		matConstants.Roughness = currMat->Roughness;

		currMatCB->CopyData(currMat->MatCBIndex, matConstants);
		currMat->NumFramesDirty--;
	}
}

void TexWavesApp::UpdateMainPassCB(const GameTimer& gt)
{
	auto& mainPassCB = mCurrFrameResource->PassCB;

	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);
	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(nullptr, view);
	XMMATRIX invProj = XMMatrixInverse(nullptr, proj);
	XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj);

	PassConstants passConstants;
	passConstants.DeltaTime = gt.DeltaTime();
	passConstants.TotalTime = gt.TotalTime();
	passConstants.FarZ = 1000.0f;
	passConstants.NearZ = 1.0f;
	passConstants.EyePosW = mEyePos;
	passConstants.RenderTargetSize = { (float)mClientWidth, (float)mClientHeight };
	passConstants.InvRenderTargetSize = { 1.0f / (float)mClientWidth, 1.0f / (float)mClientHeight };
	XMStoreFloat4x4(&passConstants.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&passConstants.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&passConstants.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&passConstants.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&passConstants.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&passConstants.InvViewProj, XMMatrixTranspose(invViewProj));
	passConstants.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
	passConstants.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	passConstants.Lights[0].Strength = { 0.9f, 0.9f, 0.9f };
	passConstants.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	passConstants.Lights[1].Strength = { 0.5f, 0.5f, 0.5f };
	passConstants.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	passConstants.Lights[2].Strength = { 0.2f, 0.2f, 0.2f };

	mainPassCB->CopyData(0, passConstants);
}

void TexWavesApp::UpdateWaves(const GameTimer& gt)
{
	// Every quarter second, generate a random wave.
	static float t_base = 0.0f;
	if ((mTimer.TotalTime() - t_base) >= 0.25f)
	{
		t_base += 0.25f;

		int i = MathHelper::Rand(4, mWaves->RowCount() - 5);
		int j = MathHelper::Rand(4, mWaves->ColumnCount() - 5);

		float r = MathHelper::RandF(0.2f, 0.5f);

		mWaves->Disturb(i, j, r);
	}

	// Update the wave simulation.
	mWaves->Update(gt.DeltaTime());

	// Update the wave vertex buffer with the new solution.
	auto currWavesVB = mCurrFrameResource->WavesVB.get();
	for (int i = 0; i < mWaves->VertexCount(); ++i)
	{
		Vertex v;

		v.Pos = mWaves->Position(i);
		v.Normal = mWaves->Normal(i);

		// Derive tex-coords from position by 
		// mapping [-w/2,w/2] --> [0,1]
		v.TexC.x = 0.5f + v.Pos.x / mWaves->Width();
		v.TexC.y = 0.5f - v.Pos.z / mWaves->Depth();

		currWavesVB->CopyData(i, v);
	}

	// Set the dynamic VB of the wave renderitem to the current frame VB.
	mWavesRitem->Geo->VertexBufferGPU = currWavesVB->Resource();
}

void TexWavesApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);
	UpdateCamera(gt);

	mCurrFrameResIndex = (mCurrFrameResIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResIndex].get();
	
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		HANDLE handle = ::CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
		mFence->SetEventOnCompletion(mCurrFrameResource->Fence, handle);
		WaitForSingleObject(handle, INFINITE);
		CloseHandle(handle);
	}

	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialCBs(gt);
	UpdateMainPassCB(gt);
	UpdateWaves(gt);
}

void TexWavesApp::DrawRenderItems(
	ID3D12GraphicsCommandList* cmdList,
	const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();

	for (auto ri : ritems)
	{
		cmdList->IASetVertexBuffers(1, 1, &RvToLv(ri->Geo->VertexBufferView()));
		cmdList->IASetIndexBuffer(&RvToLv(ri->Geo->IndexBufferView()));
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);
		cmdList->SetGraphicsRootDescriptorTable(0, tex);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

		cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
		cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);
		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

void TexWavesApp::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;
	ThrowIfFailed(cmdListAlloc->Reset());
	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mOpaquePSOs.Get()));

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	mCommandList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(
		CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET)));

	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(),
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	mCommandList->OMSetRenderTargets(1,
		&RvToLv(CurrentBackBufferView()), true, &RvToLv(DepthStencilView()));

	ID3D12DescriptorHeap* descriptorHeaps[] = { mDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());
	
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

	mCommandList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(
		CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT)));
	ThrowIfFailed(mCommandList->Close());

	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	mCurrFrameResource->Fence = ++mCurrentFence;
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void TexWavesApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}
void TexWavesApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}
void TexWavesApp::OnMouseMove(WPARAM btnState, int x, int y)
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

		mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);
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
		TexWavesApp theApp(hInstance);
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