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

//#pragma comment(lib, "d3dcompiler.lib")
//#pragma comment(lib, "D3D12.lib")

const int gNumFrameResources = 3;

struct RenderItem
{
	RenderItem() = default;

	XMFLOAT4X4 World = MathHelper::Identity4x4();
	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	int NumFramesDirty = gNumFrameResources;
	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
};

class LitColumnsApp : public D3DApp
{
public:
	LitColumnsApp(HINSTANCE hInstance);
	LitColumnsApp(const LitColumnsApp& rhs) = delete;
	LitColumnsApp& operator=(const LitColumnsApp& rhs) = delete;
	~LitColumnsApp();

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

	void BuildRootSignature();
	void BuildShadersAndInputLayout();
	void BuildShapeGeometry();
	void BuildSkullGeometry();
	void BuildMaterials();
	void SetRenderItem(UINT cbIdx, const XMMATRIX& _world, const XMMATRIX& _tex,
		std::string geoName, std::string submeshName, std::string matName);
	void BuildRenderItems();
	void BuildFrameResources();
	void BuildPSOs();
	
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

private:
	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource = nullptr;
	UINT mCurrFrameResourceIndex = 0;

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShader;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
	ComPtr<ID3D12PipelineState> mOpaquePSO = nullptr;

	std::vector<std::unique_ptr<RenderItem>> mAllRitems;
	std::vector<RenderItem*> mOpaueRitem;

	PassConstants mMainPassCB;
	
	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();
	
	float mPhi = 0.2f * XM_PI;
	float mTheta = 1.5f * XM_PI;
	float mRadius = 15.0f;

	POINT mLastMousePos;
};

LitColumnsApp::LitColumnsApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

LitColumnsApp::~LitColumnsApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool LitColumnsApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	BuildRootSignature();
	BuildShadersAndInputLayout();
	BuildShapeGeometry();
	BuildSkullGeometry();
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

void LitColumnsApp::BuildRootSignature()
{
	CD3DX12_ROOT_PARAMETER rootParameter[3];
	rootParameter[0].InitAsConstantBufferView(0);
	rootParameter[1].InitAsConstantBufferView(1);
	rootParameter[2].InitAsConstantBufferView(2);
	
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(3, rootParameter, 0, nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	
	ComPtr<ID3DBlob> serializeRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc,
		D3D_ROOT_SIGNATURE_VERSION_1, 
		serializeRootSig.GetAddressOf(), 
		errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
		::OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
	ThrowIfFailed(hr);

	md3dDevice->CreateRootSignature(0,
		serializeRootSig->GetBufferPointer(), serializeRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf()));
}

