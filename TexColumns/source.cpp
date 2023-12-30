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

class TexColumnsApp : public D3DApp
{
public:
	TexColumnsApp(HINSTANCE hInstance);
	TexColumnsApp(const TexColumnsApp& rhs) = delete;
	TexColumnsApp& operator=(const TexColumnsApp& rhs) = delete;
	~TexColumnsApp();

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

	void LoadDDSTextureFile(std::string&& name, std::wstring&& filename);
	void LoadTextures();
	void BuildRootSignature();
	void CreateShaderResourceView(std::string&& texName, CD3DX12_CPU_DESCRIPTOR_HANDLE& handle);
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
	FrameResource* mCurrFrameResource = nullptr;
	int nCurrFrameResourceIndex = 0;

	UINT mCbvSrvDescriptorSize = 0;

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mOpaquePSOs;
	
	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayouts;

	std::vector<std::unique_ptr<RenderItem>> mAllRitems;
	std::vector<RenderItem*> mOpaqueRItems;
	PassConstants mMainPassCB;

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	float mTheta = 1.3f * XM_PI;
	float mPhi = XM_PIDIV4;
	float mRadius = 2.5f;

	POINT mLastMousePos;
};

TexColumnsApp::TexColumnsApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

TexColumnsApp::~TexColumnsApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool TexColumnsApp::Initialize()
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

void TexColumnsApp::LoadDDSTextureFile(std::string&& name, std::wstring&& filename)
{
	auto tex = std::make_unique<Texture>();
	tex->Name = name;
	tex->Filename = filename;

	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(), mCommandList.Get(),
		filename.c_str(), tex->Resource, tex->UploadHeap));

	mTextures[name] = std::move(tex);
}


void TexColumnsApp::LoadTextures()
{
	LoadDDSTextureFile("bricksTex", L"../Textures/bricks.dds");
	LoadDDSTextureFile("stoneTex", L"../Textures/stone.dds");
	LoadDDSTextureFile("tileTex", L"../Textures/tile.dds");
}

std::array<CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers()
{
	CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0,
		D3D12_FILTER_MIN_MAG_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP);

	CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1,
		D3D12_FILTER_MIN_MAG_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

	CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2,
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP);

	CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3,
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
	
	CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4,
		D3D12_FILTER_ANISOTROPIC,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP);

	CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5,
		D3D12_FILTER_ANISOTROPIC,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
	
	return
	{
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp
	};
}

void TexColumnsApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);

	CD3DX12_ROOT_PARAMETER slotRootParameter[4];
	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[1].InitAsConstantBufferView(0);
	slotRootParameter[2].InitAsConstantBufferView(1);
	slotRootParameter[3].InitAsConstantBufferView(2);

	auto staticSamplers = GetStaticSamplers();

	D3D12_ROOT_SIGNATURE_DESC rootSigDesc{};
	rootSigDesc.NumParameters = 4;
	rootSigDesc.pParameters = slotRootParameter;
	rootSigDesc.NumStaticSamplers = 6;
	rootSigDesc.pStaticSamplers = staticSamplers.data();
	rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, 	D3D_ROOT_SIGNATURE_VERSION_1, 
		&serializedRootSig, &errorBlob);

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(0, 
		serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), 
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void TexColumnsApp::CreateShaderResourceView(std::string&& texName, CD3DX12_CPU_DESCRIPTOR_HANDLE& handle)
{
	auto res = mTextures[texName]->Resource;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = res->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = res->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	md3dDevice->CreateShaderResourceView(res.Get(), &srvDesc, handle);
}

void TexColumnsApp::BuildDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
	heapDesc.NumDescriptors = 3;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle{ 
		mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart() };
	CreateShaderResourceView("bricksTex", rtvHandle);
	rtvHandle.Offset(1, mCbvSrvDescriptorSize);
	CreateShaderResourceView("stoneTex", rtvHandle);
	rtvHandle.Offset(1, mCbvSrvDescriptorSize);
	CreateShaderResourceView("tileTex", rtvHandle);
}

void TexColumnsApp::BuildShadersAndInputLayout()
{
	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_0");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "PS", "ps_5_0");

	mInputLayouts =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};
}

