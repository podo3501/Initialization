#include "SkinnedMeshApp.h"
#include "../Common/d3dUtil.h"
#include "../Common/Util.h"
#include "../Common/UploadBuffer.h"
#include "../Common/GeometryGenerator.h"
#include "ShadowMap.h"
#include "Ssao.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;

SkinnedMeshApp::SkinnedMeshApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
	mSceneBounds.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
	mSceneBounds.Radius = sqrtf(10.0f * 10.0f + 15.0f * 15.0f);
}

SkinnedMeshApp::~SkinnedMeshApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool SkinnedMeshApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	mCamera.SetPosition(0.0f, 2.0f, -15.0f);

	mShadowMap = std::make_unique<ShadowMap>(md3dDevice.Get(), 2048, 2048);

	mSsao = std::make_unique<Ssao>(
		md3dDevice.Get(),
		mCommandList.Get(),
		mClientWidth, mClientHeight);

	LoadSkinnedModel();
	LoadTextures();
	BuildRootSignature();
	BuildSsaoRootSignature();
	BuildDescriptorHeaps();
	BuildShadersAndInputLayout();
	BuildShapeGeometry();
	BuildMaterials();
	BuildRenderItems();
	BuildFrameResources();
	BuildPSOs();

	mSsao->SetPSOs(mPSOs[GraphicsPSO::Ssao].Get(), mPSOs[GraphicsPSO::SsaoBlur].Get());

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists2[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists2), cmdsLists2);

	FlushCommandQueue();

	return true;
}

void SkinnedMeshApp::CreateRtvAndDsvDescriptorHeaps()
{
	// Add +1 for screen normal map, +2 for ambient maps.
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
	rtvHeapDesc.NumDescriptors = SwapChainBufferCount + 3;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
		&rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf())));

	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
	dsvHeapDesc.NumDescriptors = 2;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
		&dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf())));
}

void SkinnedMeshApp::LoadTextures()
{
	std::vector<std::wstring> filenames 
	{ 
		L"bricks2.dds",
		L"bricks2_nmap.dds",
		L"tile.dds",
		L"tile_nmap.dds",
		L"white1x1.dds",
		L"default_nmap.dds"
	};

	for_each(mSkinnedMats.begin(), mSkinnedMats.end(), [&](auto& mat) {
		std::string diffuseName = mat.DiffuseMapName;
		std::string normalName = mat.NormalMapName;

		std::wstring diffuseFilename = /*L"../Textures/" +*/ AnsiToWString(diffuseName);
		std::wstring normalFilename = /*L"../Textures/" +*/ AnsiToWString(normalName);

		diffuseName = diffuseName.substr(0, diffuseName.find_last_of("."));
		normalName = normalName.substr(0, normalName.find_last_of("."));

		mSkinnedTextureNames.push_back(diffuseName);
		filenames.emplace_back(diffuseFilename);

		mSkinnedTextureNames.push_back(normalName);
		filenames.emplace_back(normalFilename);
		});

	filenames.emplace_back(L"desertcube1024.dds");
	
	for_each(filenames.begin(), filenames.end(), [&](auto& curFilename) {
		auto tex = std::make_unique<Texture>();
		tex->Filename = L"../Textures/" + curFilename;
		ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(), mCommandList.Get(),
			tex->Filename.c_str(), tex->Resource, tex->UploadHeap));
		mTextures.emplace_back(std::move(tex));
		});
}

D3D12_STATIC_SAMPLER_DESC ShadowSampler()
{
	return CD3DX12_STATIC_SAMPLER_DESC(
		6, // shaderRegister
		D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressW
		0.0f,                               // mipLODBias
		16,                                 // maxAnisotropy
		D3D12_COMPARISON_FUNC_LESS_EQUAL,
		D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK);
}

void SkinnedMeshApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable0;
	texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0);

	CD3DX12_DESCRIPTOR_RANGE texTable1;
	texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 48, 3, 0);

	CD3DX12_ROOT_PARAMETER slotRootParameter[6];
	slotRootParameter[0].InitAsConstantBufferView(0);
	slotRootParameter[1].InitAsConstantBufferView(1);
	slotRootParameter[2].InitAsConstantBufferView(2);
	slotRootParameter[3].InitAsShaderResourceView(0, 1);
	slotRootParameter[4].InitAsDescriptorTable(1, &texTable0, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[5].InitAsDescriptorTable(1, &texTable1, D3D12_SHADER_VISIBILITY_PIXEL);

	auto staticSamplers = d3dUtil::GetStaticSamplers();
	staticSamplers.emplace_back(ShadowSampler());

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc{
		_countof(slotRootParameter), slotRootParameter,
		static_cast<UINT>(staticSamplers.size()), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT };

	ComPtr<ID3DBlob> serialized = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serialized.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr) ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	ThrowIfFailed(hr);
	
	ThrowIfFailed(md3dDevice->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), 
		IID_PPV_ARGS(&mRootSignature)));
}

