﻿#include "DynamicCubeApp.h"
#include "../Common/d3dUtil.h"
#include "../Common/Util.h"
#include "../Common/UploadBuffer.h"
#include "../Common/GeometryGenerator.h"
#include "CubeRenderTarget.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;

DynamicCubeApp::DynamicCubeApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

DynamicCubeApp::~DynamicCubeApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool DynamicCubeApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	mCamera.SetPosition(0.0f, 2.0f, -15.0f);

	BuildCubeFaceCamera(0.0f, 2.0f, 0.0f);

	mDynamicCubeMap = std::make_unique<CubeRenderTarget>(md3dDevice.Get(), 
		CubeMapSize, CubeMapSize, DXGI_FORMAT_R8G8B8A8_UNORM);

	LoadTextures();
	BuildRootSignature();
	BuildDescriptorHeaps();
	BuildCubeDepthStencil();
	BuildShadersAndInputLayout();
	BuildShapeGeometry();
	BuildSkullGeometry();
	BuildMaterials();
	BuildRenderItems();
	BuildFrameResources();
	BuildPSOs();

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists2[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists2), cmdsLists2);

	FlushCommandQueue();

	return true;
}

void DynamicCubeApp::BuildCubeFaceCamera(float x, float y, float z)
{
	// Generate the cube map about the given position.
	XMFLOAT3 center(x, y, z);
	XMFLOAT3 worldUp(0.0f, 1.0f, 0.0f);

	// Look along each coordinate axis.
	XMFLOAT3 targets[6] =
	{
		XMFLOAT3(x + 1.0f, y, z), // +X
		XMFLOAT3(x - 1.0f, y, z), // -X
		XMFLOAT3(x, y + 1.0f, z), // +Y
		XMFLOAT3(x, y - 1.0f, z), // -Y
		XMFLOAT3(x, y, z + 1.0f), // +Z
		XMFLOAT3(x, y, z - 1.0f)  // -Z
	};

	// Use world up vector (0,1,0) for all directions except +Y/-Y.  In these cases, we
	// are looking down +Y or -Y, so we need a different "up" vector.
	XMFLOAT3 ups[6] =
	{
		XMFLOAT3(0.0f, 1.0f, 0.0f),  // +X
		XMFLOAT3(0.0f, 1.0f, 0.0f),  // -X
		XMFLOAT3(0.0f, 0.0f, -1.0f), // +Y
		XMFLOAT3(0.0f, 0.0f, +1.0f), // -Y
		XMFLOAT3(0.0f, 1.0f, 0.0f),	 // +Z
		XMFLOAT3(0.0f, 1.0f, 0.0f)	 // -Z
	};

	for (int i = 0; i < 6; ++i)
	{
		mCubeMapCamera[i].LookAt(center, targets[i], ups[i]);
		mCubeMapCamera[i].SetLens(0.5f * XM_PI, 1.0f, 0.1f, 1000.0f);
		mCubeMapCamera[i].UpdateViewMatrix();
	}
}

void DynamicCubeApp::CreateRtvAndDsvDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = SwapChainBufferCount + 6;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf())));

	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 2;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf())));

	mCubeDSV = CD3DX12_CPU_DESCRIPTOR_HANDLE(
		mDsvHeap->GetCPUDescriptorHandleForHeapStart(), 1, mDsvDescriptorSize);
}

void DynamicCubeApp::LoadTextures()
{
	std::vector<std::wstring> filenames 
	{ 
		L"bricks2.dds",
		L"tile.dds",
		L"white1x1.dds",
		L"grasscube1024.dds"
	};
	
	for_each(filenames.begin(), filenames.end(), [&](auto& curFilename) {
		auto tex = std::make_unique<Texture>();
		tex->Filename = L"../Textures/" + curFilename;
		ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(), mCommandList.Get(),
			tex->Filename.c_str(), tex->Resource, tex->UploadHeap));
		mTextures.emplace_back(std::move(tex));
		});
}

void DynamicCubeApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE cubeTexTable;
	cubeTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);

	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 1, 0);

	CD3DX12_ROOT_PARAMETER slotRootParameter[5];
	slotRootParameter[0].InitAsConstantBufferView(0);
	slotRootParameter[1].InitAsConstantBufferView(1);
	slotRootParameter[2].InitAsShaderResourceView(0, 1);
	slotRootParameter[3].InitAsDescriptorTable(1, &cubeTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[4].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);

	auto staticSamplers = d3dUtil::GetStaticSamplers();
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(_countof(slotRootParameter), slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(), 
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

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void DynamicCubeApp::BuildDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	heapDesc.NodeMask = 0;
	heapDesc.NumDescriptors = 6; //texture 3, cube 1, dynaimic cube 1
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	for_each(mTextures.begin(), mTextures.end(), [&, index{ 0 }](auto& curTex) mutable {
		auto& curTexResource = curTex->Resource;
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = curTexResource->GetDesc().Format;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MipLevels = curTexResource->GetDesc().MipLevels;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;

		if (curTex->Filename.find(L"cube") != std::wstring::npos)
		{
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			srvDesc.TextureCube.MostDetailedMip = 0;
			srvDesc.TextureCube.MipLevels = curTexResource->GetDesc().MipLevels;
			srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
			srvDesc.Format = curTexResource->GetDesc().Format;
			mSkyTexHeapIndex = index;
			mDynamicTexHeapIndex = mSkyTexHeapIndex + 1;
		}
		
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDesc{ mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart() };
		hCpuDesc.Offset(index++, mCbvSrvUavDescriptorSize);
		md3dDevice->CreateShaderResourceView(curTex->Resource.Get(), &srvDesc, hCpuDesc);
		});
	
	auto srvCpuStart = mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	auto srvGpuStart = mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	auto rtvCpuStart = mRtvHeap->GetCPUDescriptorHandleForHeapStart();

	int rtvOffset = SwapChainBufferCount;

	CD3DX12_CPU_DESCRIPTOR_HANDLE cubeRtvHandles[6];
	for (auto i : Range(0, (int)_countof(cubeRtvHandles)))
		cubeRtvHandles[i] = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvCpuStart, rtvOffset + i, mRtvDescriptorSize);

	mDynamicCubeMap->BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE(srvCpuStart, mDynamicTexHeapIndex, mCbvSrvUavDescriptorSize),
		CD3DX12_GPU_DESCRIPTOR_HANDLE(srvGpuStart, mDynamicTexHeapIndex, mCbvSrvUavDescriptorSize),
		cubeRtvHandles);
}

void DynamicCubeApp::BuildCubeDepthStencil()
{
	D3D12_RESOURCE_DESC depthStencilDesc;
	depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilDesc.Alignment = 0;
	depthStencilDesc.Width = CubeMapSize;
	depthStencilDesc.Height = CubeMapSize;
	depthStencilDesc.DepthOrArraySize = 1;
	depthStencilDesc.MipLevels = 1;
	depthStencilDesc.Format = mDepthStencilFormat;
	depthStencilDesc.SampleDesc.Count = 1;
	depthStencilDesc.SampleDesc.Quality = 0;
	depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE optClear;
	optClear.Format = mDepthStencilFormat;
	optClear.DepthStencil.Depth = 1.0f;
	optClear.DepthStencil.Stencil = 0;
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&RvToLv(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
		D3D12_HEAP_FLAG_NONE,
		&depthStencilDesc,
		D3D12_RESOURCE_STATE_COMMON,
		&optClear,
		IID_PPV_ARGS(mCubeDepthStencilBuffer.GetAddressOf())));

	md3dDevice->CreateDepthStencilView(mCubeDepthStencilBuffer.Get(), nullptr, mCubeDSV);

	mCommandList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(mCubeDepthStencilBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE)));
}

void DynamicCubeApp::BuildShadersAndInputLayout()
{
	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders/StandardVS.hlsl", nullptr, "main", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders/OpaquePS.hlsl", nullptr, "main", "ps_5_1");

	mShaders["skyVS"] = d3dUtil::CompileShader(L"Shaders/SkyVS.hlsl", nullptr, "main", "vs_5_1");
	mShaders["skyPS"] = d3dUtil::CompileShader(L"Shaders/SkyPS.hlsl", nullptr, "main", "ps_5_1");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