void TexColumnsApp::BuildShapeGeometry()
{
	GeometryGenerator gen;
	GeometryGenerator::MeshData box = gen.CreateBox(1.0f, 1.0f, 1.0f, 3);
	GeometryGenerator::MeshData grid = gen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = gen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = gen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);
	
	INT boxVCnt = (INT)box.Vertices.size();
	INT gridVCnt = (INT)grid.Vertices.size();
	INT sphereVCnt = (INT)sphere.Vertices.size();
	INT cylinderVCnt = (INT)cylinder.Vertices.size();
	INT totalVCnt = boxVCnt + gridVCnt + sphereVCnt + cylinderVCnt;
	
	std::vector<Vertex> vertices(totalVCnt);
	int idx = 0;
	auto vTov = [&vertices, &idx](GeometryGenerator::Vertex& v)
	{
		vertices[idx].Pos = v.Position;
		vertices[idx].Normal = v.Normal;
		vertices[idx].TexC = v.TexC;
		idx++;
	};
	for_each(box.Vertices.begin(), box.Vertices.end(), vTov);
	for_each(grid.Vertices.begin(), grid.Vertices.end(), vTov); 
	for_each(sphere.Vertices.begin(), sphere.Vertices.end(), vTov);
	for_each(cylinder.Vertices.begin(), cylinder.Vertices.end(), vTov);

	UINT boxICnt = (UINT)box.GetIndices16().size();
	UINT gridICnt = (UINT)grid.GetIndices16().size();
	UINT sphereICnt = (UINT)sphere.GetIndices16().size();
	UINT cylinderICnt = (UINT)cylinder.GetIndices16().size();
	UINT totalICnt = boxICnt + gridICnt + sphereICnt + cylinderICnt;

	std::vector<std::int16_t> indices;
	indices.insert(indices.end(), box.GetIndices16().begin(), box.GetIndices16().end());
	indices.insert(indices.end(), grid.GetIndices16().begin(), grid.GetIndices16().end());
	indices.insert(indices.end(), sphere.GetIndices16().begin(), sphere.GetIndices16().end());
	indices.insert(indices.end(), cylinder.GetIndices16().begin(), cylinder.GetIndices16().end());
	
	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";

	SubmeshGeometry smBox;
	smBox.BaseVertexLocation = 0;
	smBox.StartIndexLocation = 0;
	smBox.IndexCount = boxICnt;

	SubmeshGeometry smGrid;
	smGrid.BaseVertexLocation = boxVCnt;
	smGrid.StartIndexLocation = boxICnt;
	smGrid.IndexCount = gridICnt;

	SubmeshGeometry smSphere;
	smSphere.BaseVertexLocation = boxVCnt + gridVCnt;
	smSphere.StartIndexLocation = boxICnt + gridICnt;
	smSphere.IndexCount = sphereICnt;

	SubmeshGeometry smCylinder;
	smCylinder.BaseVertexLocation = boxVCnt + gridVCnt + sphereVCnt;
	smCylinder.StartIndexLocation = boxICnt + gridICnt + sphereICnt;
	smCylinder.IndexCount = cylinderICnt;

	geo->DrawArgs["box"] = smBox;
	geo->DrawArgs["grid"] = smGrid;
	geo->DrawArgs["sphere"] = smSphere;
	geo->DrawArgs["cylinder"] = smCylinder;

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = totalVCnt * sizeof(Vertex);
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = totalICnt * sizeof(std::uint16_t);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		vertices.data(), geo->VertexBufferByteSize, geo->VertexBufferUploader);
	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		indices.data(), geo->IndexBufferByteSize, geo->IndexBufferUploader);

	mGeometries[geo->Name] = std::move(geo);
}

void TexColumnsApp::BuildMaterials()
{
	//CB와 SRV인덱스가 두개나 필요한지 모르겠다.
	auto bricks0 = std::make_unique<Material>();
	bricks0->Name = "bricks0";
	bricks0->MatCBIndex = 0;
	bricks0->DiffuseSrvHeapIndex = 0;
	bricks0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	bricks0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	bricks0->Roughness = 0.1f;

	auto stone0 = std::make_unique<Material>();
	stone0->Name = "stone0";
	stone0->MatCBIndex = 1;
	stone0->DiffuseSrvHeapIndex = 1;
	stone0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	stone0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	stone0->Roughness = 0.3f;

	auto tile0 = std::make_unique<Material>();
	tile0->Name = "tile0";
	tile0->MatCBIndex = 2;
	tile0->DiffuseSrvHeapIndex = 2;
	tile0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	tile0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	tile0->Roughness = 0.3f;
	
	mMaterials["bricks0"] = std::move(bricks0);
	mMaterials["stone0"] = std::move(stone0);
	mMaterials["tile0"] = std::move(tile0);
}