void SkinnedMeshApp::BuildSsaoRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable0{};
	texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);

	CD3DX12_DESCRIPTOR_RANGE texTable1{};
	texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);

	CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	slotRootParameter[0].InitAsConstantBufferView(0);
	slotRootParameter[1].InitAsConstants(1, 1);
	slotRootParameter[2].InitAsDescriptorTable(1, &texTable0, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[3].InitAsDescriptorTable(1, &texTable1, D3D12_SHADER_VISIBILITY_PIXEL);

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC depthMapSam(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressW
		0.0f,
		0,
		D3D12_COMPARISON_FUNC_LESS_EQUAL,
		D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE);

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	std::array<CD3DX12_STATIC_SAMPLER_DESC, 4> staticSamplers =
	{
		pointClamp, linearClamp, depthMapSam, linearWrap
	};

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
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
		IID_PPV_ARGS(mSsaoRootSignature.GetAddressOf())));
}

CD3DX12_CPU_DESCRIPTOR_HANDLE SkinnedMeshApp::GetCpuSrv(int index) const
{
	auto srv = CD3DX12_CPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	srv.Offset(index, mCbvSrvUavDescriptorSize);
	return srv;
}

CD3DX12_GPU_DESCRIPTOR_HANDLE SkinnedMeshApp::GetGpuSrv(int index) const
{
	auto srv = CD3DX12_GPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	srv.Offset(index, mCbvSrvUavDescriptorSize);
	return srv;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE SkinnedMeshApp::GetDsv(int index) const
{
	auto dsv = CD3DX12_CPU_DESCRIPTOR_HANDLE(mDsvHeap->GetCPUDescriptorHandleForHeapStart());
	dsv.Offset(index, mDsvDescriptorSize);
	return dsv;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE SkinnedMeshApp::GetRtv(int index) const
{
	auto rtv = CD3DX12_CPU_DESCRIPTOR_HANDLE(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
	rtv.Offset(index, mRtvDescriptorSize);
	return rtv;
}

void SkinnedMeshApp::BuildDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	srvHeapDesc.NodeMask = 0;
	srvHeapDesc.NumDescriptors = 64;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	UINT index = 0;
	for_each(mTextures.begin(), mTextures.end(), [&](auto& curTex) {
		auto& curTexResource = curTex->Resource;	
		srvDesc.Format = curTexResource->GetDesc().Format;
		srvDesc.Texture2D.MipLevels = curTexResource->GetDesc().MipLevels;

		if (curTex->Filename.find(L"cube") != std::wstring::npos)
		{
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			srvDesc.TextureCube.MostDetailedMip = 0;
			srvDesc.TextureCube.MipLevels = curTexResource->GetDesc().MipLevels;
			srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
			srvDesc.Format = curTexResource->GetDesc().Format;
			mSkyTexHeapIndex = index;
		}
		
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDesc{ mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart() };
		hCpuDesc.Offset(index++, mCbvSrvUavDescriptorSize);
		md3dDevice->CreateShaderResourceView(curTex->Resource.Get(), &srvDesc, hCpuDesc);
		});

	mSkinnedSrvHeapStart = 6;
	UINT ssaoHeapIndexStart{ 0 };
	UINT ssaoAmbientMapIndex{ 0 };
	UINT nullTexSrvIndex1{ 0 };
	UINT nullTexSrvIndex2{ 0 };

	mShadowMapHeapIndex = mSkyTexHeapIndex + 1;
	ssaoHeapIndexStart = mShadowMapHeapIndex + 1;
	ssaoAmbientMapIndex = ssaoHeapIndexStart + 3;
	mNullCubeSrvIndex = ssaoHeapIndexStart + 5;
	nullTexSrvIndex1 = mNullCubeSrvIndex + 1;
	nullTexSrvIndex2 = nullTexSrvIndex1 + 1;

	auto nullSrv = GetCpuSrv(mNullCubeSrvIndex);
	mNullSrv = GetGpuSrv(mNullCubeSrvIndex);

	md3dDevice->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);
	nullSrv.Offset(1, mCbvSrvUavDescriptorSize);

	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	md3dDevice->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);

	nullSrv.Offset(1, mCbvSrvUavDescriptorSize);
	md3dDevice->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);

	mShadowMap->BuildDescriptors(
		GetCpuSrv(mShadowMapHeapIndex),
		GetGpuSrv(mShadowMapHeapIndex),
		GetDsv(1));

	mSsao->BuildDescriptors(
		mDepthStencilBuffer.Get(),
		GetCpuSrv(ssaoHeapIndexStart),
		GetGpuSrv(ssaoHeapIndexStart),
		GetRtv(SwapChainBufferCount),
		mCbvSrvUavDescriptorSize,
		mRtvDescriptorSize);
}

