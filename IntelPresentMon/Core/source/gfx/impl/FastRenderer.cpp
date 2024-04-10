// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: MIT
#include "FastRenderer.h"
#include <d3dcompiler.h>
#include <Core/source/infra/log/Logging.h>

#pragma comment(lib, "d3dcompiler")

namespace p2c::gfx::impl
{
	FastRenderer::FastRenderer(ID3D11Device& device, const DimensionsI& dims)
        :
        dims{ dims }
	{
        // pixel shader
        {
            ComPtr<ID3DBlob> pBlob;
            if (auto hr = D3DReadFileToBlob(L"Shaders\\Line_PS.cso", &pBlob); FAILED(hr))
            {
                p2clog.hr(hr).note(L"Failure reading Shaders\\Line_PS.cso").commit();
            }
            if (auto hr = device.CreatePixelShader(
                pBlob->GetBufferPointer(),
                pBlob->GetBufferSize(),
                nullptr,
                &pPixelShader
            ); FAILED(hr))
            {
                p2clog.hr(hr).commit();
            }
        }
        // vertex shader & input layout
        {
            ComPtr<ID3DBlob> pBlob;
            if (auto hr = D3DReadFileToBlob(L"Shaders\\Line_VS.cso", &pBlob); FAILED(hr))
            {
                p2clog.hr(hr).note(L"Failure reading Shaders\\Line_VS.cso").commit();
            }
            if (auto hr = device.CreateVertexShader(
                pBlob->GetBufferPointer(),
                pBlob->GetBufferSize(),
                nullptr,
                &pVertexShader
            ); FAILED(hr))
            {
                p2clog.hr(hr).commit();
            }

            constexpr D3D11_INPUT_ELEMENT_DESC input_descs[] = {
                {
                    .SemanticName = "POSITION",
                    .SemanticIndex = 0,
                    .Format = DXGI_FORMAT::DXGI_FORMAT_R32G32_FLOAT,
                    .InputSlot = 0,
                    .AlignedByteOffset = 0,
                    .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA,
                    .InstanceDataStepRate = 0
                },
                {
                    .SemanticName = "COLOR",
                    .SemanticIndex = 0,
                    .Format = DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT,
                    .InputSlot = 0,
                    .AlignedByteOffset = 16,
                    .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA,
                    .InstanceDataStepRate = 0
                }
            };
            if (auto hr = device.CreateInputLayout(
                input_descs,
                (UINT)std::size(input_descs),
                pBlob->GetBufferPointer(),
                (UINT)pBlob->GetBufferSize(),
                &pInputLayout
            ); FAILED(hr))
            {
                p2clog.hr(hr).commit();
            }
        }
        // geometry buffers (vtx + idx)
        MakeVertexBuffer(device);
        MakeIndexBuffer(device);
        // rasterizer (backface cull etc.)
        {
            auto rasterDesc = CD3D11_RASTERIZER_DESC{ CD3D11_DEFAULT{} };
            rasterDesc.ScissorEnable = TRUE;
            if (auto hr = device.CreateRasterizerState(&rasterDesc, &pRasterizer); FAILED(hr))
            {
                p2clog.hr(hr).commit();
            }
        }
        // rasterizer AA
        {
            auto rasterDesc = CD3D11_RASTERIZER_DESC{ CD3D11_DEFAULT{} };
            rasterDesc.ScissorEnable = TRUE;
            rasterDesc.AntialiasedLineEnable = TRUE;
            if (auto hr = device.CreateRasterizerState(&rasterDesc, &pRasterizerAnti); FAILED(hr))
            {
                p2clog.hr(hr).commit();
            }
        }
        // depth stencil
        {
            D3D11_DEPTH_STENCIL_DESC dsDesc = CD3D11_DEPTH_STENCIL_DESC{ CD3D11_DEFAULT{} };
            dsDesc.DepthEnable = FALSE;
            dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            if (auto hr = device.CreateDepthStencilState(&dsDesc, &pDepthStencil); FAILED(hr))
            {
                p2clog.hr(hr).commit();
            }
        }
        // blend
        {
            auto blendDesc = CD3D11_BLEND_DESC{ CD3D11_DEFAULT{} };
            auto& rt = blendDesc.RenderTarget[0];
            rt.BlendEnable = true;
            rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
            rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            rt.BlendOp = D3D11_BLEND_OP_ADD;
            rt.SrcBlendAlpha = D3D11_BLEND_ONE;
            rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
            rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
            if (auto hr = device.CreateBlendState(&blendDesc, &pBlender); FAILED(hr))
            {
                p2clog.hr(hr).commit();
            }
        }
	}