void DynamicCubeApp::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

	//
	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.
	//

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();

	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	//
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
		vertices[k].TexC = box.Vertices[i].TexC;
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
		vertices[k].TexC = grid.Vertices[i].TexC;
	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
		vertices[k].TexC = sphere.Vertices[i].TexC;
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
		vertices[k].TexC = cylinder.Vertices[i].TexC;
	}

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}

void DynamicCubeApp::BuildSkullGeometry()
{
	std::ifstream fin("Models/skull.txt");

	if (!fin)
	{
		MessageBox(0, L"Models/skull.txt not found.", 0, 0);
		return;
	}

	UINT vcount = 0;
	UINT tcount = 0;
	std::string ignore;

	fin >> ignore >> vcount;
	fin >> ignore >> tcount;
	fin >> ignore >> ignore >> ignore >> ignore;

	XMFLOAT3 vMinf3(+MathHelper::Infinity, +MathHelper::Infinity, +MathHelper::Infinity);
	XMFLOAT3 vMaxf3(-MathHelper::Infinity, -MathHelper::Infinity, -MathHelper::Infinity);

	XMVECTOR vMin = XMLoadFloat3(&vMinf3);
	XMVECTOR vMax = XMLoadFloat3(&vMaxf3);

	std::vector<Vertex> vertices(vcount);
	for (UINT i = 0; i < vcount; ++i)
	{
		fin >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z;
		fin >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z;

		vertices[i].TexC = { 0.0f, 0.0f };

		XMVECTOR P = XMLoadFloat3(&vertices[i].Pos);

		vMin = XMVectorMin(vMin, P);
		vMax = XMVectorMax(vMax, P);
	}

	BoundingBox bounds;
	XMStoreFloat3(&bounds.Center, 0.5f * (vMin + vMax));
	XMStoreFloat3(&bounds.Extents, 0.5f * (vMax - vMin));

	fin >> ignore;
	fin >> ignore;
	fin >> ignore;

	std::vector<std::int32_t> indices(3 * tcount);
	for (UINT i = 0; i < tcount; ++i)
	{
		fin >> indices[i * 3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
	}

	fin.close();

	//
	// Pack the indices of all the meshes into one index buffer.
	//

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::int32_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "skullGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R32_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;
	submesh.BBounds = bounds;

	geo->DrawArgs["skull"] = submesh;

	mGeometries[geo->Name] = std::move(geo);
}

void DynamicCubeApp::BuildMaterials()
{
	auto MakeMaterial = [&](std::string&& name, int matCBIdx, int diffuseSrvHeapIdx,
		XMFLOAT4 diffuseAlbedo, XMFLOAT3 fresnelR0, float rough) {
			auto curMat = std::make_unique<Material>();
			curMat->Name = name;
			curMat->MatCBIndex = matCBIdx;
			curMat->DiffuseSrvHeapIndex = diffuseSrvHeapIdx;
			curMat->DiffuseAlbedo = diffuseAlbedo;
			curMat->FresnelR0 = fresnelR0;
			curMat->Roughness = rough;
			mMaterials[name] = std::move(curMat);
		};

	MakeMaterial("bricks0", 0, 0, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.1f, 0.1f, 0.1f }, 0.3f);
	MakeMaterial("tile0", 1, 1, { 0.9f, 0.9f, 0.9f, 1.0f }, { 0.2f, 0.2f, 0.2f }, 0.1f);
	MakeMaterial("mirror0", 2, 2, { 0.0f, 0.0f, 0.1f, 1.0f }, { 0.98f, 0.97f, 0.95f }, 0.1f);
	MakeMaterial("sky", 3, 3, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.1f, 0.1f, 0.1f }, 1.0f);
	MakeMaterial("skullMat", 4, 2, { 0.8f, 0.8f, 0.8f, 1.0f }, { 0.2f, 0.2f, 0.2f }, 0.2f);
}