void SkinnedMeshApp::BuildShadersAndInputLayout()
{
	const D3D_SHADER_MACRO alphaTestDefines[] = { "ALPHA_TEST", "1", NULL, NULL };
	const D3D_SHADER_MACRO skinnedDefines[] = { "SKINNED", "1", NULL, NULL };

	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders/StandardVS.hlsl", nullptr, "main", "vs_5_1");
	mShaders["skinnedVS"] = d3dUtil::CompileShader(L"Shaders/StandardVS.hlsl", skinnedDefines, "main", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders/OpaquePS.hlsl", nullptr, "main", "ps_5_1");

	mShaders["shadowVS"] = d3dUtil::CompileShader(L"Shaders/ShadowsVS.hlsl", nullptr, "main", "vs_5_1");
	mShaders["skinnedShadowVS"] = d3dUtil::CompileShader(L"Shaders/ShadowsVS.hlsl", skinnedDefines, "main", "vs_5_1");
	mShaders["shadowOpaquePS"] = d3dUtil::CompileShader(L"Shaders/ShadowsPS.hlsl", nullptr, "main", "ps_5_1");
	mShaders["ShadowAlphaTestedPS"] = d3dUtil::CompileShader(L"Shaders/ShadowsPS.hlsl", alphaTestDefines,
		"main", "ps_5_1");

	mShaders["debugVS"] = d3dUtil::CompileShader(L"Shaders/ShadowDebugVS.hlsl", nullptr, "main", "vs_5_1");
	mShaders["debugPS"] = d3dUtil::CompileShader(L"Shaders/ShadowDebugPS.hlsl", nullptr, "main", "ps_5_1");

	mShaders["drawNormalsVS"] = d3dUtil::CompileShader(L"Shaders/DrawNormalsVS.hlsl", nullptr, "main", "vs_5_1");
	mShaders["skinnedDrawNormalsVS"] = d3dUtil::CompileShader(L"Shaders/DrawNormalsVS.hlsl", skinnedDefines, "main", "vs_5_1");
	mShaders["drawNormalsPS"] = d3dUtil::CompileShader(L"Shaders/DrawNormalsPS.hlsl", nullptr, "main", "ps_5_1");

	mShaders["ssaoVS"] = d3dUtil::CompileShader(L"Shaders/SsaoVS.hlsl", nullptr, "main", "vs_5_1");
	mShaders["ssaoPS"] = d3dUtil::CompileShader(L"Shaders/SsaoPS.hlsl", nullptr, "main", "ps_5_1");

	mShaders["ssaoBlurVS"] = d3dUtil::CompileShader(L"Shaders/SsaoBlurVS.hlsl", nullptr, "main", "vs_5_1");
	mShaders["ssaoBlurPS"] = d3dUtil::CompileShader(L"Shaders/SsaoBlurPS.hlsl", nullptr, "main", "ps_5_1");

	mShaders["skyVS"] = d3dUtil::CompileShader(L"Shaders/SkyVS.hlsl", nullptr, "main", "vs_5_1");
	mShaders["skyPS"] = d3dUtil::CompileShader(L"Shaders/SkyPS.hlsl", nullptr, "main", "ps_5_1");

	mInputLayout =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TANGENT", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	};

	mSkinnedInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "WEIGHTS", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "BONEINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 56, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
}

void SkinnedMeshApp::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);
	GeometryGenerator::MeshData quad = geoGen.CreateQuad(0.0f, 0.0f, 1.0f, 1.0f, 0.0f);

	//
	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.
	//

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();
	UINT quadVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();

	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
	UINT quadIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();

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

	SubmeshGeometry quadSubmesh;
	quadSubmesh.IndexCount = (UINT)quad.Indices32.size();
	quadSubmesh.StartIndexLocation = quadIndexOffset;
	quadSubmesh.BaseVertexLocation = quadVertexOffset;

	//
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size() +
		quad.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
		vertices[k].TexC = box.Vertices[i].TexC;
		vertices[k].TangentU = box.Vertices[i].TangentU;
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
		vertices[k].TexC = grid.Vertices[i].TexC;
		vertices[k].TangentU = grid.Vertices[i].TangentU;
	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
		vertices[k].TexC = sphere.Vertices[i].TexC;
		vertices[k].TangentU = sphere.Vertices[i].TangentU;
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
		vertices[k].TexC = cylinder.Vertices[i].TexC;
		vertices[k].TangentU = cylinder.Vertices[i].TangentU;
	}

	for (int i = 0; i < quad.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = quad.Vertices[i].Position;
		vertices[k].Normal = quad.Vertices[i].Normal;
		vertices[k].TexC = quad.Vertices[i].TexC;
		vertices[k].TangentU = quad.Vertices[i].TangentU;
	}

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));
	indices.insert(indices.end(), std::begin(quad.GetIndices16()), std::end(quad.GetIndices16()));

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
	geo->DrawArgs["quad"] = quadSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}


