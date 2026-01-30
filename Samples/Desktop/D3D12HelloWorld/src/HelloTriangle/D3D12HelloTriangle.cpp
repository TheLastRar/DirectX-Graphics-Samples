//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "stdafx.h"
#include "D3D12HelloTriangle.h"
#include <wincodec.h>
#include <wincodecsdk.h>

#include <string>
#include <cassert>
#include <random>

template<typename T>
T AlignUp(T t, T alignment)
{
    return ((t + alignment - 1) / alignment) * alignment;
}

template<typename T>
T AlignDown(T t, T alignment)
{
    return (t / alignment) * alignment;
}

void D3D12HelloTriangle::SaveReadbackBufferAsImage(
    const D3D12_RESOURCE_DESC& textureDesc, ComPtr<ID3D12Resource> readbackBuffer, const std::wstring& filename)
{
    const UINT rowPitch = (UINT)AlignUp<UINT64>(textureDesc.Width * 4, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
    const UINT totalBytes = rowPitch * textureDesc.Height;

    BYTE* pData;
    D3D12_RANGE readRange = { 0, totalBytes };
    readbackBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pData));

    ComPtr<IWICImagingFactory> wicFactory;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&wicFactory));

    ComPtr<IWICBitmap> bitmap;
    wicFactory->CreateBitmapFromMemory(
        (UINT)textureDesc.Width,
        (UINT)textureDesc.Height,
        textureDesc.Format == DXGI_FORMAT_R32_FLOAT ? GUID_WICPixelFormat32bppGrayFloat : GUID_WICPixelFormat32bppRGBA,
        rowPitch,
        totalBytes,
        pData,
        &bitmap
    );

    ComPtr<IWICFormatConverter> converter;
    if (textureDesc.Format == DXGI_FORMAT_R32_FLOAT)
    {
        ThrowIfFailed(wicFactory->CreateFormatConverter(&converter));

        ThrowIfFailed(converter->Initialize(
            bitmap.Get(),
            GUID_WICPixelFormat8bppGray,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom
        ));
    }

    readbackBuffer->Unmap(0, nullptr);

    ComPtr<IWICStream> stream;
    ThrowIfFailed(wicFactory->CreateStream(&stream));
    ThrowIfFailed(stream->InitializeFromFilename(filename.c_str(), GENERIC_WRITE));

    ComPtr<IWICBitmapEncoder> encoder;
    ThrowIfFailed(wicFactory->CreateEncoder(
        GUID_ContainerFormatPng,
        nullptr,
        &encoder));
    ThrowIfFailed(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;

    ThrowIfFailed(encoder->CreateNewFrame(&frame, &props));
    ThrowIfFailed(frame->Initialize(props.Get()));

    if (converter)
    {
        frame->WriteSource(converter.Get(), nullptr);
    }
    else
    {
        frame->WriteSource(bitmap.Get(), nullptr);
    }

    ThrowIfFailed(frame->Commit());
    ThrowIfFailed(encoder->Commit());
}

void D3D12HelloTriangle::SaveRenderTargetReadbackBufferAsImage(const std::wstring& filename)
{
    SaveReadbackBufferAsImage(m_renderTargets[0]->GetDesc(), m_renderTargetReadbackBuffer, filename);
}

void D3D12HelloTriangle::SaveDepthAsColorTargetReadbackBufferAsImage(const std::wstring& filename)
{
    SaveReadbackBufferAsImage(m_depthAsColorTarget->GetDesc(), m_depthAsColorTargetReadbackBuffer, filename);
}

void D3D12HelloTriangle::CreateReadbackBuffer(const D3D12_RESOURCE_DESC& textureDesc, ComPtr<ID3D12Resource>& readbackBuffer)
{
    UINT64 totalBytes = 0;
    m_device->GetCopyableFootprints(
        &textureDesc,
        0, // first subresource
        1, // number of subresources
        0, // base offset
        nullptr, // out layouts
        nullptr, // out num rows
        nullptr, // out row size
        &totalBytes
    );

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Alignment = 0;
    bufferDesc.Width = totalBytes;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.SampleDesc.Quality = 0;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&readbackBuffer)
    );
}

void D3D12HelloTriangle::CreateDepthAsColorTargetReadbackBuffer()
{
    CreateReadbackBuffer(m_depthAsColorTarget->GetDesc(), m_depthAsColorTargetReadbackBuffer);
}
void D3D12HelloTriangle::CreateRenderTargetReadbackBuffer()
{
    CreateReadbackBuffer(m_renderTargets[0]->GetDesc(), m_renderTargetReadbackBuffer);
}