void DynamicCubeApp::BuildRenderItems()
{
	auto MakeRenderItem = [&, objIdx{ 0 }](std::string&& geoName, std::string&& smName, std::string&& matName,
		const XMMATRIX& world, const XMMATRIX& texTransform, RenderLayer renderLayer, bool visible = true, 
		RenderItem** outRenderItem = nullptr) mutable {
		auto renderItem = std::make_unique<RenderItem>();
		auto& sm = mGeometries[geoName]->DrawArgs[smName];
		renderItem->Geo = mGeometries[geoName].get();
		renderItem->StartIndexLocation = sm.StartIndexLocation;
		renderItem->BaseVertexLocation = sm.BaseVertexLocation;
		renderItem->IndexCount = sm.IndexCount;
		renderItem->Mat = mMaterials[matName].get();
		renderItem->ObjCBIndex = objIdx++;
		renderItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		XMStoreFloat4x4(&renderItem->World, world);
		XMStoreFloat4x4(&renderItem->TexTransform, texTransform);
		renderItem->Visible = visible;

		if (outRenderItem != nullptr) (*outRenderItem) = renderItem.get();
		mRitemLayer[renderLayer].emplace_back(renderItem.get());
		mAllRitems.emplace_back(std::move(renderItem));
	};

	MakeRenderItem("shapeGeo", "sphere", "sky", XMMatrixScaling(5000.0f, 5000.0f, 5000.0f), XMMatrixIdentity(), RenderLayer::Sky);
	MakeRenderItem("skullGeo", "skull", "skullMat", XMMatrixIdentity(), XMMatrixIdentity(), RenderLayer::Opaque, true, &mSkullRitem);
	MakeRenderItem("shapeGeo", "box", "bricks0", XMMatrixScaling(2.0f, 1.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f),
		XMMatrixScaling(1.0f, 1.0f, 1.0f), RenderLayer::Opaque);
	MakeRenderItem("shapeGeo", "sphere", "mirror0", XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 2.0f, 0.0f),
		XMMatrixScaling(1.0f, 1.0f, 1.0f), RenderLayer::OpaqueDynamicReflectors);
	MakeRenderItem("shapeGeo", "grid", "tile0", XMMatrixIdentity(), XMMatrixScaling(8.0f, 8.0f, 1.0f), RenderLayer::Opaque);

	XMMATRIX brickTexTransform = XMMatrixScaling(1.5f, 2.0f, 1.0f);
	for (auto i : Range(0, 5))
	{
		XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
		XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f);

		XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);
		XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f);

		MakeRenderItem("shapeGeo", "cylinder", "bricks0", leftCylWorld, brickTexTransform, RenderLayer::Opaque);
		MakeRenderItem("shapeGeo", "cylinder", "bricks0", rightCylWorld, brickTexTransform, RenderLayer::Opaque);

		MakeRenderItem("shapeGeo", "sphere", "mirror0", leftSphereWorld, XMMatrixIdentity(), RenderLayer::Opaque);
		MakeRenderItem("shapeGeo", "sphere", "mirror0", rightSphereWorld, XMMatrixIdentity(), RenderLayer::Opaque);
	}
}

void DynamicCubeApp::BuildFrameResources()
{
	for (auto i : Range(0, gNumFrameResources))
	{
		auto frameRes = std::make_unique<FrameResource>(md3dDevice.Get(), 7,
			static_cast<UINT>(mAllRitems.size()), static_cast<UINT>(mMaterials.size()));
		mFrameResources.emplace_back(std::move(frameRes));
	}
}

D3D12_SHADER_BYTECODE GetShaderBytecode(
	std::unordered_map<std::string, ComPtr<ID3DBlob>>& shaders, std::string&& name)
{
	return { shaders[name]->GetBufferPointer(), shaders[name]->GetBufferSize() };
}

void DynamicCubeApp::MakeOpaqueDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc)
{
	inoutDesc->VS = GetShaderBytecode(mShaders, "standardVS");
	inoutDesc->PS = GetShaderBytecode(mShaders, "opaquePS");
	inoutDesc->NodeMask = 0;
	inoutDesc->SampleMask = UINT_MAX;
	inoutDesc->NumRenderTargets = 1;
	inoutDesc->InputLayout = { mInputLayout.data(), static_cast<UINT>(mInputLayout.size()) };
	inoutDesc->pRootSignature = mRootSignature.Get();
	inoutDesc->BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	inoutDesc->DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	inoutDesc->RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	inoutDesc->RTVFormats[0] = mBackBufferFormat;
	inoutDesc->DSVFormat = mDepthStencilFormat;
	inoutDesc->PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	inoutDesc->SampleDesc.Count = m4xMsaaState ? 4 : 1;
	inoutDesc->SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
}