void SkinnedMeshApp::LoadSkinnedModel()
{
	std::vector<M3DLoader::SkinnedVertex> vertices;
	std::vector<std::uint16_t> indices;

	M3DLoader m3dLoader;
	m3dLoader.LoadM3d(mSkinnedModelFilename, vertices, indices,
		mSkinnedSubsets, mSkinnedMats, mSkinnedInfo);

	mSkinnedModelInst = std::make_unique<SkinnedModelInstance>();
	mSkinnedModelInst->SkinnedInfo = &mSkinnedInfo;
	mSkinnedModelInst->FinalTransforms.resize(mSkinnedInfo.BoneCount());
	mSkinnedModelInst->ClipName = "Take1";
	mSkinnedModelInst->TimePos = 0.0f;

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(SkinnedVertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = mSkinnedModelFilename;

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(SkinnedVertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	for (UINT i = 0; i < (UINT)mSkinnedSubsets.size(); ++i)
	{
		SubmeshGeometry submesh;
		std::string name = "sm_" + std::to_string(i);

		submesh.IndexCount = (UINT)mSkinnedSubsets[i].FaceCount * 3;
		submesh.StartIndexLocation = mSkinnedSubsets[i].FaceStart * 3;
		submesh.BaseVertexLocation = 0;

		geo->DrawArgs[name] = submesh;
	}

	mGeometries[geo->Name] = std::move(geo);
}

void SkinnedMeshApp::BuildMaterials()
{
	auto MakeMaterial = [&](std::string name, int matCBIdx, int diffuseSrvHeapIdx, int normalSrvHeapIdx,
		XMFLOAT4 diffuseAlbedo, XMFLOAT3 fresnelR0, float rough) {
			auto curMat = std::make_unique<Material>();
			curMat->Name = name;
			curMat->MatCBIndex = matCBIdx;
			curMat->DiffuseSrvHeapIndex = diffuseSrvHeapIdx;
			curMat->NormalSrvHeapIndex = normalSrvHeapIdx;
			curMat->DiffuseAlbedo = diffuseAlbedo;
			curMat->FresnelR0 = fresnelR0;
			curMat->Roughness = rough;
			mMaterials[name] = std::move(curMat);
		};

	MakeMaterial("bricks0", 0, 0, 1, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.1f, 0.1f, 0.1f }, 0.3f);
	MakeMaterial("tile0", 1, 2, 3, { 0.9f, 0.9f, 0.9f, 1.0f }, { 0.2f, 0.2f, 0.2f }, 0.1f);
	MakeMaterial("mirror0", 2, 4, 5, { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.98f, 0.97f, 0.95f }, 0.1f);
	MakeMaterial("sky", 3, 6, 7, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.1f, 0.1f, 0.1f }, 1.0f);

	UINT matCBIndex = static_cast<UINT>(mMaterials.size());
	UINT srvHeapIndex = mSkinnedSrvHeapStart;
	for_each(mSkinnedMats.begin(), mSkinnedMats.end(), [&](auto& skinMat) {
		MakeMaterial(skinMat.Name, matCBIndex++, srvHeapIndex++, srvHeapIndex++,
			skinMat.DiffuseAlbedo, skinMat.FresnelR0, skinMat.Roughness);
		});
}

void SkinnedMeshApp::BuildRenderItems()
{
	auto MakeRenderItem = [&, objIdx{ 0 }](std::string geoName, std::string smName, std::string matName,
		const XMMATRIX& world, const XMMATRIX& texTransform, RenderLayer renderLayer, bool visible = true, 
		UINT skinnedCBIdx = -1, SkinnedModelInstance* skinnedModelInst = nullptr ) mutable {
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
		renderItem->SkinnedCBIndex = skinnedCBIdx;
		renderItem->SkinnedModelInst = skinnedModelInst;
		mRitemLayer[renderLayer].emplace_back(renderItem.get());
		mAllRitems.emplace_back(std::move(renderItem));
	};

	MakeRenderItem("shapeGeo", "sphere", "sky", XMMatrixScaling(5000.0f, 5000.0f, 5000.0f), XMMatrixIdentity(), RenderLayer::Sky);
	MakeRenderItem("shapeGeo", "quad", "bricks0", XMMatrixIdentity(), XMMatrixIdentity(), RenderLayer::Debug);
	MakeRenderItem("shapeGeo", "box", "bricks0", XMMatrixScaling(2.0f, 1.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f),
		XMMatrixScaling(1.0f, 1.0f, 1.0f), RenderLayer::Opaque);
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

	for(auto i : Range(0, static_cast<UINT>(mSkinnedMats.size())))
	{
		std::string submeshName = "sm_" + std::to_string(i);
		
		auto ritem = std::make_unique<RenderItem>();

		XMMATRIX modelScale = XMMatrixScaling(0.05f, 0.05f, -0.05f);
		XMMATRIX modelRot = XMMatrixRotationY(MathHelper::Pi);
		XMMATRIX modelOffset = XMMatrixTranslation(0.0f, 0.0f, -5.0f);

		MakeRenderItem(mSkinnedModelFilename, submeshName, mSkinnedMats[i].Name, modelScale * modelRot * modelOffset, 
			XMMatrixIdentity(), RenderLayer::SkinnedOpaque, true, 0, mSkinnedModelInst.get());
	}
}

void SkinnedMeshApp::BuildFrameResources()
{
	for (auto i : Range(0, gNumFrameResources))
	{
		auto frameRes = std::make_unique<FrameResource>(md3dDevice.Get(), 2,
			static_cast<UINT>(mAllRitems.size()), 1, static_cast<UINT>(mMaterials.size()));
		mFrameResources.emplace_back(std::move(frameRes));
	}
}

D3D12_SHADER_BYTECODE GetShaderBytecode(
	std::unordered_map<std::string, ComPtr<ID3DBlob>>& shaders, std::string&& name)
{
	return { shaders[name]->GetBufferPointer(), shaders[name]->GetBufferSize() };
}

void SkinnedMeshApp::MakeOpaqueDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc)
{
	inoutDesc->InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	inoutDesc->pRootSignature = mRootSignature.Get();
	inoutDesc->VS = GetShaderBytecode(mShaders, "standardVS");
	inoutDesc->PS = GetShaderBytecode(mShaders, "opaquePS");
	inoutDesc->RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	inoutDesc->BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	inoutDesc->DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	inoutDesc->SampleMask = UINT_MAX;
	inoutDesc->PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	inoutDesc->NumRenderTargets = 1;
	inoutDesc->RTVFormats[0] = mBackBufferFormat;
	inoutDesc->SampleDesc.Count = m4xMsaaState ? 4 : 1;
	inoutDesc->SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	inoutDesc->DSVFormat = mDepthStencilFormat;
}