void D3D12HelloTriangle::CopyToReadbackBuffer(ComPtr<ID3D12Resource> texture, ComPtr<ID3D12Resource> readbackBuffer,
    D3D12_RESOURCE_STATES currState)
{
    D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
    srcLocation.pResource = texture.Get();
    srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    srcLocation.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
    dstLocation.pResource = readbackBuffer.Get();
    dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;

    D3D12_RESOURCE_DESC textureDesc = texture->GetDesc();

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    footprint.Offset = 0;
    footprint.Footprint.Format = textureDesc.Format;
    footprint.Footprint.Width = (UINT)textureDesc.Width;
    footprint.Footprint.Height = textureDesc.Height;
    footprint.Footprint.Depth = 1;
    footprint.Footprint.RowPitch = (UINT)AlignUp<UINT64>(textureDesc.Width * 4, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);

    dstLocation.PlacedFootprint = footprint;

    {
        // Signal and increment the fence value.
        const UINT64 fence = m_fenceValue;
        ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
        m_fenceValue++;

        // Wait until the previous frame is finished.
        if (m_fence->GetCompletedValue() < fence)
        {
            ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
            WaitForSingleObject(m_fenceEvent, INFINITE);
        }
    }

    // Reset and execute the copy command.
    {
        ThrowIfFailed(m_commandAllocator->Reset());
        ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState[0].Get()));
        m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            texture.Get(), currState, D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES));
        m_commandList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);
        m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            texture.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, currState,
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES));
        ThrowIfFailed(m_commandList->Close());
        ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
        m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
    }

    {
        // Signal and increment the fence value.
        const UINT64 fence = m_fenceValue;
        ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
        m_fenceValue++;

        // Wait until the copy frame is finished.
        if (m_fence->GetCompletedValue() < fence)
        {
            ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
            WaitForSingleObject(m_fenceEvent, INFINITE);
        }
    }
}

void D3D12HelloTriangle::CopyRenderTargetToReadbackBuffer(UINT i)
{
    CopyToReadbackBuffer(m_renderTargets[i], m_renderTargetReadbackBuffer, D3D12_RESOURCE_STATE_PRESENT);
}