void TexColumnsApp::BuildRenderItems()
{
	auto boxRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 1.0f, 0.0f));
	XMStoreFloat4x4(&boxRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	boxRitem->ObjCBIndex = 0;
	boxRitem->Mat = mMaterials["stone0"].get();
	boxRitem->Geo = mGeometries["shapeGeo"].get();
	boxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	auto& smBox = boxRitem->Geo->DrawArgs["box"];
	boxRitem->BaseVertexLocation = smBox.BaseVertexLocation;
	boxRitem->StartIndexLocation = smBox.StartIndexLocation;
	boxRitem->IndexCount = smBox.IndexCount;
	mAllRitems.emplace_back(std::move(boxRitem));

	auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(8.0f, 8.0f, 1.0f));
	gridRitem->ObjCBIndex = 1;
	gridRitem->Mat = mMaterials["tile0"].get();
	gridRitem->Geo = mGeometries["shapeGeo"].get();
	gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	auto& smGrid = gridRitem->Geo->DrawArgs["grid"];
	gridRitem->BaseVertexLocation = smGrid.BaseVertexLocation;
	gridRitem->StartIndexLocation = smGrid.StartIndexLocation;
	gridRitem->IndexCount = smGrid.IndexCount;
	mAllRitems.emplace_back(std::move(gridRitem));

	XMMATRIX brickTexTransform = XMMatrixScaling(1.0f, 1.0f, 1.0f);
	UINT objCBIndex = 2;
	for (auto i : Range(0, 5))
	{
		XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
		XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f);
		XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);
		XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f);

		auto leftCylRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&leftCylRitem->World, leftCylWorld);
		XMStoreFloat4x4(&leftCylRitem->TexTransform, brickTexTransform);
		leftCylRitem->ObjCBIndex = objCBIndex++;
		leftCylRitem->Mat = mMaterials["bricks0"].get();
		leftCylRitem->Geo = mGeometries["shapeGeo"].get();
		leftCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		auto& smLeftCyl = leftCylRitem->Geo->DrawArgs["cylinder"];
		leftCylRitem->BaseVertexLocation = smLeftCyl.BaseVertexLocation;
		leftCylRitem->StartIndexLocation = smLeftCyl.StartIndexLocation;
		leftCylRitem->IndexCount = smLeftCyl.IndexCount;
		mAllRitems.emplace_back(std::move(leftCylRitem));

		auto rightCylRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&rightCylRitem->World, rightCylWorld);
		XMStoreFloat4x4(&rightCylRitem->TexTransform, brickTexTransform);
		rightCylRitem->ObjCBIndex = objCBIndex++;
		rightCylRitem->Mat = mMaterials["bricks0"].get();
		rightCylRitem->Geo = mGeometries["shapeGeo"].get();
		rightCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		auto& smRightCyl = rightCylRitem->Geo->DrawArgs["cylinder"];
		rightCylRitem->BaseVertexLocation = smRightCyl.BaseVertexLocation;
		rightCylRitem->StartIndexLocation = smRightCyl.StartIndexLocation;
		rightCylRitem->IndexCount = smRightCyl.IndexCount;
		mAllRitems.emplace_back(std::move(rightCylRitem));

		auto leftSphereRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
		XMStoreFloat4x4(&leftSphereRitem->TexTransform, brickTexTransform);
		leftSphereRitem->ObjCBIndex = objCBIndex++;
		leftSphereRitem->Mat = mMaterials["stone0"].get();
		leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
		leftSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		auto& smLeftSphere = leftSphereRitem->Geo->DrawArgs["sphere"];
		leftSphereRitem->BaseVertexLocation = smLeftSphere.BaseVertexLocation;
		leftSphereRitem->StartIndexLocation = smLeftSphere.StartIndexLocation;
		leftSphereRitem->IndexCount = smLeftSphere.IndexCount;
		mAllRitems.emplace_back(std::move(leftSphereRitem));

		auto rightSphereRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
		XMStoreFloat4x4(&rightSphereRitem->TexTransform, brickTexTransform);
		rightSphereRitem->ObjCBIndex = objCBIndex++;
		rightSphereRitem->Mat = mMaterials["stone0"].get();
		rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
		rightSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		auto& smRightSphere = rightSphereRitem->Geo->DrawArgs["sphere"];
		rightSphereRitem->BaseVertexLocation = smRightSphere.BaseVertexLocation;
		rightSphereRitem->StartIndexLocation = smRightSphere.StartIndexLocation;
		rightSphereRitem->IndexCount = smRightSphere.IndexCount;
		mAllRitems.emplace_back(std::move(rightSphereRitem));
	}

	for (auto& e : mAllRitems)
		mOpaqueRItems.emplace_back(e.get());
}

void TexColumnsApp::BuildFrameResources()
{
	for (auto i : Range(0, gNumFrameResources))
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			1, (UINT)mAllRitems.size(), (UINT)mMaterials.size()));
	}
}

void TexColumnsApp::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePSO{ 0 };
	ZeroMemory(&opaquePSO, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePSO.VS.pShaderBytecode = reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer());
	opaquePSO.VS.BytecodeLength = mShaders["standardVS"]->GetBufferSize();
	opaquePSO.PS.pShaderBytecode = reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer());
	opaquePSO.PS.BytecodeLength = mShaders["opaquePS"]->GetBufferSize();
	opaquePSO.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePSO.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePSO.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePSO.NumRenderTargets = 1;
	opaquePSO.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePSO.InputLayout.pInputElementDescs = mInputLayouts.data();
	opaquePSO.InputLayout.NumElements = (UINT)mInputLayouts.size();
	opaquePSO.pRootSignature = mRootSignature.Get();
	opaquePSO.DSVFormat = mDepthStencilFormat;
	opaquePSO.RTVFormats[0] = mBackBufferFormat;
	opaquePSO.SampleMask = UINT_MAX;
	opaquePSO.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePSO.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePSO, IID_PPV_ARGS(&mOpaquePSOs["opaque"])));
}