void SkinnedMeshApp::MakeSkinnedOpaqueDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc)
{
	inoutDesc->InputLayout = { mSkinnedInputLayout.data(), (UINT)mSkinnedInputLayout.size() };
	inoutDesc->VS = GetShaderBytecode(mShaders, "skinnedVS");
	inoutDesc->PS = GetShaderBytecode(mShaders, "opaquePS");
}

void SkinnedMeshApp::MakeShadowOpaqueDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc)
{
	inoutDesc->RasterizerState.DepthBias = 100000;
	inoutDesc->RasterizerState.DepthBiasClamp = 0.0f;
	inoutDesc->RasterizerState.SlopeScaledDepthBias = 1.0f;
	inoutDesc->pRootSignature = mRootSignature.Get();
	inoutDesc->VS = GetShaderBytecode(mShaders, "shadowVS");
	inoutDesc->PS = GetShaderBytecode(mShaders, "shadowOpaquePS");
	inoutDesc->RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
	inoutDesc->NumRenderTargets = 0;
}

void SkinnedMeshApp::MakeSkinnedShadowOpaque(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc)
{
	MakeShadowOpaqueDesc(inoutDesc);
	inoutDesc->InputLayout = { mSkinnedInputLayout.data(), (UINT)mSkinnedInputLayout.size() };
	inoutDesc->VS = GetShaderBytecode(mShaders, "skinnedShadowVS");
	inoutDesc->PS = GetShaderBytecode(mShaders, "shadowOpaquePS");
}

void SkinnedMeshApp::MakeDebugDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc)
{
	inoutDesc->pRootSignature = mRootSignature.Get();
	inoutDesc->VS = GetShaderBytecode(mShaders, "debugVS");
	inoutDesc->PS = GetShaderBytecode(mShaders, "debugPS");
}

void SkinnedMeshApp::MakeDrawNormals(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc)
{
	inoutDesc->VS = GetShaderBytecode(mShaders, "drawNormalsVS");
	inoutDesc->PS = GetShaderBytecode(mShaders, "drawNormalsPS");
	inoutDesc->RTVFormats[0] = Ssao::NormalMapFormat;
	inoutDesc->SampleDesc.Count = 1;
	inoutDesc->SampleDesc.Quality = 0;
	inoutDesc->DSVFormat = mDepthStencilFormat;
}

void SkinnedMeshApp::MakeSkinnedDrawNormals(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc)
{
	MakeDrawNormals(inoutDesc);
	inoutDesc->InputLayout = { mSkinnedInputLayout.data(), (UINT)mSkinnedInputLayout.size() };
	inoutDesc->VS = GetShaderBytecode(mShaders, "skinnedDrawNormalsVS");
	inoutDesc->PS = GetShaderBytecode(mShaders, "drawNormalsPS");
}

void SkinnedMeshApp::MakeSsao(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc)
{
	inoutDesc->InputLayout = { nullptr, 0 };
	inoutDesc->pRootSignature = mSsaoRootSignature.Get();
	inoutDesc->VS = GetShaderBytecode(mShaders, "ssaoVS");
	inoutDesc->PS = GetShaderBytecode(mShaders, "ssaoPS");
	inoutDesc->DepthStencilState.DepthEnable = false;
	inoutDesc->DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	inoutDesc->RTVFormats[0] = Ssao::AmbientMapFormat;
	inoutDesc->SampleDesc.Count = 1;
	inoutDesc->SampleDesc.Quality = 0;
	inoutDesc->DSVFormat = DXGI_FORMAT_UNKNOWN;
}

void SkinnedMeshApp::MakeSsaoBlur(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc)
{
	MakeSsao(inoutDesc);
	inoutDesc->VS = GetShaderBytecode(mShaders, "ssaoBlurVS");
	inoutDesc->PS = GetShaderBytecode(mShaders, "ssaoBlurPS");
}

void SkinnedMeshApp::MakeSkyDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc)
{
	inoutDesc->RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	inoutDesc->DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	inoutDesc->pRootSignature = mRootSignature.Get();
	inoutDesc->VS = GetShaderBytecode(mShaders, "skyVS");
	inoutDesc->PS = GetShaderBytecode(mShaders, "skyPS");
}

void SkinnedMeshApp::MakePSOPipelineState(GraphicsPSO psoType)
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
	MakeOpaqueDesc(&psoDesc);

	switch (psoType)
	{
	case GraphicsPSO::Opaque:																										break;
	case GraphicsPSO::SkinnedOpaque:					MakeSkinnedOpaqueDesc(&psoDesc);		break;
	case GraphicsPSO::ShadowOpaque:					MakeShadowOpaqueDesc(&psoDesc);		break;
	case GraphicsPSO::SkinnedShadowOpaque:	MakeSkinnedShadowOpaque(&psoDesc);	break;
	case GraphicsPSO::Debug:									MakeDebugDesc(&psoDesc);						break;
	case GraphicsPSO::DrawNormals:						MakeDrawNormals(&psoDesc);					break;
	case GraphicsPSO::SkinnedDrawNormals:		MakeSkinnedDrawNormals(&psoDesc);		break;
	case GraphicsPSO::Ssao:									MakeSsao(&psoDesc);									break;
	case GraphicsPSO::SsaoBlur:								MakeSsaoBlur(&psoDesc);							break;
	case GraphicsPSO::Sky:										MakeSkyDesc(&psoDesc);								break;
	default: assert(!"wrong type");
	}

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSOs[psoType])));
}