void DynamicCubeApp::MakeSkyDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc)
{
	inoutDesc->RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	inoutDesc->DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	inoutDesc->pRootSignature = mRootSignature.Get();
	inoutDesc->VS = GetShaderBytecode(mShaders, "skyVS");
	inoutDesc->PS = GetShaderBytecode(mShaders, "skyPS");
}

void DynamicCubeApp::MakePSOPipelineState(GraphicsPSO psoType)
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
	MakeOpaqueDesc(&psoDesc);

	switch (psoType)
	{
	case GraphicsPSO::Opaque:		break;
	case GraphicsPSO::Sky:	MakeSkyDesc(&psoDesc);		break;
	default: assert(!"wrong type");
	}

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSOs[psoType])));
}

void DynamicCubeApp::BuildPSOs()
{
	for (auto gPso : GraphicsPSO_ALL)
		MakePSOPipelineState(gPso);
}

void DynamicCubeApp::OnResize()
{
	D3DApp::OnResize();

	mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.f);
}

void DynamicCubeApp::OnKeyboardInput(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();

	float speed = 10.0f;
	float walkSpeed = 0.0f;
	float strafeSpeed = 0.0f;

	std::vector<int> keyList{ 'W', 'S', 'D', 'A' };
	for_each(keyList.begin(), keyList.end(), [&](int vKey) {
		bool bPressed = GetAsyncKeyState(vKey) & 0x8000;
		if (bPressed)
		{
			switch (vKey)
			{
			case 'W':		walkSpeed += speed;		break;
			case 'S':		walkSpeed += -speed;		break;
			case 'D':		strafeSpeed += speed;		break;
			case 'A':		strafeSpeed += -speed;		break;
			}
		}});
	
	mCamera.Move(Camera::eWalk, walkSpeed * dt);
	mCamera.Move(Camera::eStrafe, strafeSpeed * dt);

	mCamera.UpdateViewMatrix();
}

void DynamicCubeApp::AnimateMaterials(const GameTimer& gt)
{
}

void StoreMatrix4x4(XMFLOAT4X4& dest, XMFLOAT4X4& src) { XMStoreFloat4x4(&dest, XMMatrixTranspose(XMLoadFloat4x4(&src))); }
void StoreMatrix4x4(XMFLOAT4X4& dest, XMMATRIX src) { XMStoreFloat4x4(&dest, XMMatrixTranspose(src)); }
XMMATRIX Multiply(XMFLOAT4X4& m1, XMFLOAT4X4 m2) { return XMMatrixMultiply(XMLoadFloat4x4(&m1), XMLoadFloat4x4(&m2)); }
XMMATRIX Inverse(XMMATRIX& m) { return XMMatrixInverse(nullptr, m); }
XMMATRIX Inverse(XMFLOAT4X4& src) {	return Inverse(RvToLv(XMLoadFloat4x4(&src))); }

void DynamicCubeApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurFrameRes->ObjectCB.get();
	for (auto& e : mAllRitems)
	{
		if (e->NumFramesDirty <= 0)
			continue;

		ObjectConstants objConstants;
		StoreMatrix4x4(objConstants.World, e->World);
		StoreMatrix4x4(objConstants.TexTransform, e->TexTransform);
		objConstants.MaterialIndex = e->Mat->MatCBIndex;

		currObjectCB->CopyData(e->ObjCBIndex, objConstants);

		e->NumFramesDirty--;
	}
}

void DynamicCubeApp::UpdateMaterialBuffer(const GameTimer& gt)
{
	auto currMaterialBuffer = mCurFrameRes->MaterialBuffer.get();
	for (auto& e : mMaterials)
	{
		Material* mat = e.second.get();
		if (mat->NumFramesDirty <= 0)
			continue;

		MaterialData matData;
		matData.DiffuseAlbedo = mat->DiffuseAlbedo;
		matData.FresnelR0 = mat->FresnelR0;
		matData.Roughness = mat->Roughness;
		matData.DiffuseMapIndex = mat->DiffuseSrvHeapIndex;
		StoreMatrix4x4(matData.MatTransform, mat->MatTransform);

		currMaterialBuffer->CopyData(mat->MatCBIndex, matData);

		mat->NumFramesDirty--;
	}
}