	void FastRenderer::StartFrame(ID3D11DeviceContext& context)
	{
        if (vertexLimitExceededAmount || indexLimitExceededAmount) {
            p2clog.warn(L"Limit on vtx or idx buffer exceeded last frame and not resized, resetting tripwire").commit();
            vertexLimitExceededAmount.reset();
            indexLimitExceededAmount.reset();
        }
        if (!batches.empty())
        {
            p2clog.warn(L"batches not empty").commit();
            context.Unmap(pIndexBuffer.Get(), 0);
        }
        if (vertexMapping || indexMapping)
        {
            p2clog.warn(L"mapping already open").commit();
            context.Unmap(pVertexBuffer.Get(), 0);
        }
        if (chainSize)
        {
            p2clog.warn(L"chain not closed").commit();
            chainSize = {};
        }
        if (activeBatch)
        {
            p2clog.warn(L"batch was already active").commit();
            activeBatch = std::nullopt;
        }

        vertexMapping.emplace();
        if (auto hr = context.Map(pVertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &*vertexMapping); FAILED(hr))
        {
            p2clog.hr(hr).commit();
        }
        indexMapping.emplace();
        if (auto hr = context.Map(pIndexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &*indexMapping); FAILED(hr))
        {
            p2clog.hr(hr).commit();
        }
	}