void SkinnedMeshApp::BuildPSOs()
{
	for (auto gPso : GraphicsPSO_ALL)
		MakePSOPipelineState(gPso);
}

void SkinnedMeshApp::OnResize()
{
	D3DApp::OnResize();

	mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.f);

	if (mSsao != nullptr)
	{
		mSsao->OnResize(mClientWidth, mClientHeight);
		mSsao->RebuildDescriptors(mDepthStencilBuffer.Get());
	}
}

void SkinnedMeshApp::OnKeyboardInput(const GameTimer& gt)
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

void SkinnedMeshApp::AnimateMaterials(const GameTimer& gt)
{
}

void StoreMatrix4x4(XMFLOAT4X4& dest, XMFLOAT4X4& src) { XMStoreFloat4x4(&dest, XMMatrixTranspose(XMLoadFloat4x4(&src))); }
void StoreMatrix4x4(XMFLOAT4X4& dest, XMMATRIX src) { XMStoreFloat4x4(&dest, XMMatrixTranspose(src)); }
XMMATRIX Multiply(XMFLOAT4X4& m1, XMFLOAT4X4 m2) { return XMMatrixMultiply(XMLoadFloat4x4(&m1), XMLoadFloat4x4(&m2)); }
XMMATRIX Inverse(XMMATRIX& m) { return XMMatrixInverse(nullptr, m); }
XMMATRIX Inverse(XMFLOAT4X4& src) {	return Inverse(RvToLv(XMLoadFloat4x4(&src))); }

void SkinnedMeshApp::UpdateObjectCBs(const GameTimer& gt)
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

void SkinnedMeshApp::UpdateSkinnedCBs(const GameTimer& gt)
{
	auto currSkinnedCB = mCurFrameRes->SkinnedCB.get();

	mSkinnedModelInst->UpdateSkinnedAnimation(gt.DeltaTime());

	SkinnedConstants skinnedConstants;
	std::copy(
		std::begin(mSkinnedModelInst->FinalTransforms), 
		std::end(mSkinnedModelInst->FinalTransforms),
		&skinnedConstants.BoneTransforms[0]);

	currSkinnedCB->CopyData(0, skinnedConstants);
}

void SkinnedMeshApp::UpdateMaterialBuffer(const GameTimer& gt)
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
		matData.NormalMapIndex = mat->NormalSrvHeapIndex;
		StoreMatrix4x4(matData.MatTransform, mat->MatTransform);

		currMaterialBuffer->CopyData(mat->MatCBIndex, matData);

		mat->NumFramesDirty--;
	}
}

void SkinnedMeshApp::UpdateShadowTransform(const GameTimer& gt)
{
	XMVECTOR lightDir = XMLoadFloat3(&mRotatedLightDirections[0]);
	XMVECTOR lightPos = -2.0f * mSceneBounds.Radius * lightDir;
	XMVECTOR targetPos = XMLoadFloat3(&mSceneBounds.Center);
	XMVECTOR lightUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	XMMATRIX lightView = XMMatrixLookAtLH(lightPos, targetPos, lightUp);

	XMStoreFloat3(&mLightPosW, lightPos);

	XMFLOAT3 sphereCenterLS;
	XMStoreFloat3(&sphereCenterLS, XMVector3TransformCoord(targetPos, lightView));

	float l = sphereCenterLS.x - mSceneBounds.Radius;
	float b = sphereCenterLS.y - mSceneBounds.Radius;
	float n = sphereCenterLS.z - mSceneBounds.Radius;
	float r = sphereCenterLS.x + mSceneBounds.Radius;
	float t = sphereCenterLS.y + mSceneBounds.Radius;
	float f = sphereCenterLS.z + mSceneBounds.Radius;

	mLightNearZ = n;
	mLightFarZ = f;
	XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);
	//XMMATRIX lightProj = XMMatrixPerspectiveOffCenterLH(l, r, b, t, n, f);

	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	XMMATRIX S = lightView * lightProj * T;
	XMStoreFloat4x4(&mLightView, lightView);
	XMStoreFloat4x4(&mLightProj, lightProj);
	XMStoreFloat4x4(&mShadowTransform, S);
}

void SkinnedMeshApp::UpdateMainPassCB(const GameTimer& gt)
{
	auto& passCB = mCurFrameRes->PassCB;

	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();
	XMMATRIX viewProj = XMMatrixMultiply(view, proj);

	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);
	XMMATRIX viewProjTex = XMMatrixMultiply(viewProj, T);

	StoreMatrix4x4(mMainPassCB.View, view);
	StoreMatrix4x4(mMainPassCB.InvView, Inverse(view));
	StoreMatrix4x4(mMainPassCB.Proj, proj);
	StoreMatrix4x4(mMainPassCB.InvProj, Inverse(proj));
	StoreMatrix4x4(mMainPassCB.ViewProj, viewProj);
	StoreMatrix4x4(mMainPassCB.InvViewProj, Inverse(viewProj));
	StoreMatrix4x4(mMainPassCB.ViewProjTex, viewProjTex);
	StoreMatrix4x4(mMainPassCB.ShadowTransform, mShadowTransform);
	mMainPassCB.EyePosW = mCamera.GetPosition3f();
	mMainPassCB.RenderTargetSize = { (float)mClientWidth, (float)mClientHeight };
	mMainPassCB.InvRenderTargetSize = { 1.0f / (float)mClientWidth, 1.0f / (float)mClientHeight };
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
	mMainPassCB.Lights[0].Direction = mRotatedLightDirections[0];
	mMainPassCB.Lights[0].Strength = { 0.9f, 0.8f, 0.7f };
	mMainPassCB.Lights[1].Direction = mRotatedLightDirections[1];
	mMainPassCB.Lights[1].Strength = { 0.4f, 0.4f, 0.4f };
	mMainPassCB.Lights[2].Direction = mRotatedLightDirections[2];
	mMainPassCB.Lights[2].Strength = { 0.2f, 0.2f, 0.2f };

	passCB->CopyData(0, mMainPassCB);
}