void D3D12HelloTriangle::CopyDepthCompareTargetToReadbackBuffer()
{
    CopyToReadbackBuffer(m_depthCompareTarget, m_renderTargetReadbackBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
}

void D3D12HelloTriangle::CopyDepthAsColorTargetToReadbackBuffer()
{
    CopyToReadbackBuffer(m_depthAsColorTarget, m_depthAsColorTargetReadbackBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
}

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 618; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\"; }

D3D12HelloTriangle::D3D12HelloTriangle(UINT width, UINT height, std::wstring name) :
    DXSample(width, height, name),
    m_frameIndex(0),
    m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
    m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
    m_rtvDescriptorSize(0)
{
}

void D3D12HelloTriangle::OnInit()
{
    LoadPipeline();
    LoadAssets();
    LoadAssetsCompare();
    CreateRenderTargetReadbackBuffer();
    CreateDepthAsColorTargetReadbackBuffer();
}

// Load the rendering pipeline dependencies.
void D3D12HelloTriangle::LoadPipeline()
{
    UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    // Enable the debug layer (requires the Graphics Tools "optional feature").
    // NOTE: Enabling the debug layer after device creation will invalidate the active device.
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();

            // Enable additional debug layers.
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

    if (m_useWarpDevice)
    {
        ComPtr<IDXGIAdapter> warpAdapter;
        ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

        ThrowIfFailed(D3D12CreateDevice(
            warpAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_device)
        ));
    }
    else
    {
        ComPtr<IDXGIAdapter1> hardwareAdapter;
        GetHardwareAdapter(factory.Get(), &hardwareAdapter);

        ThrowIfFailed(D3D12CreateDevice(
            hardwareAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_device)
        ));
    }

    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = m_width;
    swapChainDesc.Height = m_height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(
        m_commandQueue.Get(),        // Swap chain needs the queue so that it can force a flush on it.
        Win32Application::GetHwnd(),
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain
    ));

    // This sample does not support fullscreen transitions.
    ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

    ThrowIfFailed(swapChain.As(&m_swapChain));
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // Create depth buffer
    {
        D3D12_RESOURCE_DESC depthDesc = {};
        depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthDesc.Alignment = 0;
        depthDesc.Width = m_width;
        depthDesc.Height = m_height;
        depthDesc.DepthOrArraySize = 1;
        depthDesc.MipLevels = 1;
        depthDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.SampleDesc.Quality = 0;
        depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = DXGI_FORMAT_D32_FLOAT;
        clearValue.DepthStencil.Depth = 0.0f;
        clearValue.DepthStencil.Stencil = 0;

        m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &depthDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &clearValue,
            IID_PPV_ARGS(&m_depthTarget)
        );
    }

    // Create depth as color buffer
    {
        D3D12_RESOURCE_DESC depthAsColorDesc = {};
        depthAsColorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthAsColorDesc.Alignment = 0;
        depthAsColorDesc.Width = m_width;
        depthAsColorDesc.Height = m_height;
        depthAsColorDesc.DepthOrArraySize = 1;
        depthAsColorDesc.MipLevels = 1;
        depthAsColorDesc.Format = DXGI_FORMAT_R32_FLOAT;
        depthAsColorDesc.SampleDesc.Count = 1;
        depthAsColorDesc.SampleDesc.Quality = 0;
        depthAsColorDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        depthAsColorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE clearValue = { DXGI_FORMAT_R32_FLOAT, { 0.0f, 0.0f, 0.0f, 1.0f } };

        m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &depthAsColorDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            &clearValue,
            IID_PPV_ARGS(&m_depthAsColorTarget)
        );
    }

    // Create depth compare buffer
    {
        D3D12_RESOURCE_DESC depthCompareDesc = {};
        depthCompareDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthCompareDesc.Alignment = 0;
        depthCompareDesc.Width = m_width;
        depthCompareDesc.Height = m_height;
        depthCompareDesc.DepthOrArraySize = 1;
        depthCompareDesc.MipLevels = 1;
        depthCompareDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        depthCompareDesc.SampleDesc.Count = 1;
        depthCompareDesc.SampleDesc.Quality = 0;
        depthCompareDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        depthCompareDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE clearValue = { DXGI_FORMAT_R8G8B8A8_UNORM, { 0.0f, 0.0f, 0.0f, 1.0f } };

        m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &depthCompareDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            &clearValue,
            IID_PPV_ARGS(&m_depthCompareTarget)
        );
    }

    // Create RTV descriptor heaps.
    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount + 2;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }

    // Create DSV descriptor heaps.
    {
        // Describe and create a depth stencil view (DSV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
        dsvHeapDesc.NumDescriptors = FrameCount;
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));

        m_dsvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    }

    // Create SRV descriptor heaps.
    {
        // Describe and create a shader resource view (SRV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
        srvHeapDesc.NumDescriptors = 2;
        srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvHeap)));

        m_srvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    // Create GPU visible SRV heap.
    {
        // Describe and create a shader resource view (SRV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
        srvHeapDesc.NumDescriptors = 2;
        srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvHeapGpu)));
    }

    // Create frame resources.
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
        CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
        CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_srvHeap->GetCPUDescriptorHandleForHeapStart());

        // Create a RTV for each frame.
        for (UINT n = 0; n < FrameCount; n++)
        {
            ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
            m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
            m_rtvHandles[n] = rtvHandle;
            rtvHandle.Offset(1, m_rtvDescriptorSize);
        }

        // Create RTV for depth as color.
        m_device->CreateRenderTargetView(m_depthAsColorTarget.Get(), nullptr, rtvHandle);
        m_depthAsColorRtvHandle = rtvHandle;
        rtvHandle.Offset(1, m_rtvDescriptorSize);

        // Create RTV for depth compare.
        m_device->CreateRenderTargetView(m_depthCompareTarget.Get(), nullptr, rtvHandle);
        m_depthCompareRtvHandle = rtvHandle;
        rtvHandle.Offset(1, m_rtvDescriptorSize);

        // Create DSV
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvd{DXGI_FORMAT_D32_FLOAT, D3D12_DSV_DIMENSION_TEXTURE2D, D3D12_DSV_FLAG_NONE};

        m_device->CreateDepthStencilView(m_depthTarget.Get(), &dsvd, dsvHandle);
        m_dsvHandle = dsvHandle;
        dsvHandle.Offset(1, m_dsvDescriptorSize);

        // Create first SRV
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;

            m_device->CreateShaderResourceView(m_depthTarget.Get(), &srvDesc, srvHandle);
            m_depthSrvHandle = srvHandle;
            srvHandle.Offset(1, m_srvDescriptorSize);
        }

        // Create second SRV
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;

            m_device->CreateShaderResourceView(m_depthAsColorTarget.Get(), &srvDesc, srvHandle);
            m_depthAsColorSrvHandle = srvHandle;
            srvHandle.Offset(1, m_srvDescriptorSize);
        }
    }

    // Copy descriptors fro CPU to GPU
    {
        m_device->CopyDescriptorsSimple(2, m_srvHeapGpu->GetCPUDescriptorHandleForHeapStart(),
            m_srvHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
}

void D3D12HelloTriangle::UpdateGPUDescriptors()
{

}

// Load the sample assets.
void D3D12HelloTriangle::LoadAssets()
{
    // Create an empty root signature.
    {
        CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
        ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
    }

    // Create the pipeline state, which includes compiling and loading shaders.
    // 0 is for standard depth; 1 is for disabling early Z.
    for (int i = 0; i < 2; i++)
    {
        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
        // Enable better shader debugging with the graphics debugging tools.
        UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlags = 0;
#endif

        D3D_SHADER_MACRO macros[] = { "DISABLE_EARLY_Z", i == 0 ? "0" : "1" , nullptr, nullptr };

        ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), macros, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, nullptr));
        ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), macros, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, nullptr));

        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };

        // Describe and create the graphics pipeline state object (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
        psoDesc.pRootSignature = m_rootSignature.Get();
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        psoDesc.DepthStencilState.DepthEnable = TRUE;
        psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 2;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.RTVFormats[1] = DXGI_FORMAT_R32_FLOAT;
        psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        psoDesc.SampleDesc.Count = 1;
        ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState[i])));
    }

    // Create the command list.
    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), m_pipelineState[0].Get(), IID_PPV_ARGS(&m_commandList)));

    // Command lists are created in the recording state, but there is nothing
    // to record yet. The main loop expects it to be closed, so close it now.
    ThrowIfFailed(m_commandList->Close());

    // Create the vertex buffer.
    {

        m_vertices.clear();
        if (1)
        {
            // Random vertices
            constexpr int numVertices = 10000;
            std::mt19937 gen(123);
            std::uniform_real_distribution<float> dist(0, 1);
            for (int i = 0; i < numVertices; i++)
            {
                m_vertices.push_back({
                    { 2 * dist(gen) - 1, (2 * dist(gen) - 1) * m_aspectRatio, dist(gen) },
                    { dist(gen) , dist(gen) , dist(gen), 1.0f } });
            }
        }
        else
        {
            // Single triangles
            m_vertices.push_back({ { 0.0f, 0.25f * m_aspectRatio, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } });
            m_vertices.push_back({ { 0.25f, -0.25f * m_aspectRatio, 1.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } });
            m_vertices.push_back({ { -0.25f, -0.25f * m_aspectRatio, 1.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } });
        }

        const UINT vertexBufferSize = (UINT)m_vertices.size() * sizeof(Vertex);

        // Note: using upload heaps to transfer static data like vert buffers is not 
        // recommended. Every time the GPU needs it, the upload heap will be marshalled 
        // over. Please read up on Default Heap usage. An upload heap is used here for 
        // code simplicity and because there are very few verts to actually transfer.
        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_vertexBuffer)));

        // Copy the triangle data to the vertex buffer.
        UINT8* pVertexDataBegin;
        CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
        ThrowIfFailed(m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
        memcpy(pVertexDataBegin, m_vertices.data(), vertexBufferSize);
        m_vertexBuffer->Unmap(0, nullptr);

        // Initialize the vertex buffer view.
        m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
        m_vertexBufferView.StrideInBytes = sizeof(Vertex);
        m_vertexBufferView.SizeInBytes = vertexBufferSize;
    }

    // Create synchronization objects and wait until assets have been uploaded to the GPU.
    {
        ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
        m_fenceValue = 1;

        // Create an event handle to use for frame synchronization.
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (m_fenceEvent == nullptr)
        {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }

        // Wait for the command list to execute; we are reusing the same command 
        // list in our main loop but for now, we just want to wait for setup to 
        // complete before continuing.
        WaitForPreviousFrame();
    }
}