void DynamicCubeApp::UpdateCubeMapFacePassCBs()
{
	for (auto i : Range(0, 6))
	{
		PassConstants cubeFacePassCB = mMainPassCB;

		XMMATRIX view = mCubeMapCamera[i].GetView();
		XMMATRIX proj = mCubeMapCamera[i].GetProj();
		XMMATRIX viewProj = XMMatrixMultiply(view, proj);

		StoreMatrix4x4(mMainPassCB.View, view);
		StoreMatrix4x4(mMainPassCB.InvView, Inverse(view));
		StoreMatrix4x4(mMainPassCB.Proj, proj);
		StoreMatrix4x4(mMainPassCB.InvProj, Inverse(proj));
		StoreMatrix4x4(mMainPassCB.ViewProj, viewProj);
		StoreMatrix4x4(mMainPassCB.InvViewProj, Inverse(viewProj));
		cubeFacePassCB.EyePosW = mCubeMapCamera[i].GetPosition3f();
		cubeFacePassCB.RenderTargetSize = XMFLOAT2((float)CubeMapSize, (float)CubeMapSize);
		cubeFacePassCB.InvRenderTargetSize = XMFLOAT2(1.0f / CubeMapSize, 1.0f / CubeMapSize);

		auto currPassCB = mCurFrameRes->PassCB.get();

		currPassCB->CopyData(1 + i, cubeFacePassCB);
	}
}

void DynamicCubeApp::UpdateMainPassCB(const GameTimer& gt)
{
	auto& passCB = mCurFrameRes->PassCB;

	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();
	XMMATRIX viewProj = XMMatrixMultiply(view, proj);

	StoreMatrix4x4(mMainPassCB.View, view);
	StoreMatrix4x4(mMainPassCB.InvView, Inverse(view));
	StoreMatrix4x4(mMainPassCB.Proj, proj);
	StoreMatrix4x4(mMainPassCB.InvProj, Inverse(proj));
	StoreMatrix4x4(mMainPassCB.ViewProj, viewProj);
	StoreMatrix4x4(mMainPassCB.InvViewProj, Inverse(viewProj));
	mMainPassCB.EyePosW = mCamera.GetPosition3f();
	mMainPassCB.RenderTargetSize = { (float)mClientWidth, (float)mClientHeight };
	mMainPassCB.InvRenderTargetSize = { 1.0f / (float)mClientWidth, 1.0f / (float)mClientHeight };
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
	mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[0].Strength = { 0.8f, 0.8f, 0.8f };
	mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[1].Strength = { 0.4f, 0.4f, 0.4f };
	mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	mMainPassCB.Lights[2].Strength = { 0.2f, 0.2f, 0.2f };

	passCB->CopyData(0, mMainPassCB);

	UpdateCubeMapFacePassCBs();
}

void DynamicCubeApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);

	XMMATRIX skullScale = XMMatrixScaling(0.2f, 0.2f, 0.2f);
	XMMATRIX skullOffset = XMMatrixTranslation(3.0f, 2.0f, 0.0f);
	XMMATRIX skullLocalRotate = XMMatrixRotationY(2.0f * gt.TotalTime());
	XMMATRIX skullGlobalRotate = XMMatrixRotationY(0.5f * gt.TotalTime());
	XMStoreFloat4x4(&mSkullRitem->World, skullScale * skullLocalRotate * skullOffset * skullGlobalRotate);
	mSkullRitem->NumFramesDirty = gNumFrameResources;

	mFrameResIdx = (mFrameResIdx + 1) % gNumFrameResources;
	mCurFrameRes = mFrameResources[mFrameResIdx].get();
	if (mCurFrameRes->Fence != 0 && mFence->GetCompletedValue() < mCurFrameRes->Fence)
	{
		HANDLE hEvent = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurFrameRes->Fence, hEvent));
		WaitForSingleObject(hEvent, INFINITE);
		CloseHandle(hEvent);
	}
	
	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialBuffer(gt);
	UpdateMainPassCB(gt);
}