void SkinnedMeshApp::UpdateShadowPassCB(const GameTimer& gt)
{
	auto& passCB = mCurFrameRes->PassCB;

	UINT w = mShadowMap->Width();
	UINT h = mShadowMap->Height();

	XMMATRIX view = XMLoadFloat4x4(&mLightView);
	XMMATRIX proj = XMLoadFloat4x4(&mLightProj);
	XMMATRIX viewProj = XMMatrixMultiply(view, proj);

	PassConstants shadowPC;
	StoreMatrix4x4(shadowPC.View, view);
	StoreMatrix4x4(shadowPC.InvView, Inverse(view));
	StoreMatrix4x4(shadowPC.Proj, proj);
	StoreMatrix4x4(shadowPC.InvProj, Inverse(proj));
	StoreMatrix4x4(shadowPC.ViewProj, viewProj);
	StoreMatrix4x4(shadowPC.InvViewProj, Inverse(viewProj));
	shadowPC.EyePosW = mLightPosW;
	shadowPC.RenderTargetSize = XMFLOAT2((float)w, (float)h);
	shadowPC.InvRenderTargetSize = XMFLOAT2(1.0f / w, 1.0f / h);
	shadowPC.NearZ = mLightNearZ;
	shadowPC.FarZ = mLightFarZ;

	passCB->CopyData(1, shadowPC);
}

void SkinnedMeshApp::UpdateSsaoCB(const GameTimer& gt)
{
	SsaoConstants ssaoCB{};

	XMMATRIX P = mCamera.GetProj();

	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	ssaoCB.Proj = mMainPassCB.Proj;
	ssaoCB.InvProj = mMainPassCB.InvProj;
	StoreMatrix4x4(ssaoCB.ProjTex, P * T);

	mSsao->GetOffsetVectors(ssaoCB.OffsetVectors);

	auto blurWeights = mSsao->CalcGaussWeights(2.5f);
	ssaoCB.BlurWeights[0] = XMFLOAT4(&blurWeights[0]);
	ssaoCB.BlurWeights[1] = XMFLOAT4(&blurWeights[4]);
	ssaoCB.BlurWeights[2] = XMFLOAT4(&blurWeights[8]);

	ssaoCB.InvRenderTargetSize = XMFLOAT2(1.0f / mSsao->SsaoMapWidth(), 1.0f / mSsao->SsaoMapHeight());

	ssaoCB.OcclusionRadius = 0.5f;
	ssaoCB.OcclusionFadeStart = 0.2f;
	ssaoCB.OcclusionFadeEnd = 2.0f;
	ssaoCB.SurfaceEpsilon = 0.05f;

	auto currSsaoCB = mCurFrameRes->SsaoCB.get();
	currSsaoCB->CopyData(0, ssaoCB);
}

void SkinnedMeshApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);

	mFrameResIdx = (mFrameResIdx + 1) % gNumFrameResources;
	mCurFrameRes = mFrameResources[mFrameResIdx].get();
	if (mCurFrameRes->Fence != 0 && mFence->GetCompletedValue() < mCurFrameRes->Fence)
	{
		HANDLE hEvent = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurFrameRes->Fence, hEvent));
		WaitForSingleObject(hEvent, INFINITE);
		CloseHandle(hEvent);
	}

	mLightRotationAngle += 0.1f * gt.DeltaTime();
	XMMATRIX R = XMMatrixRotationY(mLightRotationAngle);
	for (auto i : Range(0, 3))
	{
		XMVECTOR lightDir = XMLoadFloat3(&mBaseLightDirections[i]);
		lightDir = XMVector3TransformNormal(lightDir, R);
		XMStoreFloat3(&mRotatedLightDirections[i], lightDir);
	}
	
	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateSkinnedCBs(gt);
	UpdateMaterialBuffer(gt);
	UpdateShadowTransform(gt);
	UpdateMainPassCB(gt);
	UpdateShadowPassCB(gt);
	UpdateSsaoCB(gt);
}

