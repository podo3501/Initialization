#include "CameraAndDynamicIndexingApp.h"
#include "../Common/d3dUtil.h"
#include "../Common/Util.h"
#include "../Common/UploadBuffer.h"
#include "FrameResource.h"
#include "../Common/GeometryGenerator.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;

CameraAndDynamicIndexingApp::CameraAndDynamicIndexingApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

CameraAndDynamicIndexingApp::~CameraAndDynamicIndexingApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool CameraAndDynamicIndexingApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	mCamera.SetPosition(0.0f, 2.0f, -15.0f);

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
	ID3D12CommandList* cmdsLists2[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists2), cmdsLists2);

	FlushCommandQueue();

	return true;
}

void CameraAndDynamicIndexingApp::LoadTextures()
{
	auto TexDir = [](std::wstring&& filename)->std::wstring { return L"../Textures/" + filename; };
	std::vector<std::wstring> filenames
		{ TexDir(L"bricks.dds"), TexDir(L"stone.dds"), TexDir(L"tile.dds"), TexDir(L"WoodCrate01.dds") };
	
	for_each(filenames.begin(), filenames.end(), [&](auto& curFilename) {
		auto tex = std::make_unique<Texture>();
		tex->Filename = curFilename;
		ThrowIfFailed(d3dUtil::LoadTextureFromFile(md3dDevice.Get(), mCommandList.Get(),
			curFilename.c_str(), tex->Resource, tex->UploadHeap));
		mTextures.emplace_back(std::move(tex));
		});
}

void CameraAndDynamicIndexingApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 0 };

	CD3DX12_ROOT_PARAMETER param[4];
	param[0].InitAsConstantBufferView(0);
	param[1].InitAsConstantBufferView(1);
	param[2].InitAsShaderResourceView(0, 1);
	param[3].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);

	auto staticSamplers = d3dUtil::GetStaticSamplers();
	CD3DX12_ROOT_SIGNATURE_DESC rootDesc
	{ _countof(param), param, static_cast<UINT>(staticSamplers.size()),
		staticSamplers.data(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT };

	ComPtr<ID3DBlob> serialized = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, 
		serialized.GetAddressOf(), errorBlob.GetAddressOf());
	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(0,
		serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&mRootSignature)));
}

void CameraAndDynamicIndexingApp::BuildDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
	heapDesc.NodeMask = 0;
	heapDesc.NumDescriptors = static_cast<UINT>(mTextures.size());
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
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
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDesc{ mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart() };
		hCpuDesc.Offset(index++, mCbvSrvUavDescriptorSize);
		md3dDevice->CreateShaderResourceView(curTex->Resource.Get(), &srvDesc, hCpuDesc);
		});
}

void CameraAndDynamicIndexingApp::BuildShadersAndInputLayout()
{
	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders/VertexShader.hlsl", nullptr, "main", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders/PixelShader.hlsl", nullptr, "main", "ps_5_1");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

void CameraAndDynamicIndexingApp::BuildShapeGeometry()
{
	GeometryGenerator geoGen{};
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);
	std::vector<GeometryGenerator::MeshData*> mMeshDataList{ &box, &grid, &sphere, &cylinder };

	UINT totalVertexCount = 0;
	UINT totalIndexCount = 0;
	std::vector<SubmeshGeometry> submeshes;
	for_each(mMeshDataList.begin(), mMeshDataList.end(), [&](auto curMeshData) {
		UINT curIndexCount = static_cast<UINT>(curMeshData->Indices32.size());

		SubmeshGeometry submesh;
		submesh.BaseVertexLocation = totalVertexCount;
		submesh.StartIndexLocation = totalIndexCount;
		submesh.IndexCount = curIndexCount;
		submeshes.emplace_back(submesh);

		totalVertexCount += static_cast<UINT>(curMeshData->Vertices.size());
		totalIndexCount += curIndexCount;
		});

	std::vector<Vertex> vertices(totalVertexCount);
	for_each(mMeshDataList.begin(), mMeshDataList.end(), [&, k{ 0 }](auto curMeshData) mutable {
		for (auto i : Range(0, static_cast<int>(curMeshData->Vertices.size())))
		{
			vertices[k].Pos = curMeshData->Vertices[i].Position;
			vertices[k].Normal = curMeshData->Vertices[i].Normal;
			vertices[k].TexC = curMeshData->Vertices[i].TexC;
			k++;
		}});
	
	std::vector<std::uint16_t> indices;
	for_each(mMeshDataList.begin(), mMeshDataList.end(), [&](auto curMeshData) {
		auto& curIndices = curMeshData->GetIndices16();
		indices.insert(indices.end(), curIndices.begin(), curIndices.end());
		});

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";
	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		vertices.data(), vbByteSize, geo->VertexBufferUploader);
	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		indices.data(), ibByteSize, geo->IndexBufferUploader);
	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["box"] = submeshes[0];
	geo->DrawArgs["grid"] = submeshes[1];
	geo->DrawArgs["sphere"] = submeshes[2];
	geo->DrawArgs["cylinder"] = submeshes[3];

	mGeometries[geo->Name] = std::move(geo);
}