void LitColumnsApp::BuildShadersAndInputLayout()
{
	mShader["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
	mShader["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "PS", "ps_5_1");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
}

void SetVertexPosAndNormal(Vertex& outV, const GeometryGenerator::Vertex& inV)
{
	outV.Pos = inV.Position;
	outV.Normal = inV.Normal;
}

void LitColumnsApp::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.5f, 0.5f, 1.5f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = boxVertexOffset + static_cast<UINT>(box.Vertices.size());
	UINT sphereVertexOffset = gridVertexOffset + static_cast<UINT>(grid.Vertices.size());
	UINT cylinderVertexOffset = sphereVertexOffset + static_cast<UINT>(sphere.Vertices.size());

	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = boxIndexOffset + static_cast<UINT>(box.Indices32.size());
	UINT sphereIndexOffset = gridIndexOffset + static_cast<UINT>(grid.Indices32.size());
	UINT cylinderIndexOffset = sphereIndexOffset + static_cast<UINT>(sphere.Indices32.size());

	SubmeshGeometry smBox;
	smBox.IndexCount = static_cast<UINT>(box.Indices32.size());
	smBox.BaseVertexLocation = boxVertexOffset;
	smBox.StartIndexLocation = boxIndexOffset;
	
	SubmeshGeometry smGrid;
	smGrid.IndexCount = static_cast<UINT>(grid.Indices32.size());
	smGrid.BaseVertexLocation = gridVertexOffset;
	smGrid.StartIndexLocation = gridIndexOffset;

	SubmeshGeometry smSphere;
	smSphere.IndexCount = static_cast<UINT>(sphere.Indices32.size());
	smSphere.BaseVertexLocation = sphereVertexOffset;
	smSphere.StartIndexLocation = sphereIndexOffset;

	SubmeshGeometry smCylinder;
	smCylinder.IndexCount = static_cast<UINT>(cylinder.Indices32.size());
	smCylinder.BaseVertexLocation = cylinderVertexOffset;
	smCylinder.StartIndexLocation = cylinderIndexOffset;

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";
	geo->DrawArgs["box"] = smBox;
	geo->DrawArgs["grid"] = smGrid;
	geo->DrawArgs["sphere"] = smSphere;
	geo->DrawArgs["cylinder"] = smCylinder;

	auto totalVertexCount = 
		box.Vertices.size() + grid.Vertices.size() + sphere.Vertices.size() + cylinder.Vertices.size();
	std::vector<Vertex> vertices(totalVertexCount);
	UINT idx = 0;
	for (auto& mdVertices : { box.Vertices, grid.Vertices, sphere.Vertices, cylinder.Vertices })
	{
		for_each( mdVertices.begin(), mdVertices.end(), [&vertices, &idx](auto& vertex)
			{ SetVertexPosAndNormal(vertices[idx++], vertex); });
	}
	const UINT vbByteSize = static_cast<UINT>(vertices.size()) * sizeof(Vertex);

	std::vector<uint16_t> indices;
	indices.insert(indices.end(), box.Indices32.begin(), box.Indices32.end());
	indices.insert(indices.end(), grid.Indices32.begin(), grid.Indices32.end());
	indices.insert(indices.end(), sphere.Indices32.begin(), sphere.Indices32.end());
	indices.insert(indices.end(), cylinder.Indices32.begin(), cylinder.Indices32.end());
	const UINT ibByteSize = static_cast<UINT>(indices.size()) * sizeof(uint16_t);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		vertices.data(), vbByteSize, geo->VertexBufferUploader);
	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), 
		indices.data(), ibByteSize, geo->IndexBufferUploader);
	
	geo->VertexBufferByteSize = vbByteSize;
	geo->VertexByteStride = sizeof(Vertex);
	geo->IndexBufferByteSize = ibByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;

	mGeometries[geo->Name] = std::move(geo);	
}