void SkinnedMeshApp::DrawRenderItems(
	ID3D12GraphicsCommandList* cmdList,
	const std::vector<RenderItem*> ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT skinnedCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(SkinnedConstants));

	auto objectCB = mCurFrameRes->ObjectCB->Resource();
	auto skinnedCB = mCurFrameRes->SkinnedCB->Resource();

	for (auto& ri : ritems)
	{
		cmdList->IASetVertexBuffers(0, 1, &RvToLv(ri->Geo->VertexBufferView()));
		cmdList->IASetIndexBuffer(&RvToLv(ri->Geo->IndexBufferView()));
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = 
			objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
		cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);

		if (ri->SkinnedModelInst != nullptr)
		{
			D3D12_GPU_VIRTUAL_ADDRESS skinnedCBAddress = 
				skinnedCB->GetGPUVirtualAddress() + ri->SkinnedCBIndex * skinnedCBByteSize;
			cmdList->SetGraphicsRootConstantBufferView(1, skinnedCBAddress);
		}
		else
		{
			cmdList->SetGraphicsRootConstantBufferView(1, 0);
		}

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

void SkinnedMeshApp::DrawSceneToShadowMap()
{
	mCommandList->RSSetViewports(1, &RvToLv(mShadowMap->Viewport()));
	mCommandList->RSSetScissorRects(1, &RvToLv(mShadowMap->ScissorRect()));

	mCommandList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap->Resource(),
		D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE)));

	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

	mCommandList->ClearDepthStencilView(mShadowMap->Dsv(),
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	mCommandList->OMSetRenderTargets(0, nullptr, false, &RvToLv(mShadowMap->Dsv()));

	auto passCB = mCurFrameRes->PassCB->Resource();
	D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = passCB->GetGPUVirtualAddress() + 1 * passCBByteSize;
	mCommandList->SetGraphicsRootConstantBufferView(2, passCBAddress);

	mCommandList->SetPipelineState(mPSOs[GraphicsPSO::ShadowOpaque].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[RenderLayer::Opaque]);

	mCommandList->SetPipelineState(mPSOs[GraphicsPSO::SkinnedShadowOpaque].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[RenderLayer::SkinnedOpaque]);

	mCommandList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap->Resource(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_GENERIC_READ)));
}

void SkinnedMeshApp::DrawNormalsAndDepth()
{
	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	auto normalMap = mSsao->NormalMap();
	auto normalMapRtv = mSsao->NormalMapRtv();

	mCommandList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(normalMap,
		D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET)));

	float clearValue[] = { 0.0f, 0.0f, 1.0f, 0.0f };
	mCommandList->ClearRenderTargetView(normalMapRtv, clearValue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), 
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	mCommandList->OMSetRenderTargets(1, &normalMapRtv, true, &RvToLv(DepthStencilView()));

	auto passCB = mCurFrameRes->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

	mCommandList->SetPipelineState(mPSOs[GraphicsPSO::DrawNormals].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[RenderLayer::Opaque]);

	mCommandList->SetPipelineState(mPSOs[GraphicsPSO::SkinnedDrawNormals].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[RenderLayer::SkinnedOpaque]);

	mCommandList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(normalMap,
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ)));
}

void SkinnedMeshApp::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurFrameRes->CmdListAlloc;
	ThrowIfFailed(cmdListAlloc->Reset());

	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs[GraphicsPSO::Opaque].Get()));

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	auto matBuffer = mCurFrameRes->MaterialBuffer->Resource();
	mCommandList->SetGraphicsRootShaderResourceView(3, matBuffer->GetGPUVirtualAddress());

	mCommandList->SetGraphicsRootDescriptorTable(4, mNullSrv);
	mCommandList->SetGraphicsRootDescriptorTable(5, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	DrawSceneToShadowMap();
	DrawNormalsAndDepth();

	mCommandList->SetGraphicsRootSignature(mSsaoRootSignature.Get());
	mSsao->ComputeSsao(mCommandList.Get(), mCurFrameRes, 2);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	matBuffer = mCurFrameRes->MaterialBuffer->Resource();
	mCommandList->SetGraphicsRootShaderResourceView(3, matBuffer->GetGPUVirtualAddress());

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	mCommandList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET)));

	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
	mCommandList->OMSetRenderTargets(1, &RvToLv(CurrentBackBufferView()), true, &RvToLv(DepthStencilView()));

	mCommandList->SetGraphicsRootDescriptorTable(5, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	auto passCB = mCurFrameRes->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

	CD3DX12_GPU_DESCRIPTOR_HANDLE skyTexDescriptor(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	skyTexDescriptor.Offset(mSkyTexHeapIndex, mCbvSrvUavDescriptorSize);
	mCommandList->SetGraphicsRootDescriptorTable(4, skyTexDescriptor);

	//CD3DX12_GPU_DESCRIPTOR_HANDLE shadowTexDescriptor(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	//shadowTexDescriptor.Offset(mShadowMapHeapIndex, mCbvSrvUavDescriptorSize);
	//mCommandList->SetGraphicsRootDescriptorTable(4, shadowTexDescriptor);

	mCommandList->SetPipelineState(mPSOs[GraphicsPSO::Opaque].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[RenderLayer::Opaque]);

	mCommandList->SetPipelineState(mPSOs[GraphicsPSO::SkinnedOpaque].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[RenderLayer::SkinnedOpaque]);

	mCommandList->SetPipelineState(mPSOs[GraphicsPSO::Debug].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[RenderLayer::Debug]);

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

void SkinnedMeshApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		mLastMousePos.x = x;
		mLastMousePos.y = y;

		SetCapture(mhMainWnd);
	}
}
void SkinnedMeshApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}
void SkinnedMeshApp::OnMouseMove(WPARAM btnState, int x, int y)
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
		SkinnedMeshApp theApp(hInstance);
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