// Load the sample assets for comparing textures
void D3D12HelloTriangle::LoadAssetsCompare()
{
    // Create a root signature with one descriptor table.
    {
        D3D12_DESCRIPTOR_RANGE srvRange;
        srvRange.BaseShaderRegister = 0;
        srvRange.NumDescriptors = 2;
        srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.RegisterSpace = 0;

        D3D12_ROOT_PARAMETER rootParameter;
        rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameter.DescriptorTable.NumDescriptorRanges = 1;
        rootParameter.DescriptorTable.pDescriptorRanges = &srvRange;
        rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init(1, &rootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
        ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignatureCompare)));
    }

    // Create the pipeline state, which includes compiling and loading shaders.
    {
        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
        // Enable better shader debugging with the graphics debugging tools.
        UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlags = 0;
#endif
        ComPtr<ID3DBlob> errorBlob;
        ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr, "VSMainCompare", "vs_5_0", compileFlags, 0, &vertexShader, nullptr));
        HRESULT hr = D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr, "PSMainCompare", "ps_5_0", compileFlags, 0, &pixelShader, &errorBlob);
        if (FAILED(hr))
        {
            if (errorBlob)
            {
                const char* errorMsg = reinterpret_cast<const char*>(errorBlob->GetBufferPointer());
                OutputDebugStringA(errorMsg);
                printf("%s\n", errorMsg);
            }
            else
            {
                printf("No error blob.\n");
            }
        }

        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        // Describe and create the graphics pipeline state object (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
        psoDesc.pRootSignature = m_rootSignatureCompare.Get();
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count = 1;
        ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineStateCompare)));
    }

    // Create the vertex buffer.
    {
        // Define the geometry for a triangle.
        VertexCompare quadVertices[] =
        {
            { { -1.0f, 1.0f, 0.0f } },
            { { 1.0f, 1.0f, 0.0f } },
            { { -1.0f, -1.0f, 0.0f } },
            { { 1.0f, -1.0f, 0.0f } },
        };

        const UINT vertexBufferSize = sizeof(quadVertices);

        // Note: using upload heaps to transfer static data like vert buffers is not 
        // recommended. Every time the GPU needs it, the upload heap will be marshalled 
        // over. Please read up on Default Heap usage. An upload heap is used here for 
        // code simplicity and because there are very few verts to actually transfer.
        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_vertexBufferCompare)));

        // Copy the triangle data to the vertex buffer.
        UINT8* pVertexDataBegin;
        CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
        ThrowIfFailed(m_vertexBufferCompare->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
        memcpy(pVertexDataBegin, quadVertices, sizeof(quadVertices));
        m_vertexBufferCompare->Unmap(0, nullptr);

        // Initialize the vertex buffer view.
        m_vertexBufferViewCompare.BufferLocation = m_vertexBufferCompare->GetGPUVirtualAddress();
        m_vertexBufferViewCompare.StrideInBytes = sizeof(VertexCompare);
        m_vertexBufferViewCompare.SizeInBytes = vertexBufferSize;
    }
}

