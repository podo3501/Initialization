#include "d3dUtil.h"
#include "Util.h"
#include <comdef.h>
#include <fstream>
#include "../Common/DirectXTK12Inc/WICTextureLoader.h"

using Microsoft::WRL::ComPtr;

DxException::DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber) :
    ErrorCode(hr),
    FunctionName(functionName),
    Filename(filename),
    LineNumber(lineNumber)
{
}

bool d3dUtil::IsKeyDown(int vkeyCode)
{
    return (GetAsyncKeyState(vkeyCode) & 0x8000) != 0;
}

ComPtr<ID3DBlob> d3dUtil::LoadBinary(const std::wstring& filename)
{
    std::ifstream fin(filename, std::ios::binary);

    fin.seekg(0, std::ios_base::end);
    std::ifstream::pos_type size = (int)fin.tellg();
    fin.seekg(0, std::ios_base::beg);

    ComPtr<ID3DBlob> blob;
    ThrowIfFailed(D3DCreateBlob(static_cast<SIZE_T>(size), blob.GetAddressOf()));

    fin.read((char*)blob->GetBufferPointer(), size);
    fin.close();

    return blob;
}

Microsoft::WRL::ComPtr<ID3D12Resource> d3dUtil::CreateDefaultBuffer(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    const void* initData,
    UINT64 byteSize,
    Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer)
{
    ComPtr<ID3D12Resource> defaultBuffer;

    // 실제 기본 버퍼 자원을 생성한다.
    // 정적 기하구조를 그릴 때에는 최적의 성능을 위해 기본힙(D3D12_HEAP_TYPE_DEFAULT)에 넣는다.
    ThrowIfFailed(device->CreateCommittedResource(
        &RvToLv(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
        D3D12_HEAP_FLAG_NONE,
        &RvToLv(CD3DX12_RESOURCE_DESC(CD3DX12_RESOURCE_DESC::Buffer(byteSize))),
		D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(defaultBuffer.GetAddressOf())));

    // cpu메모리(ram)의 자료를 기본 버퍼(vram)에 복사하려면
    // 임시 업로드 힙을 만들어야 한다. 기본버퍼는 cpu에서 접근이 가능하지 않기 때문이다. 
    ThrowIfFailed(device->CreateCommittedResource(
        &RvToLv(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
		D3D12_HEAP_FLAG_NONE,
        &RvToLv(CD3DX12_RESOURCE_DESC(CD3DX12_RESOURCE_DESC::Buffer(byteSize))),
		D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(uploadBuffer.GetAddressOf())));


    // 기본버퍼에 복사할 자료이다.
    D3D12_SUBRESOURCE_DATA subResourceData = {};
    subResourceData.pData = initData;
    subResourceData.RowPitch = static_cast<LONG_PTR>(byteSize);
    subResourceData.SlicePitch = subResourceData.RowPitch;

    //기본 버퍼 자원으로의 자료 복사를 '스케쥴'한다(지금 실행되는 것이 아님)
    //개략적으로 말하자면, subResourceData를 임시 업로드 힙에 복사하고, 
    //ID3D12CommandList::CopySubresourceRegion을 이용해서 임시 업로드 힙의 자료를 defaultBuffer에 복사한다.
    CD3DX12_RESOURCE_BARRIER barrierDest(
        CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));
	cmdList->ResourceBarrier(1, &barrierDest);

    UpdateSubresources<1>(cmdList, defaultBuffer.Get(), uploadBuffer.Get(), 0, 0, 1, &subResourceData);

    CD3DX12_RESOURCE_BARRIER barrierRead(
        CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ));
	cmdList->ResourceBarrier(1, &barrierRead);

    // Note: uploadBuffer has to be kept alive after the above function calls because
    // the command list has not been executed yet that performs the actual copy.
    // The caller can Release the uploadBuffer after it knows the copy has been executed.


    return defaultBuffer;
}

ComPtr<ID3DBlob> d3dUtil::CompileShader(
	const std::wstring& filename,
	const D3D_SHADER_MACRO* defines,
	const std::string& entrypoint,
	const std::string& target)
{
	UINT compileFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)  
	compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	HRESULT hr = S_OK;

	ComPtr<ID3DBlob> byteCode = nullptr;
	ComPtr<ID3DBlob> errors;
	hr = D3DCompileFromFile(filename.c_str(), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		entrypoint.c_str(), target.c_str(), compileFlags, 0, &byteCode, &errors);

	if(errors != nullptr)
		OutputDebugStringA((char*)errors->GetBufferPointer());

	ThrowIfFailed(hr);

	return byteCode;
}

HRESULT d3dUtil::LoadTextureFromFile(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    std::wstring&& filename,
    Microsoft::WRL::ComPtr<ID3D12Resource>& texture,
    Microsoft::WRL::ComPtr<ID3D12Resource>& uploadHeaps)
{
    std::unique_ptr<uint8_t[]> decodedData;
    D3D12_SUBRESOURCE_DATA subresource;
    HRESULT hr = DirectX::LoadWICTextureFromFile(device, filename.c_str(), texture.ReleaseAndGetAddressOf(),
        decodedData, subresource);
    if (FAILED(hr))
    {
        return hr;
    }

    const UINT64 uploadBufferSize = GetRequiredIntermediateSize(texture.Get(), 0, 1);

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);

    auto desc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);

    // Create the GPU upload buffer.
    hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(uploadHeaps.GetAddressOf()));
    if (FAILED(hr))
    {
        return hr;
    }

    UpdateSubresources(cmdList, texture.Get(), uploadHeaps.Get(),
        0, 0, 1, &subresource);

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(texture.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &barrier);  

    return S_OK;
}

std::wstring DxException::ToString()const
{
    // Get the string description of the error code.
    _com_error err(ErrorCode);
    std::wstring msg = err.ErrorMessage();

    return FunctionName + L" failed in " + Filename + L"; line " + std::to_wstring(LineNumber) + L"; error: " + msg;
}

std::vector<D3D12_STATIC_SAMPLER_DESC> d3dUtil::GetStaticSamplers()
{
    std::vector<D3D12_STATIC_SAMPLER_DESC> samplers;

    auto MakeSampler = [&samplers, count{ 0u }](D3D12_FILTER filter,
        D3D12_TEXTURE_ADDRESS_MODE addressUVW, FLOAT mipLODBias = 0.0f, UINT maxAnisotropy = 16) mutable
        {
            samplers.emplace_back(CD3DX12_STATIC_SAMPLER_DESC
                {
                    count++, filter,
                    addressUVW, addressUVW, addressUVW,
                    mipLODBias,
                    maxAnisotropy
                });
        };

    MakeSampler(D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
    MakeSampler(D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    MakeSampler(D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
    MakeSampler(D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    MakeSampler(D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_WRAP, 0.0f, 8);
    MakeSampler(D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 0.0f, 8);

    return samplers;
}