void CameraAndDynamicIndexingApp::BuildMaterials()
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

	MakeMaterial("bricks0", 0, 0, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.02f, 0.02f, 0.02f }, 0.1f);
	MakeMaterial("stone0", 1, 1, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.05f, 0.05f, 0.05f }, 0.3f);
	MakeMaterial("tile0", 2, 2, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.02f, 0.02f, 0.02f }, 0.3f);
	MakeMaterial("crate0", 3, 3, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.05f, 0.05f, 0.05f }, 0.2f);
}

void CameraAndDynamicIndexingApp::BuildRenderItems()
{
	auto MakeRenderItem = [&, objIdx{ 0 }](std::string&& smName, std::string&& matName,
		const XMMATRIX& world, const XMMATRIX& texTransform) mutable {
		auto renderItem = std::make_unique<RenderItem>();
		auto& sm = mGeometries["shapeGeo"]->DrawArgs[smName];
		renderItem->Geo = mGeometries["shapeGeo"].get();
		renderItem->StartIndexLocation = sm.StartIndexLocation;
		renderItem->BaseVertexLocation = sm.BaseVertexLocation;
		renderItem->IndexCount = sm.IndexCount;
		renderItem->Mat = mMaterials[matName].get();
		renderItem->ObjCBIndex = objIdx++;
		renderItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		XMStoreFloat4x4(&renderItem->World, world);
		XMStoreFloat4x4(&renderItem->TexTransform, texTransform);
		mAllRitems.emplace_back(std::move(renderItem));};

	MakeRenderItem("box", "crate0", XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 1.0f, 0.0f), XMMatrixScaling(1.0f, 1.0f, 1.0f));
	MakeRenderItem("box", "bricks0", XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 1.0f, -5.0f), XMMatrixScaling(1.0f, 1.0f, 1.0f));
	MakeRenderItem("box", "stone0", XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 1.0f, 5.0f), XMMatrixScaling(1.0f, 1.0f, 1.0f));
	MakeRenderItem("box", "tile0", XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 1.0f, 10.0f), XMMatrixScaling(1.0f, 1.0f, 1.0f));

	MakeRenderItem("grid", "tile0", XMMatrixIdentity(), XMMatrixScaling(8.0f, 8.0f, 1.0f));

	XMMATRIX brickTexTransform = XMMatrixScaling(1.0f, 1.0f, 1.0f);
	for (auto i : Range(0, 5))
	{
		XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f);
		XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
		XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f);
		XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);

		MakeRenderItem("cylinder", "bricks0", rightCylWorld, brickTexTransform);
		MakeRenderItem("cylinder", "bricks0", leftCylWorld, brickTexTransform);
		MakeRenderItem("sphere", "stone0", rightSphereWorld, brickTexTransform);
		MakeRenderItem("sphere", "stone0", leftSphereWorld, brickTexTransform);
	}
	
	for (auto& e : mAllRitems)
		mOpaqueRitems.emplace_back(e.get());
}