void LitColumnsApp::BuildSkullGeometry()
{
	std::ifstream fin("Models/skull.txt");
	if (fin.bad())
	{
		MessageBox(0, L"Models/Skull.txt not found", 0, 0);
		return;
	}

	UINT vCount = 0;
	UINT iCount = 0;
	std::string ignore;
	fin >> ignore >> vCount;
	fin >> ignore >> iCount;
	fin >> ignore >> ignore >> ignore >> ignore;

	std::vector<Vertex> vertices(vCount);
	for (auto i : Range(0, vCount))
	{
		fin >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z;
		fin >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z;
	}
	fin >> ignore >> ignore >> ignore;

	std::vector<std::int32_t> indices(iCount * 3);
	for (auto i : Range(0, iCount))
	{
		fin >> indices[i * 3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
	}
	fin.close();

	UINT vbByteSize = static_cast<UINT>(vertices.size()) * sizeof(Vertex);
	UINT ibByteSize = static_cast<UINT>(indices.size()) * sizeof(std::int32_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "skullGeo";

	auto& submesh = geo->DrawArgs["skull"];
	submesh.IndexCount = static_cast<UINT>(indices.size());
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->VertexBufferByteSize = vbByteSize;
	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(
		md3dDevice.Get(), mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);
	geo->VertexByteStride = sizeof(Vertex);
	geo->IndexBufferByteSize = ibByteSize;
	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(
		md3dDevice.Get(), mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);
	geo->IndexFormat = DXGI_FORMAT_R32_UINT;
	
	mGeometries[geo->Name] = std::move(geo);
}

void LitColumnsApp::BuildMaterials()
{
	auto bricks0 = std::make_unique<Material>();
	bricks0->Name = "bricks0";
	bricks0->MatCBIndex = 0;
	bricks0->DiffuseSrvHeapIndex = 0;
	bricks0->DiffuseAlbedo = XMFLOAT4(Colors::ForestGreen);
	bricks0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	bricks0->Roughness = 0.1f;

	auto stone0 = std::make_unique<Material>();
	stone0->Name = "stone0";
	stone0->MatCBIndex = 1;
	stone0->DiffuseSrvHeapIndex = 1;
	stone0->DiffuseAlbedo = XMFLOAT4(Colors::LightSteelBlue);
	stone0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	stone0->Roughness = 0.3f;

	auto tile0 = std::make_unique<Material>();
	tile0->Name = "tile0";
	tile0->MatCBIndex = 2;
	tile0->DiffuseSrvHeapIndex = 2;
	tile0->DiffuseAlbedo = XMFLOAT4(Colors::LightGray);
	tile0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	tile0->Roughness = 0.2f;

	auto skullMat = std::make_unique<Material>();
	skullMat->Name = "skullMat";
	skullMat->MatCBIndex = 3;
	skullMat->DiffuseSrvHeapIndex = 3;
	skullMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	skullMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	skullMat->Roughness = 0.3f;

	mMaterials[bricks0->Name] = std::move(bricks0);
	mMaterials[stone0->Name] = std::move(stone0);
	mMaterials[tile0->Name] = std::move(tile0);
	mMaterials[skullMat->Name] = std::move(skullMat);
}

void LitColumnsApp::SetRenderItem( UINT cbIdx, const XMMATRIX& _world, const XMMATRIX& _tex,
	std::string geoName, std::string submeshName, std::string matName)
{
	auto renderItem = std::make_unique<RenderItem>();

	renderItem->ObjCBIndex = cbIdx;
	XMStoreFloat4x4(&renderItem->World, _world);
	XMStoreFloat4x4(&renderItem->TexTransform, _tex);
	renderItem->Geo = mGeometries[geoName].get();
	auto& submesh = mGeometries[geoName]->DrawArgs[submeshName];
	renderItem->Mat = mMaterials[matName].get();
	renderItem->IndexCount = submesh.IndexCount;
	renderItem->BaseVertexLocation = submesh.BaseVertexLocation;
	renderItem->StartIndexLocation = submesh.StartIndexLocation;
	renderItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	mAllRitems.push_back(std::move(renderItem));
}

void LitColumnsApp::BuildRenderItems()
{
	SetRenderItem(0, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f), XMMatrixScaling(1.0f, 1.0f, 1.0f),
		"shapeGeo", "box", "stone0");
	SetRenderItem(1, XMMatrixIdentity(), XMMatrixScaling(8.0f, 8.0f, 1.0f),
		"shapeGeo", "grid", "tile0");
	SetRenderItem(2, XMMatrixScaling(0.5f, 0.5f, 0.5f) * XMMatrixTranslation(0.0f, 1.0f, 0.0f), XMMatrixIdentity(),
		"skullGeo", "skull", "skullMat");

	const int rendermeshCount = 4;
	std::string submeshAndMat[rendermeshCount][2] =
	{
		{"cylinder", "bricks0"},
		{"cylinder", "bricks0"},
		{"sphere", "stone0"},
		{"sphere", "stone0"},
	};
	XMMATRIX matMatrices[rendermeshCount] =
	{
		XMMatrixScaling(1.0f, 1.0f, 1.0f),
		XMMatrixScaling(1.0f, 1.0f, 1.0f),
		XMMatrixIdentity(),
		XMMatrixIdentity()
	};
	UINT objCBIndex = 3;
	for (auto i : Range(0, 5))
	{
		XMMATRIX trasMatrices[rendermeshCount] =
		{
			XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f),
			XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f),
			XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f),
			XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f)
		};
		for (auto meshIdx : Range(0, rendermeshCount))
		{
			SetRenderItem(objCBIndex++, trasMatrices[meshIdx], matMatrices[meshIdx],
				"shapeGeo", submeshAndMat[meshIdx][0], submeshAndMat[meshIdx][1]);
		}
	}

	for (auto& e : mAllRitems)
		mOpaueRitem.push_back(e.get());
}

