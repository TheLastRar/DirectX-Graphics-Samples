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

#pragma once

#include "DXSample.h"
#include <vector>

using namespace DirectX;

// Note that while ComPtr is used to manage the lifetime of resources on the CPU,
// it has no understanding of the lifetime of resources on the GPU. Apps must account
// for the GPU lifetime of resources to avoid destroying objects that may still be
// referenced by the GPU.
// An example of this can be found in the class method: OnDestroy().
using Microsoft::WRL::ComPtr;

class D3D12HelloTriangle : public DXSample
{
public:
    D3D12HelloTriangle(UINT width, UINT height, std::wstring name);

    virtual void OnInit();
    virtual void OnUpdate();
    virtual void OnRender();
    virtual void OnDestroy();

private:
    static const UINT FrameCount = 2;

    struct Vertex
    {
        XMFLOAT3 position;
        XMFLOAT4 color;
    };

    struct VertexCompare
    {
        XMFLOAT3 position;
    };

    std::vector<Vertex> m_vertices;

    // Pipeline objects.
    CD3DX12_VIEWPORT m_viewport;
    CD3DX12_RECT m_scissorRect;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
    ComPtr<ID3D12Resource> m_depthTarget;
    ComPtr<ID3D12Resource> m_depthAsColorTarget;
    ComPtr<ID3D12Resource> m_depthCompareTarget;
    ComPtr<ID3D12Resource> m_renderTargetReadbackBuffer;
    ComPtr<ID3D12Resource> m_depthAsColorTargetReadbackBuffer;
    ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12RootSignature> m_rootSignatureCompare;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    ComPtr<ID3D12DescriptorHeap> m_srvHeapGpu;
    ComPtr<ID3D12PipelineState> m_pipelineState[2]; // 0 for early Z; 1 for early Z disabled.
    ComPtr<ID3D12PipelineState> m_pipelineStateCompare;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    UINT m_rtvDescriptorSize;
    UINT m_dsvDescriptorSize;
    UINT m_srvDescriptorSize;

    CD3DX12_CPU_DESCRIPTOR_HANDLE m_rtvHandles[FrameCount];
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_dsvHandle;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_depthAsColorRtvHandle;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_depthSrvHandle;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_depthAsColorSrvHandle;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_depthCompareRtvHandle;

    // App resources.
    ComPtr<ID3D12Resource> m_vertexBuffer;
    ComPtr<ID3D12Resource> m_vertexBufferCompare;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferViewCompare;

    // Synchronization objects.
    UINT m_frameIndex;
    HANDLE m_fenceEvent;
    ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceValue;

    void LoadPipeline();
    void LoadAssets();
    void LoadAssetsCompare();
    void PopulateCommandList(int i);
    void PopulateCommandListCompare();
    void ExecuteAndWait();
    void WaitForPreviousFrame();
    void UpdateGPUDescriptors();

    void CreateReadbackBuffer(const D3D12_RESOURCE_DESC& texture, ComPtr<ID3D12Resource>& readbackBuffer);
    void CreateRenderTargetReadbackBuffer();
    void CreateDepthAsColorTargetReadbackBuffer();

    void CopyToReadbackBuffer(ComPtr<ID3D12Resource> texture, ComPtr<ID3D12Resource> readbackBuffer,
        D3D12_RESOURCE_STATES currState);
    void CopyRenderTargetToReadbackBuffer(UINT i);
    void CopyDepthCompareTargetToReadbackBuffer();
    void CopyDepthAsColorTargetToReadbackBuffer();

    void SaveReadbackBufferAsImage(
        const D3D12_RESOURCE_DESC& textureDesc, ComPtr<ID3D12Resource> readbackBuffer,
        const std::wstring& filename);
    void SaveRenderTargetReadbackBufferAsImage(const std::wstring& filename);
    void SaveDepthAsColorTargetReadbackBufferAsImage(const std::wstring& filename);
};