void CameraAndDynamicIndexingApp::BuildFrameResources()
{
	for (auto i : Range(0, gNumFrameResources))
	{
		auto frameRes = std::make_unique<FrameResource>(md3dDevice.Get(), 1,
			static_cast<UINT>(mAllRitems.size()), static_cast<UINT>(mMaterials.size()));
		mFrameResources.emplace_back(std::move(frameRes));
	}
}

D3D12_SHADER_BYTECODE GetShaderBytecode(
	std::unordered_map<std::string, ComPtr<ID3DBlob>>& shaders, std::string&& name)
{
	return { shaders[name]->GetBufferPointer(), shaders[name]->GetBufferSize() };
}

void CameraAndDynamicIndexingApp::MakeOpaqueDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc)
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

void CameraAndDynamicIndexingApp::MakePSOPipelineState(GraphicsPSO psoType)
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
	MakeOpaqueDesc(&psoDesc);

	switch (psoType)
	{
	case GraphicsPSO::Opaque:						break;
	default: assert(!"wrong type");
	}

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSOs[GraphicsPSO::Opaque])));
}

void CameraAndDynamicIndexingApp::BuildPSOs()
{
	for (auto gPso : GraphicsPSO_ALL)
		MakePSOPipelineState(gPso);
}

void CameraAndDynamicIndexingApp::OnResize()
{
	D3DApp::OnResize();

	mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.f);
}

void CameraAndDynamicIndexingApp::OnKeyboardInput(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();

	float speed = 10.0f;
	float angle = 0.2f;
	float walkSpeed = 0.0f;
	float strafeSpeed = 0.0f;
	float rollSpeed = 0.0f;
	float pitchSpeed = 0.0f;

	std::vector<int> keyList{ 'W', 'S', 'D', 'A', 'Q', 'E', 'F', 'R' };
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
			case 'Q':		rollSpeed += angle;			break;
			case 'E':		rollSpeed += -angle;			break;
			case 'F':		pitchSpeed += angle;		break;
			case 'R':		pitchSpeed += -angle;		break;
			}
		}});
	
	mCamera.Move(Camera::eWalk, walkSpeed * dt);
	mCamera.Move(Camera::eStrafe, strafeSpeed * dt);
	mCamera.Move(Camera::eRoll, MathHelper::Pi * rollSpeed * dt);
	mCamera.Move(Camera::ePitch, MathHelper::Pi * pitchSpeed * dt);

	mCamera.UpdateViewMatrix();
}

void CameraAndDynamicIndexingApp::AnimateMaterials(const GameTimer& gt)
{
}

void StoreMatrix4x4(XMFLOAT4X4& dest, XMFLOAT4X4& src) { XMStoreFloat4x4(&dest, XMMatrixTranspose(XMLoadFloat4x4(&src))); }
void StoreMatrix4x4(XMFLOAT4X4& dest, XMMATRIX src) { XMStoreFloat4x4(&dest, XMMatrixTranspose(src)); }
XMMATRIX Multiply(XMFLOAT4X4& m1, XMFLOAT4X4 m2) { return XMMatrixMultiply(XMLoadFloat4x4(&m1), XMLoadFloat4x4(&m2)); }
XMMATRIX Inverse(XMMATRIX& m) { return XMMatrixInverse(nullptr, m); }
XMMATRIX Inverse(XMFLOAT4X4& src) {	return Inverse(RvToLv(XMLoadFloat4x4(&src))); }

void CameraAndDynamicIndexingApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto& objCB = mCurFrameRes->ObjectCB;
	
	for (auto& r : mAllRitems)
	{
		if (r->NumFramesDirty <= 0) break;

		ObjectConstants oc;
		StoreMatrix4x4(oc.World, r->World);
		StoreMatrix4x4(oc.TexTransform, r->TexTransform);
		oc.MaterialIndex = r->Mat->MatCBIndex;
		
		objCB->CopyData(r->ObjCBIndex, oc);

		r->NumFramesDirty--;
	}
}