void LitColumnsApp::BuildFrameResources()
{
	for (auto i : Range(0, gNumFrameResources))
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(), 1,
			static_cast<UINT>(mAllRitems.size()), static_cast<UINT>(mMaterials.size())));
	}
}

void LitColumnsApp::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueState;

	ZeroMemory(&opaqueState, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaqueState.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	opaqueState.pRootSignature = mRootSignature.Get();
	opaqueState.VS =
	{
		static_cast<BYTE*>(mShader["standardVS"].Get()->GetBufferPointer()),
		mShader["standardVS"].Get()->GetBufferSize()
	};
	opaqueState.PS = 
	{ 
		static_cast<BYTE*>(mShader["opaquePS"].Get()->GetBufferPointer()), 
		mShader["opaquePS"].Get()->GetBufferSize() 
	};
	opaqueState.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaqueState.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaqueState.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaqueState.SampleMask = UINT_MAX;
	opaqueState.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaqueState.NumRenderTargets = 1;
	opaqueState.RTVFormats[0] = mBackBufferFormat;
	opaqueState.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaqueState.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaqueState.DSVFormat = mDepthStencilFormat;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaqueState, IID_PPV_ARGS(&mOpaquePSO)));
}

void LitColumnsApp::OnResize()
{
	D3DApp::OnResize();

	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);
}

void LitColumnsApp::OnKeyboardInput(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();
}

void LitColumnsApp::UpdateCamera(const GameTimer& gt)
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

void LitColumnsApp::AnimateMaterials(const GameTimer& gt)
{
}

void Float4x4Transpose(XMFLOAT4X4* dest, const XMFLOAT4X4& src)
{
	XMStoreFloat4x4(dest, XMMatrixTranspose(XMLoadFloat4x4(&src)));
}

void LitColumnsApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currCB = mCurrFrameResource->ObjectCB.get();
	for (auto& e : mAllRitems)
	{
		if (e->NumFramesDirty <= 0)
			continue;

		ObjectConstants objConstants;
		Float4x4Transpose(&objConstants.World, e->World);
		Float4x4Transpose(&objConstants.TexTransform, e->TexTransform);

		currCB->CopyData(e->ObjCBIndex, objConstants);
		
		e->NumFramesDirty--;
	}
}

void LitColumnsApp::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMatCB = mCurrFrameResource->MaterialCB.get();
	for (auto& e : mMaterials)
	{
		Material* currMat = e.second.get();
		if (currMat->NumFramesDirty <= 0)
			continue;

		MaterialConstants matConstants;
		matConstants.DiffuseAlbedo = currMat->DiffuseAlbedo;
		matConstants.FresnelR0 = currMat->FresnelR0;
		matConstants.Roughness = currMat->Roughness;
		Float4x4Transpose(&matConstants.MatTransform, currMat->MatTransform);

		currMatCB->CopyData(currMat->MatCBIndex, matConstants);

		currMat->NumFramesDirty--;
	}
}

void Float4x4Transpose(XMFLOAT4X4* dest, const XMMATRIX& src)
{
	XMStoreFloat4x4(dest, XMMatrixTranspose(src));
}

void Float4x4InvTranspose(XMFLOAT4X4* dest, const XMFLOAT4X4& src)
{
	XMStoreFloat4x4(dest, XMMatrixTranspose(XMMatrixInverse(nullptr, XMLoadFloat4x4(&src))));
}