	void FastRenderer::EndFrame(ID3D11DeviceContext& context)
	{
        if (activeBatch)
        {
            p2clog.warn(L"Batch still open").commit();
            activeBatch = std::nullopt;
        }
        if (!vertexMapping || !indexMapping)
        {
            p2clog.note(L"Vertex or index mapping not open").commit();
        }
        if (chainSize)
        {
            p2clog.warn(L"chain not closed").commit();
            chainSize = {};
        }
        if (activeBatch)
        {
            p2clog.warn(L"batch was still active").commit();
            activeBatch = std::nullopt;
        }

        context.Unmap(pVertexBuffer.Get(), 0);
        context.Unmap(pIndexBuffer.Get(), 0);
        vertexMapping = std::nullopt;
        indexMapping = std::nullopt;

        if (batches.empty())
        {
            return;
        }

        context.PSSetShader(pPixelShader.Get(), nullptr, 0);
        context.VSSetShader(pVertexShader.Get(), nullptr, 0);
        context.IASetInputLayout(pInputLayout.Get());
        // vertex buffer
        {
            const UINT stride = sizeof(Point);
            const UINT offset = 0;
            context.IASetVertexBuffers(0, 1, pVertexBuffer.GetAddressOf(), &stride, &offset);
        }
        context.IASetIndexBuffer(pIndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
        context.OMSetDepthStencilState(pDepthStencil.Get(), 0);
        context.OMSetBlendState(pBlender.Get(), nullptr, 0xFFFFFFFF);
        // viewport
        {
            auto& actual = dims.GetActual();
            const D3D11_VIEWPORT vp = {
                .TopLeftX = 0.f,
                .TopLeftY = 0.f,
                .Width = float(actual.width),
                .Height = float(actual.height),
                .MinDepth = 0.0f,
                .MaxDepth = 1.0f,
            };
            context.RSSetViewports(1, &vp);
        }

        for (const auto& b : batches)
        {
            {
                const D3D11_RECT scissor{
                    .left = b.clip.left,
                    .top = b.clip.top,
                    .right = b.clip.right,
                    .bottom = b.clip.bottom,
                };
                context.RSSetScissorRects(1, &scissor);
            }
            context.IASetPrimitiveTopology(b.topology);
            if (b.useAntiAlias)
            {
                context.RSSetState(pRasterizerAnti.Get());
            }
            else
            {
                context.RSSetState(pRasterizer.Get());
            }
            context.DrawIndexed(b.indexEnd - b.indexBegin, b.indexBegin, b.vertexOffset);
        }

        nVertices = 0;
        nIndices = 0;
        batches.clear();
	}

    void FastRenderer::WriteVertex(Vec2 pt)
    {
        if (nVertices < vertexBufferSize) {
            reinterpret_cast<Point*>(vertexMapping->pData)[nVertices++] = { ConvertPoint(pt), chainColor };
        }
        else  {
            if (!vertexLimitExceededAmount) {
                vertexLimitExceededAmount = 0;
                p2clog.warn(L"Vertex limit exceeded!").commit();
            }
            *vertexLimitExceededAmount += 1;
        }
    }

    void FastRenderer::WriteIndex(UINT32 i)
    {
#ifdef _DEBUG
        if (i < activeBatch->vertexOffset || i > nVertices)
        {
            p2clog.warn(std::format(L"writing index out of range: {}", i)).commit();
        }
#endif
        if (nIndices < indexBufferSize) {
            reinterpret_cast<UINT16*>(indexMapping->pData)[nIndices++] = UINT16(i - activeBatch->vertexOffset);
        }
        else {
            if (!indexLimitExceededAmount) {
                indexLimitExceededAmount = 0;
                p2clog.warn(L"Index limit exceeded!").commit();
            }
            *indexLimitExceededAmount += 1;
        }
    }

    void FastRenderer::MakeVertexBuffer(ID3D11Device& device)
    {
        D3D11_BUFFER_DESC bd = {};
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bd.MiscFlags = 0u;
        bd.ByteWidth = vertexBufferSize * sizeof(Point);
        bd.StructureByteStride = sizeof(Point);
        if (auto hr = device.CreateBuffer(&bd, nullptr, &pVertexBuffer); FAILED(hr))
        {
            p2clog.hr(hr).commit();
        }
    }

    void FastRenderer::MakeIndexBuffer(ID3D11Device& device)
    {
        D3D11_BUFFER_DESC ibd = {};
        ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        ibd.Usage = D3D11_USAGE_DYNAMIC;
        ibd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ibd.MiscFlags = 0u;
        ibd.ByteWidth = indexBufferSize * sizeof(UINT16);
        ibd.StructureByteStride = sizeof(UINT16);
        if (auto hr = device.CreateBuffer(&ibd, nullptr, &pIndexBuffer); FAILED(hr))
        {
            p2clog.hr(hr).commit();
        }
    }

    // TODO: consider how to handle float -> int conversion in scissor
    void FastRenderer::StartBatch(const RectI& clip, D3D11_PRIMITIVE_TOPOLOGY topology, bool aa)
    {
#ifdef _DEBUG
        if (activeBatch)
        {
            p2clog.warn(L"Batch still open").commit();
        }
#endif
        activeBatch = {
            .topology = topology,
            .indexBegin = nIndices,
            .vertexOffset = nVertices,
            .clip = clip,
            .useAntiAlias = aa
        };
    }

	void FastRenderer::StartLineBatch(const RectI& clip, bool anti)
	{
        StartBatch(clip, D3D11_PRIMITIVE_TOPOLOGY::D3D11_PRIMITIVE_TOPOLOGY_LINELIST, anti);
	}

	void FastRenderer::StartTriangleBatch(const RectI& clip)
	{
        StartBatch(clip, D3D11_PRIMITIVE_TOPOLOGY::D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false);
	}

	void FastRenderer::EndBatch()
	{
#ifdef _DEBUG
        if (!activeBatch)
        {
            p2clog.warn(L"Batch not open").commit();
        }
        if (chainSize)
        {
            p2clog.warn(L"Chain not closed").commit();
        }
#endif
        activeBatch->indexEnd = nIndices;
        batches.push_back(*activeBatch);
        activeBatch = std::nullopt;
	}

    void FastRenderer::StartLine(Vec2 pt, Color c)
    {
#ifdef _DEBUG
        if (!activeBatch)
        {
            p2clog.warn(L"Batch not open").commit();
        }
        if (chainSize)
        {
            p2clog.warn(L"Chain not closed").commit();
        }
        if (activeBatch->topology != D3D11_PRIMITIVE_TOPOLOGY_LINELIST)
        {
            p2clog.note(L"Not line topology").commit();
        }
#endif
        chainColor = c;
        chainSize = 1;
        WriteIndex(nVertices);
        WriteVertex(pt);
    }
    
    void FastRenderer::AddVertex(Vec2 pt)
    {
#ifdef _DEBUG
        if (!activeBatch)
        {
            p2clog.warn(L"Batch not open").commit();
        }
        if (!chainSize)
        {
            p2clog.warn(L"Chain not open").commit();
        }
        if (activeBatch->topology != D3D11_PRIMITIVE_TOPOLOGY_LINELIST)
        {
            p2clog.note(L"Not line topology").commit();
        }
#endif
        chainSize++;
        WriteIndex(nVertices);
        WriteIndex(nVertices);
        WriteVertex(pt);
    }

    void FastRenderer::EndLine(Vec2 pt)
    {
#ifdef _DEBUG
        if (!activeBatch)
        {
            p2clog.warn(L"Batch not open").commit();
        }
        if (chainSize < 1)
        {
            p2clog.warn(L"Chain not long enough to realize geometry").commit();
        }
        if (activeBatch->topology != D3D11_PRIMITIVE_TOPOLOGY_LINELIST)
        {
            p2clog.note(L"Not line topology").commit();
        }
#endif
        WriteIndex(nVertices);
        WriteVertex(pt);
        chainSize = 0;
    }

    void FastRenderer::StartPeakChain(Vec2 top, Vec2 bottom, Color c)
    {
#ifdef _DEBUG
        if (!activeBatch)
        {
            p2clog.warn(L"Batch not open").commit();
        }
        if (chainSize)
        {
            p2clog.warn(L"Chain not closed").commit();
        }
        if (activeBatch->topology != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST)
        {
            p2clog.note(L"Not triangle topology").commit();
        }
#endif
        chainColor = c;
        chainSize = 1;
        WriteVertex(top);
        WriteVertex(bottom);
    }

    void FastRenderer::AddPeakChain(Vec2 top, Vec2 bottom)
    {
#ifdef _DEBUG
        if (!activeBatch)
        {
            p2clog.warn(L"Batch not open").commit();
        }
        if (!chainSize)
        {
            p2clog.warn(L"Chain not open").commit();
        }
        if (activeBatch->topology != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST)
        {
            p2clog.note(L"Not triangle topology").commit();
        }
#endif
        chainSize++;
        WriteVertex(top);
        WriteVertex(bottom);
        WriteIndex(nVertices - 4);
        WriteIndex(nVertices - 3);
        WriteIndex(nVertices - 2);
        WriteIndex(nVertices - 3);
        WriteIndex(nVertices - 1);
        WriteIndex(nVertices - 2);
    }

    void FastRenderer::EndPeakChain(Vec2 top, Vec2 bottom)
    {
#ifdef _DEBUG
        if (!activeBatch)
        {
            p2clog.warn(L"Batch not open").commit();
        }
        if (chainSize < 1)
        {
            p2clog.warn(L"Chain not long enough to realize geometry").commit();
        }
        if (activeBatch->topology != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST)
        {
            p2clog.note(L"Not triangle topology").commit();
        }
#endif
        WriteVertex(top);
        WriteVertex(bottom);
        WriteIndex(nVertices - 4);
        WriteIndex(nVertices - 3);
        WriteIndex(nVertices - 2);
        WriteIndex(nVertices - 3);
        WriteIndex(nVertices - 1);
        WriteIndex(nVertices - 2);
        chainSize = 0;
    }

    void FastRenderer::EmitRect(const Rect& rect, Color c)
    {
#ifdef _DEBUG
        if (!activeBatch)
        {
            p2clog.warn(L"Batch not open").commit();
        }
        if (chainSize > 0)
        {
            p2clog.warn(L"Chain not closed").commit();
        }
        if (activeBatch->topology != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST)
        {
            p2clog.note(L"Not triangle topology").commit();
        }
#endif
        chainColor = c;
        WriteVertex(rect.GetTopLeft());
        WriteVertex(rect.GetTopRight());
        WriteVertex(rect.GetBottomLeft());
        WriteVertex(rect.GetBottomRight());
        WriteIndex(nVertices - 4);
        WriteIndex(nVertices - 3);
        WriteIndex(nVertices - 2);
        WriteIndex(nVertices - 2);
        WriteIndex(nVertices - 3);
        WriteIndex(nVertices - 1);
    }

    void FastRenderer::EmitLineRect(const Rect& rect, Color c)
    {
#ifdef _DEBUG
        if (!activeBatch)
        {
            p2clog.warn(L"Batch not open").commit();
        }
        if (chainSize > 0)
        {
            p2clog.warn(L"Chain not closed").commit();
        }
        if (activeBatch->topology != D3D11_PRIMITIVE_TOPOLOGY_LINELIST)
        {
            p2clog.note(L"Not line topology").commit();
        }
#endif
        chainColor = c;
        WriteVertex(rect.GetTopLeft());
        WriteVertex(rect.GetTopRight());
        WriteVertex(rect.GetBottomLeft());
        WriteVertex(rect.GetBottomRight());
        WriteIndex(nVertices - 4);
        WriteIndex(nVertices - 3);
        WriteIndex(nVertices - 2);
        WriteIndex(nVertices - 1);
        WriteIndex(nVertices - 4);
        WriteIndex(nVertices - 2);
        WriteIndex(nVertices - 3);
        WriteIndex(nVertices - 1);
    }

    void FastRenderer::EmitLineRectTop(const Rect& rect, Color c)
    {
#ifdef _DEBUG
        if (!activeBatch)
        {
            p2clog.warn(L"Batch not open").commit();
        }
        if (chainSize > 0)
        {
            p2clog.warn(L"Chain not closed").commit();
        }
        if (activeBatch->topology != D3D11_PRIMITIVE_TOPOLOGY_LINELIST)
        {
            p2clog.note(L"Not line topology").commit();
        }
#endif
        chainColor = c;
        WriteVertex(rect.GetTopLeft());
        WriteVertex(rect.GetTopRight());
        WriteVertex(rect.GetBottomLeft());
        WriteVertex(rect.GetBottomRight());
        WriteIndex(nVertices - 4);
        WriteIndex(nVertices - 3);
        WriteIndex(nVertices - 4);
        WriteIndex(nVertices - 2);
        WriteIndex(nVertices - 3);
        WriteIndex(nVertices - 1);
    }

    void FastRenderer::Resize(const DimensionsI& dims_)
    {
        dims = dims_;
    }

    void FastRenderer::ResizeGeometryBuffersIfNecessary(ID3D11Device& device)
    {
        if (vertexLimitExceededAmount) {
            const auto newSize = UINT(float(vertexBufferSize + *vertexLimitExceededAmount) * 2.f);
            p2clog.info(std::format(L"Vertex limit exceeded last frame, resizing from {} to {}",
                vertexBufferSize, newSize)).commit();
            vertexBufferSize = newSize;
            MakeVertexBuffer(device);
            vertexLimitExceededAmount.reset();
        }
        if (indexLimitExceededAmount) {
            const auto newSize = UINT(float(indexBufferSize + *indexLimitExceededAmount) * 2.f);
            p2clog.info(std::format(L"Index limit exceeded last frame, resizing from {} to {}",
                indexBufferSize, newSize)).commit();
            indexBufferSize = newSize;
            MakeIndexBuffer(device);
            indexLimitExceededAmount.reset();
        }
    }

    Vec2 FastRenderer::ConvertPoint(Vec2 pt) const
    {
        auto& half = dims.GetHalf();
        auto& inv = dims.GetInverseHalf();
        return { (pt.x - half.width) * inv.width, (half.height - pt.y) * inv.height };
    }
}