void TexColumnsApp::OnResize()
{
	D3DApp::OnResize();

	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);
}

void TexColumnsApp::OnKeyboardInput(const GameTimer& gt)
{
}

void TexColumnsApp::UpdateCamera(const GameTimer& gt)
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

void TexColumnsApp::AnimateMaterials(const GameTimer& gt)
{
}

void TexColumnsApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto& currCB = mCurrFrameResource->ObjectCB;
	
	for (auto& ri : mAllRitems)
	{
		if (ri->NumFrameDirty <= 0)
			continue;

		XMMATRIX world = XMLoadFloat4x4(&ri->World);
		XMMATRIX texTransform = XMLoadFloat4x4(&ri->TexTransform);

		ObjectConstants obj;
		XMStoreFloat4x4(&obj.World, XMMatrixTranspose(world));
		XMStoreFloat4x4(&obj.TexTransform, XMMatrixTranspose(texTransform));

		currCB->CopyData(ri->ObjCBIndex, obj);

		ri->NumFrameDirty--;
	}
}

void TexColumnsApp::UpdateMaterialCBs(const GameTimer& gt)
{
	auto& currMaterialCB = mCurrFrameResource->MaterialCB;

	for (auto& m : mMaterials)
	{
		auto& currMat = m.second;
		if (currMat->NumFramesDirty <= 0)
			continue;

		XMMATRIX matTransform = XMLoadFloat4x4(&currMat->MatTransform); 

		MaterialConstants mConstants;
		mConstants.DiffuseAlbedo = currMat->DiffuseAlbedo;
		mConstants.FresnelR0 = currMat->FresnelR0;
		mConstants.Roughness = currMat->Roughness;
		XMStoreFloat4x4(&mConstants.MatTransform, XMMatrixTranspose(matTransform));

		currMaterialCB->CopyData(currMat->MatCBIndex, mConstants);

		currMat->NumFramesDirty--;
	}
}

void TexColumnsApp::UpdateMainPassCB(const GameTimer& gt)
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
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.EyePosW = mEyePos;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.RenderTargetSize = { (float)mClientWidth, (float)mClientHeight };
	mMainPassCB.InvRenderTargetSize = { 1.0f / (float)mClientWidth, 1.0f / (float)mClientHeight };
	mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
	mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[0].Strength = { 0.8f, 0.8f, 0.8f };
	mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[1].Strength = { 0.4f, 0.4f, 0.4f };
	mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	mMainPassCB.Lights[2].Strength = { 0.2f, 0.2f, 0.2f };

	auto& currPassCB = mCurrFrameResource->PassCB;
	currPassCB->CopyData(0, mMainPassCB);
}

void TexColumnsApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);
	UpdateCamera(gt);

	nCurrFrameResourceIndex = (nCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[nCurrFrameResourceIndex].get();
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		HANDLE handle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, handle));
		WaitForSingleObject(handle, INFINITE);
		CloseHandle(handle);
	}

	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialCBs(gt);
	UpdateMainPassCB(gt);
}

void TexColumnsApp::DrawRenderItems(
	ID3D12GraphicsCommandList* cmdList,
	const std::vector<RenderItem*>& ritems)
{
	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();

	UINT objByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	for (auto ri : ritems)
	{
		cmdList->IASetVertexBuffers(0, 1, &RvToLv(ri->Geo->VertexBufferView()));
		cmdList->IASetIndexBuffer(&RvToLv(ri->Geo->IndexBufferView()));
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);
		
		CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = 
			objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress =
			matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matByteSize;

		cmdList->SetGraphicsRootDescriptorTable(0, tex);
		cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
		cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

void TexColumnsApp::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;
	ThrowIfFailed(cmdListAlloc->Reset());
	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mOpaquePSOs["opaque"].Get()));

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

	ID3D12DescriptorHeap* descHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descHeaps), descHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	D3D12_GPU_VIRTUAL_ADDRESS mainPassAddress = mCurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress();
	mCommandList->SetGraphicsRootConstantBufferView(2, mainPassAddress);

	DrawRenderItems(mCommandList.Get(), mOpaqueRItems);

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

void TexColumnsApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}
void TexColumnsApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}
void TexColumnsApp::OnMouseMove(WPARAM btnState, int x, int y)
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
		TexColumnsApp theApp(hInstance);
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