void DynamicCubeApp::DrawRenderItems(
	ID3D12GraphicsCommandList* cmdList,
	const std::vector<RenderItem*> ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	auto objectCB = mCurFrameRes->ObjectCB->Resource();

	for (auto i : Range(0, (int)ritems.size()))
	{
		auto ri = ritems[i];

		cmdList->IASetVertexBuffers(0, 1, &RvToLv(ri->Geo->VertexBufferView()));
		cmdList->IASetIndexBuffer(&RvToLv(ri->Geo->IndexBufferView()));
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
		
		cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

void DynamicCubeApp::DrawSceneToCubeMap()
{
	mCommandList->RSSetViewports(1, &RvToLv(mDynamicCubeMap->Viewport()));
	mCommandList->RSSetScissorRects(1, &RvToLv(mDynamicCubeMap->ScissorRect()));

	mCommandList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(mDynamicCubeMap->Resource(),
		D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET)));

	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

	for (auto i : Range(0, 6))
	{
		mCommandList->ClearRenderTargetView(mDynamicCubeMap->Rtv(i), Colors::LightSteelBlue, 0, nullptr);
		mCommandList->ClearDepthStencilView(mCubeDSV, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

		mCommandList->OMSetRenderTargets(1, &RvToLv(mDynamicCubeMap->Rtv(i)), true, &mCubeDSV);

		auto passCB = mCurFrameRes->PassCB->Resource();
		D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = passCB->GetGPUVirtualAddress() + (1 + i) * passCBByteSize;
		mCommandList->SetGraphicsRootConstantBufferView(1, passCBAddress);

		DrawRenderItems(mCommandList.Get(), mRitemLayer[RenderLayer::Opaque]);

		mCommandList->SetPipelineState(mPSOs[GraphicsPSO::Sky].Get());
		DrawRenderItems(mCommandList.Get(), mRitemLayer[RenderLayer::Sky]);

		mCommandList->SetPipelineState(mPSOs[GraphicsPSO::Opaque].Get());
	}
	mCommandList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(mDynamicCubeMap->Resource(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ)));
}

void DynamicCubeApp::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurFrameRes->CmdListAlloc;
	ThrowIfFailed(cmdListAlloc->Reset());

	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs[GraphicsPSO::Opaque].Get()));

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	auto matBuffer = mCurFrameRes->MaterialBuffer->Resource();
	mCommandList->SetGraphicsRootShaderResourceView(2, matBuffer->GetGPUVirtualAddress());

	CD3DX12_GPU_DESCRIPTOR_HANDLE skyTexDescriptor(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	skyTexDescriptor.Offset(mSkyTexHeapIndex, mCbvSrvUavDescriptorSize);
	mCommandList->SetGraphicsRootDescriptorTable(3, skyTexDescriptor);

	mCommandList->SetGraphicsRootDescriptorTable(4, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	DrawSceneToCubeMap();

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	mCommandList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET)));

	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	mCommandList->OMSetRenderTargets(1, &RvToLv(CurrentBackBufferView()), true, &RvToLv(DepthStencilView()));

	auto passCB = mCurFrameRes->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

	CD3DX12_GPU_DESCRIPTOR_HANDLE dynamicTexDescriptor(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	dynamicTexDescriptor.Offset(mSkyTexHeapIndex + 1, mCbvSrvUavDescriptorSize);
	mCommandList->SetGraphicsRootDescriptorTable(3, dynamicTexDescriptor);
	DrawRenderItems(mCommandList.Get(), mRitemLayer[RenderLayer::OpaqueDynamicReflectors]);

	mCommandList->SetGraphicsRootDescriptorTable(3, skyTexDescriptor);
	DrawRenderItems(mCommandList.Get(), mRitemLayer[RenderLayer::Opaque]);

	mCommandList->SetPipelineState(mPSOs[GraphicsPSO::Sky].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[RenderLayer::Sky]);

	mCommandList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT)));

	ThrowIfFailed(mCommandList->Close());

	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	mCurFrameRes->Fence = ++mCurrentFence;

	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void DynamicCubeApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		mLastMousePos.x = x;
		mLastMousePos.y = y;

		SetCapture(mhMainWnd);
	}
}
void DynamicCubeApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}
void DynamicCubeApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		mCamera.Move(Camera::ePitch, dy);
		mCamera.Move(Camera::eRotateY, dx);
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
		DynamicCubeApp theApp(hInstance);
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