// Update frame-based values.
void D3D12HelloTriangle::OnUpdate()
{
}

// Render the scene.
void D3D12HelloTriangle::OnRender()
{
    // Make the comparison with early Z enabled
    {
        PopulateCommandList(0);

        ExecuteAndWait();

        PopulateCommandListCompare();

        ExecuteAndWait();

        static bool saved0 = false;
        // Save the comparison to disk.
        {
            if (!saved0)
            {
                /*CopyRenderTargetToReadbackBuffer(m_frameIndex);
                SaveRenderTargetReadbackBufferAsImage(L"output.png");
                CopyDepthAsColorTargetToReadbackBuffer();
                SaveDepthAsColorTargetReadbackBufferAsImage(L"output2.png");*/
                CopyDepthCompareTargetToReadbackBuffer();
                SaveRenderTargetReadbackBufferAsImage(L"compare_early_z_enabled.png");
                saved0 = true;
            }
        }
    }


    // Make the comparison with early Z disabled
    {
        PopulateCommandList(1);

        ExecuteAndWait();

        PopulateCommandListCompare();

        ExecuteAndWait();

        // Save the comparison to disk.
        static bool saved1 = false;
        {
            if (!saved1)
            {
                /*CopyRenderTargetToReadbackBuffer(m_frameIndex);
                SaveRenderTargetReadbackBufferAsImage(L"output.png");
                CopyDepthAsColorTargetToReadbackBuffer();
                SaveDepthAsColorTargetReadbackBufferAsImage(L"output2.png");*/
                CopyDepthCompareTargetToReadbackBuffer();
                SaveRenderTargetReadbackBufferAsImage(L"compare_early_z_disabled.png");
                saved1 = true;
            }
        }
    }

    // Present the frame.
    ThrowIfFailed(m_swapChain->Present(1, 0));


    WaitForPreviousFrame();
}