void Float4x4InvTranspose(XMFLOAT4X4* dest, const XMMATRIX& src)
{
	XMStoreFloat4x4(dest, XMMatrixTranspose(XMMatrixInverse(nullptr, src)));
}

void LitColumnsApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);
	XMMATRIX viewProj = XMMatrixMultiply(view, proj);

	Float4x4Transpose(&mMainPassCB.View, view);
	Float4x4InvTranspose(&mMainPassCB.InvView, view);
	Float4x4Transpose(&mMainPassCB.Proj, proj);
	Float4x4InvTranspose(&mMainPassCB.InvProj, proj);
	Float4x4Transpose(&mMainPassCB.ViewProj, viewProj);
	Float4x4InvTranspose(&mMainPassCB.InvViewProj, viewProj);

	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.EyePosW = mEyePos;
	mMainPassCB.RenderTargetSize = XMFLOAT2(static_cast<float>(mClientWidth), static_cast<float>(mClientHeight));
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / static_cast<float>(mClientWidth), 1.0f / static_cast<float>(mClientHeight));
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
	mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[0].Strength = { 0.6f, 0.6f, 0.6f };
	mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
	mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	mMainPassCB.Lights[2].Strength = { 0.15f, 0.15f, 0.15f };

	//for (auto i : Range(0, 10))
	//{
	//	mMainPassCB.Lights[i].Strength = { 0.9f, 0.9f, 0.9f };
	//	mMainPassCB.Lights[i].Direction = { 0.0f, -1.0f, 0.0f };
	//	mMainPassCB.Lights[i].FalloffStart = 4.5f;
	//	mMainPassCB.Lights[i].FalloffEnd = 5.0f;
	//	mMainPassCB.Lights[i].SpotPower = 0.8f;
	//}

	//for (auto i : Range(0, 5))
	//{
	//	mMainPassCB.Lights[i * 2].Position = { -5.0f, 3.5f, -10.0f + i * 5.0f };
	//	mMainPassCB.Lights[i * 2 + 1].Position = { +5.0f, 3.5f, -10.0f + i * 5.0f };
	//}

	auto currCB = mCurrFrameResource->PassCB.get();
	currCB->CopyData(0, mMainPassCB);
}

void LitColumnsApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);
	UpdateCamera(gt);

	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();
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

void LitColumnsApp::DrawRenderItems(
	ID3D12GraphicsCommandList* cmdList,
	const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();

	for (auto e : ritems)
	{
		cmdList->IASetVertexBuffers(0, 1, &RvToLv(e->Geo->VertexBufferView()));
		cmdList->IASetIndexBuffer(&RvToLv(e->Geo->IndexBufferView()));
		cmdList->IASetPrimitiveTopology(e->PrimitiveType);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + (e->ObjCBIndex * objCBByteSize);
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + (e->Mat->MatCBIndex * matCBByteSize);

		cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);
		cmdList->SetGraphicsRootConstantBufferView(1, matCBAddress);
		
		cmdList->DrawIndexedInstanced(e->IndexCount, 1, e->StartIndexLocation, e->BaseVertexLocation, 0);
	}
}

void LitColumnsApp::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;
	ThrowIfFailed(cmdListAlloc->Reset());
	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mOpaquePSO.Get()));

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
	
	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

	DrawRenderItems(mCommandList.Get(), mOpaueRitem);

	mCommandList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(
		CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT)));

	ThrowIfFailed(mCommandList->Close());
	//이게 왜 배열로 바뀌는지 모르겠다.
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	mCurrFrameResource->Fence = ++mCurrentFence;

	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void LitColumnsApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}
void LitColumnsApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}
void LitColumnsApp::OnMouseMove(WPARAM btnState, int x, int y)
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
		LitColumnsApp theApp(hInstance);
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