void CameraAndDynamicIndexingApp::UpdateMaterialBuffer(const GameTimer& gt)
{
	auto& matBuffer = mCurFrameRes->MaterialBuffer;

	for (auto& mat : mMaterials)
	{
		auto& m = mat.second;
		if (m->NumFramesDirty <= 0) break;

		MaterialData md;
		md.DiffuseAlbedo = m->DiffuseAlbedo;
		md.FresnelR0 = m->FresnelR0;
		md.Roughness = m->Roughness;
		md.DiffuseMapIndex = m->DiffuseSrvHeapIndex;
		StoreMatrix4x4(md.MatTransform, m->MatTransform);

		matBuffer->CopyData(m->MatCBIndex, md);

		m->NumFramesDirty--;
	}
}

void CameraAndDynamicIndexingApp::UpdateMainPassCB(const GameTimer& gt)
{
	auto& passCB = mCurFrameRes->PassCB;
	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);

	PassConstants pc;
	StoreMatrix4x4(pc.View, view);
	StoreMatrix4x4(pc.InvView, Inverse(view));
	StoreMatrix4x4(pc.Proj, proj);
	StoreMatrix4x4(pc.InvProj, Inverse(proj));
	StoreMatrix4x4(pc.ViewProj, viewProj);
	StoreMatrix4x4(pc.InvViewProj, Inverse(viewProj));
	pc.EyePosW = mCamera.GetPosition3f();
	pc.RenderTargetSize = { (float)mClientWidth, (float)mClientHeight };
	pc.InvRenderTargetSize = { 1.0f / (float)mClientWidth, 1.0f / (float)mClientHeight };
	pc.NearZ = 1.0f;
	pc.FarZ = 1000.0f;
	pc.TotalTime = gt.TotalTime();
	pc.DeltaTime = gt.DeltaTime();
	pc.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
	pc.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	pc.Lights[0].Strength = { 0.8f, 0.8f, 0.8f };
	pc.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	pc.Lights[1].Strength = { 0.4f, 0.4f, 0.4f };
	pc.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	pc.Lights[2].Strength = { 0.2f, 0.2f, 0.2f };

	passCB->CopyData(0, pc);
}

void CameraAndDynamicIndexingApp::Update(const GameTimer& gt)
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
	
	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialBuffer(gt);
	UpdateMainPassCB(gt);
}

void CameraAndDynamicIndexingApp::DrawRenderItems(
	ID3D12GraphicsCommandList* cmdList,
	const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	auto objCBRes = mCurFrameRes->ObjectCB->Resource();

	for (auto ri : ritems)
	{
		cmdList->IASetVertexBuffers(0, 1, &RvToLv(ri->Geo->VertexBufferView()));
		cmdList->IASetIndexBuffer(&RvToLv(ri->Geo->IndexBufferView()));
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddr = objCBRes->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
		cmdList->SetGraphicsRootConstantBufferView(0, objCBAddr);
		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

void CameraAndDynamicIndexingApp::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurFrameRes->CmdListAlloc;
	ThrowIfFailed(cmdListAlloc->Reset());
	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs[GraphicsPSO::Opaque].Get()));

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	mCommandList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), 
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET)));

	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
		1.0f, 0, 0, nullptr);

	mCommandList->OMSetRenderTargets(1, &RvToLv(CurrentBackBufferView()), true, &RvToLv(DepthStencilView()));

	ID3D12DescriptorHeap* ppDescriptorHeap[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(ppDescriptorHeap), ppDescriptorHeap);
	
	auto& passCB = mCurFrameRes->PassCB;
	mCommandList->SetGraphicsRootConstantBufferView(1, passCB->Resource()->GetGPUVirtualAddress());

	auto& matBuffer = mCurFrameRes->MaterialBuffer;
	mCommandList->SetGraphicsRootShaderResourceView(2, matBuffer->Resource()->GetGPUVirtualAddress());

	mCommandList->SetGraphicsRootDescriptorTable(3, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	DrawRenderItems(mCommandList.Get(), mOpaqueRitems);

	mCommandList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT)));
	ThrowIfFailed(mCommandList->Close());

	ID3D12CommandList* ppCommandLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	mCurFrameRes->Fence = ++mCurrentFence;
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void CameraAndDynamicIndexingApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}
void CameraAndDynamicIndexingApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}
void CameraAndDynamicIndexingApp::OnMouseMove(WPARAM btnState, int x, int y)
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
		CameraAndDynamicIndexingApp theApp(hInstance);
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