void D3D12HelloTriangle::OnDestroy()
{
    // Ensure that the GPU is no longer referencing resources that are about to be
    // cleaned up by the destructor.
    WaitForPreviousFrame();

    CloseHandle(m_fenceEvent);
}

void D3D12HelloTriangle::PopulateCommandList(int i)
{
    // Command list allocators can only be reset when the associated 
    // command lists have finished execution on the GPU; apps should use 
    // fences to determine GPU execution progress.
    ThrowIfFailed(m_commandAllocator->Reset());

    // However, when ExecuteCommandList() is called on a particular command 
    // list, that command list can then be reset at any time and must be before 
    // re-recording.
    ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState[i].Get()));

    // Set necessary state.
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);

    // Indicate that the back buffer will be used as a render target.
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvhandles[2] = { m_rtvHandles[m_frameIndex], m_depthAsColorRtvHandle };
    m_commandList->OMSetRenderTargets(2, rtvhandles, FALSE, &m_dsvHandle);

    // Record commands.
    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_commandList->ClearRenderTargetView(m_rtvHandles[m_frameIndex], clearColor, 0, nullptr);
    m_commandList->ClearRenderTargetView(m_depthAsColorRtvHandle, clearColor, 0, nullptr);
    m_commandList->ClearDepthStencilView(m_dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    m_commandList->DrawInstanced((UINT)m_vertices.size(), 1, 0, 0);

    // Indicate that the back buffer will now be used to present.
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    ThrowIfFailed(m_commandList->Close());
}

void D3D12HelloTriangle::PopulateCommandListCompare()
{
    // Command list allocators can only be reset when the associated 
    // command lists have finished execution on the GPU; apps should use 
    // fences to determine GPU execution progress.
    ThrowIfFailed(m_commandAllocator->Reset());

    // However, when ExecuteCommandList() is called on a particular command 
    // list, that command list can then be reset at any time and must be before 
    // re-recording.
    ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineStateCompare.Get()));

    // Set necessary state.
    m_commandList->SetGraphicsRootSignature(m_rootSignatureCompare.Get());
    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);

    // Set the descriptor heap/table
    m_commandList->SetDescriptorHeaps(1, m_srvHeapGpu.GetAddressOf());
    m_commandList->SetGraphicsRootDescriptorTable(0, m_srvHeapGpu->GetGPUDescriptorHandleForHeapStart());

    // Indicate that the depth as color buffer will be used as a shader resource.
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        m_depthAsColorTarget.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

    // Indicate that the depth will be used as a shader resource.
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        m_depthTarget.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

    m_commandList->OMSetRenderTargets(1, &m_depthCompareRtvHandle, FALSE, nullptr);

    // Record commands.
    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_commandList->ClearRenderTargetView(m_depthCompareRtvHandle, clearColor, 0, nullptr);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferViewCompare);
    m_commandList->DrawInstanced(4, 1, 0, 0);

    // Indicate that the depth as color buffer will be used as a render target.
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        m_depthAsColorTarget.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));

    // Indicate that the depth will be used as a depth buffer.
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        m_depthTarget.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE));

    ThrowIfFailed(m_commandList->Close());
}

void D3D12HelloTriangle::ExecuteAndWait()
{
    {
        // Execute the command list.
        ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
        m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
    }

    {
        // Signal and increment the fence value.
        const UINT64 fence = m_fenceValue;
        ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
        m_fenceValue++;

        // Wait until the previous frame is finished.
        if (m_fence->GetCompletedValue() < fence)
        {
            ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
            WaitForSingleObject(m_fenceEvent, INFINITE);
        }
    }
}

void D3D12HelloTriangle::WaitForPreviousFrame()
{
    // WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
    // This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
    // sample illustrates how to use fences for efficient resource usage and to
    // maximize GPU utilization.

    // Signal and increment the fence value.
    const UINT64 fence = m_fenceValue;
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
    m_fenceValue++;

    // Wait until the previous frame is finished.
    if (m_fence->GetCompletedValue() < fence)
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}