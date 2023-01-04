#include "viewer.h"

#include "test_files.h"

#include <cputex/d3d12.h>
#include <cputex/utility.h>
#include <gpufmt/dxgi.h>
#include <gpufmt/string.h>
#include <imgui.h>
#include <teximp/string.h>
#include <d3dcompiler.h>

Viewer::Viewer()
{
    for(int i = kDescriptorHeapIndexStart; i < kDescriptorHeapIndexStart + kDescriptorHeapIndexCount; ++i)
    {
        mAvailableDescriptorIndices.insert(i);
    }
}

Viewer::Viewer(std::filesystem::path baseDirectory)
    : Viewer()
{
    mBaseDirectory = std::move(baseDirectory);
}

void Viewer::prevFileFormat()
{
    mSelectedFileFormat = ((int)mSelectedFileFormat > 0) ? (teximp::FileFormat)((int)mSelectedFileFormat - 1) : (teximp::FileFormat)((int)teximp::FileFormat::Count - 1);
    mSelectedTestFile = 0;
    mSelectionChanged = true;
}

void Viewer::nextFileFormat()
{
    mSelectedFileFormat = (teximp::FileFormat)(((int)mSelectedFileFormat + 1) % (int)teximp::FileFormat::Count);
    mSelectedTestFile = 0;
    mSelectionChanged = true;
}

void Viewer::prevTestImage()
{
    const int fileCount = currentFileFormatTestImageCount();
    mSelectedTestFile = (mSelectedTestFile > 0) ? (mSelectedTestFile - 1) % fileCount : fileCount - 1;
    mSelectionChanged = true;
}

void Viewer::nextTestImage()
{
    mSelectedTestFile = (mSelectedTestFile + 1) % currentFileFormatTestImageCount();
    mSelectionChanged = true;
}

int Viewer::currentFileFormatTestImageCount()
{
    return (int)std::ssize(kTestFiles[(size_t)mSelectedFileFormat]);
}

bool Viewer::isLastTestImage()
{
    return mSelectedTestFile == currentFileFormatTestImageCount() - 1;
}

void Viewer::drawUI()
{

    if(mAutoMode)
    {
        const auto now = std::chrono::steady_clock::now();
        const auto delta = now - mLastAutoTime;

        if(delta > mAutoPauseDuration)
        {
            mLastAutoTime = now;

            if(isLastTestImage())
            {
                mSelectedTestFile = 0;
                nextFileFormat();
            }
            else
            {
                nextTestImage();
            }
        }
    }

    if(ImGui::Begin("Test"))
    {
        if(ImGui::Checkbox("Auto", &mAutoMode) && mAutoMode)
        {
            mLastAutoTime = std::chrono::steady_clock::now();
        }
        
        ImGui::SameLine();

        float autoPauseDuration = mAutoPauseDuration.count();
        if(ImGui::InputFloat("##AutoPauseDuration", &autoPauseDuration))
        {
            mAutoPauseDuration = std::chrono::duration<float>(autoPauseDuration);
        }

        ImGui::TextUnformatted("File Format");
        if(ImGui::Button("<##FileFormatDec"))
        {
            prevFileFormat();
        }
        ImGui::SameLine();
        if(ImGui::BeginCombo("##File Format", teximp::toString(mSelectedFileFormat).data()))
        {
            for(int i = 0; i < (int)teximp::FileFormat::Count; ++i)
            {
                const teximp::FileFormat fileFormat = (teximp::FileFormat)i;

                if(ImGui::Selectable(teximp::toString(fileFormat).data(), mSelectedFileFormat == fileFormat))
                {
                    mSelectedFileFormat = fileFormat;
                    mSelectedTestFile = 0;
                    mSelectionChanged = true;
                }
            }

            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if(ImGui::Button(">##FileFormatInc"))
        {
            nextFileFormat();
        }
    }

    ImGui::TextUnformatted("Test File");
    if(ImGui::Button("<##FileDec"))
    {
        prevTestImage();
    }
    ImGui::SameLine();
    if(ImGui::BeginCombo("##Test File", kTestFiles[(size_t)mSelectedFileFormat][mSelectedTestFile].data()))
    {
        std::span testFiles = kTestFiles[(size_t)mSelectedFileFormat];

        for(int i = 0; i < std::ssize(testFiles); ++i)
        {
            if(ImGui::Selectable(testFiles[i].data(), i == mSelectedTestFile))
            {
                mSelectedTestFile = i;
                mSelectionChanged = true;
            }
        }

        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if(ImGui::Button(">##FileInc"))
    {
        nextTestImage();
    }

    if(mTextureData.valid())
    {
        if(mTextureData.hasImportError())
        {
            ImGui::Text("Import error: %s", teximp::toString(mTextureData.importResult.importer->error()).data());
            ImGui::TextWrapped(mTextureData.importResult.importer->errorMessage().data());
        }
        else
        {
            ImGui::TextUnformatted("Texture successfully loaded!");
            ImGui::NewLine();

            const std::span textures = mTextureData.importResult.textureAllocator.getTextures();

            if(textures.size() > 1 &&
                ImGui::InputInt("Texture", &mTextureData.selectedTexture, 1, 2))
            {
                mTextureData.selectedTexture = std::clamp(mTextureData.selectedTexture, 0, (int)std::ssize(mTextureData.resources) - 1);
            }

            D3d12Resources& selectedResource = mTextureData.resources[mTextureData.selectedTexture];

            if(selectedResource.arrayCount > 1 &&
                ImGui::InputInt("Array Slice", &selectedResource.selectedArraySlice, 1, 2))
            {
                selectedResource.selectedArraySlice = std::clamp(selectedResource.selectedArraySlice, 0, selectedResource.arrayCount - 1);
            }

            if(selectedResource.faceCount > 1 &&
                ImGui::InputInt("Face", &selectedResource.selectedFace, 1, 2))
            {
                selectedResource.selectedFace = std::clamp(selectedResource.selectedFace, 0, selectedResource.faceCount - 1);
            }

            if(selectedResource.mipCount > 1 &&
                ImGui::InputInt("Mip", &selectedResource.selectedMip, 1, 2))
            {
                selectedResource.selectedMip = std::clamp(selectedResource.selectedMip, 0, selectedResource.mipCount - 1);
            }

            const cputex::Extent mipExtent = cputex::calculateMipExtent(selectedResource.extent, selectedResource.selectedMip);

            if(mipExtent.z > 1 &&
                ImGui::InputInt("Volume Slice", &selectedResource.selectedVolumeSlice, 1, 2))
            {
                selectedResource.selectedVolumeSlice = std::clamp(selectedResource.selectedVolumeSlice, 0, mipExtent.z - 1);
            }

            for(const auto& texture : textures)
            {
                ImGui::Separator();
                ImGui::TextUnformatted(gpufmt::toString(texture.format()).data());

                gpufmt::dxgi::FormatConversion conversionResult = gpufmt::dxgi::translateFormat(texture.format());

                if(conversionResult && conversionResult.exact)
                {
                    ImGui::TextUnformatted(gpufmt::toString(conversionResult.exact.value()).data());
                }
                else
                {
                    ImGui::TextUnformatted("No valid dxgi format");
                }

                if(texture.extent().z == 1)
                {
                    ImGui::Text("%d x %d", texture.extent().x, texture.extent().y);
                }
                else
                {
                    ImGui::Text("%d x %d x %d", texture.extent().x, texture.extent().y, texture.extent().z);
                }
            }
        }
    }

    ImGui::End();

    if(!mTextureData.resources.empty() && mTextureData.resources[mTextureData.selectedTexture].d3d12Texture)
    {
        const std::span cpuTextures = mTextureData.importResult.textureAllocator.getTextures();

        const cputex::TextureView cpuTextureView = cpuTextures[mTextureData.selectedTexture];
        const D3d12Resources& d3d12Resources = mTextureData.resources[mTextureData.selectedTexture];

        const ImVec2 displaySize = ImGui::GetIO().DisplaySize;

        const ImVec2 min((displaySize.x - cpuTextureView.extent().x) * 0.5f, (displaySize.y - cpuTextureView.extent().y) * 0.5f);
        const ImVec2 max(min.x + cpuTextureView.extent().x, min.y + cpuTextureView.extent().y);

        const D3d12Srv& selectedSrv = d3d12Resources.srv;
        //ImGui::GetBackgroundDrawList()->AddImage((ImTextureID)selectedSrv.gpuDescriptorHandle.ptr, min, max);
    }
}

void Viewer::renderUpdate(ID3D12Device* d3dDevice, ID3D12GraphicsCommandList* d3dCommandList, ID3D12DescriptorHeap* d3dSrvDescHeap)
{
    if(mFrame == 0)
    {
        createRootSignature(d3dDevice);
        createD3d12Meshes(d3dDevice, d3dCommandList, d3dSrvDescHeap);
        createD3d12PipelineStates(d3dDevice);
    }

    ++mFrame;

    auto newEnd = std::remove_if(mFreeQueue.begin(), mFreeQueue.end(), [frame = mFrame](const D3d12Resources& resources)
        {
            return frame - resources.lastUsedFrame > 3;
        });

    for(auto itr = newEnd; itr != mFreeQueue.end(); ++itr)
    {
        if(itr->srv.descriptorIndex < 0) { continue; }
        mAvailableDescriptorIndices.insert(itr->srv.descriptorIndex);
    }

    mFreeQueue.erase(newEnd, mFreeQueue.end());

    if(!mSelectionChanged) { return; }

    mSelectionChanged = false;
    const auto filePath = mBaseDirectory / kTestFiles[(size_t)mSelectedFileFormat][mSelectedTestFile];

    for(auto& resource : mTextureData.resources)
    {
        resource.lastUsedFrame = mFrame;
    }

    mFreeQueue.insert(mFreeQueue.end(),
        std::move_iterator(mTextureData.resources.begin()),
        std::move_iterator(mTextureData.resources.end()));

    mTextureData = {};
    mTextureData.importResult = teximp::importTexture(filePath);

    if(mTextureData.importResult.importer->error() != teximp::TextureImportError::None) { return; }

    createD3d12Textures(d3dDevice, d3dCommandList, d3dSrvDescHeap);
}

void Viewer::renderPass(ID3D12Device* d3dDevice, ID3D12GraphicsCommandList* d3dCommandList, ID3D12DescriptorHeap* srvDescriptorHeap)
{
    if(mTextureData.resources.empty())
    {
        return;
    }

    const auto& selectedTextureResources = mTextureData.resources[mTextureData.selectedTexture];

    if(selectedTextureResources.d3d12Texture == nullptr)
    {
        return;
    }

    d3dCommandList->SetGraphicsRootSignature(mRootSignature.Get());
    d3dCommandList->SetDescriptorHeaps(1, &srvDescriptorHeap);

    const auto& currentTexture = mTextureData.importResult.textureAllocator.getTextures()[mTextureData.selectedTexture];
    
    ShaderVariation::Value shaderVariation = ShaderVariation::Texture2d;

    if(currentTexture.dimension() == cputex::TextureDimension::Texture1D &&
       currentTexture.arraySize() == 1)
    {
        shaderVariation = ShaderVariation::Texture1d;
    }
    else if(currentTexture.dimension() == cputex::TextureDimension::Texture1D &&
        currentTexture.arraySize() > 1)
    {
        shaderVariation = ShaderVariation::Texture1dArray;
    }
    else if(currentTexture.dimension() == cputex::TextureDimension::Texture2D &&
        currentTexture.arraySize() == 1)
    {
        shaderVariation = ShaderVariation::Texture2d;
    }
    else if(currentTexture.dimension() == cputex::TextureDimension::Texture2D &&
        currentTexture.arraySize() > 1)
    {
        shaderVariation = ShaderVariation::Texture2dArray;
    }
    else if(currentTexture.dimension() == cputex::TextureDimension::TextureCube &&
        currentTexture.arraySize() == 1)
    {
        shaderVariation = ShaderVariation::TextureCube;
    }
    else if(currentTexture.dimension() == cputex::TextureDimension::TextureCube &&
        currentTexture.arraySize() > 1)
    {
        shaderVariation = ShaderVariation::TextureCubeArray;
    }
    else if(currentTexture.dimension() == cputex::TextureDimension::Texture3D)
    {
        shaderVariation = ShaderVariation::Texture3d;
    }

    d3dCommandList->SetPipelineState(mPipelineStates[shaderVariation].Get());

    struct ShaderConstants
    {
        float screenAspect;
        float textureAspect;
        uint32_t arraySlice;
        uint32_t face;
        uint32_t mip;
        float volumeW;
        uint32_t pointSample;
    } shaderConstants;

    ImVec2 screenSize = ImGui::GetIO().DisplaySize;

    const auto& selectedCpuTexture = mTextureData.importResult.textureAllocator.getTextures()[mTextureData.selectedTexture];
    
    const cputex::Extent mipExtent = cputex::calculateMipExtent(selectedTextureResources.extent, selectedTextureResources.selectedMip);

    shaderConstants.screenAspect = screenSize.x / screenSize.y;
    shaderConstants.textureAspect = (float)selectedCpuTexture.extent().x / (float)selectedCpuTexture.extent().y;
    shaderConstants.arraySlice = selectedTextureResources.selectedArraySlice;
    shaderConstants.face = selectedTextureResources.selectedFace;
    shaderConstants.mip = selectedTextureResources.selectedMip;
    shaderConstants.volumeW = ((float)selectedTextureResources.selectedVolumeSlice + 0.5f) / (float)mipExtent.z;
    shaderConstants.pointSample = 1;

    D3D12_VIEWPORT viewport = {};
    viewport.Width = screenSize.x;
    viewport.Height = screenSize.y;

    d3dCommandList->RSSetViewports(1, &viewport);

    D3D12_RECT scissorRect = {};
    scissorRect.right = screenSize.x;
    scissorRect.bottom = screenSize.y;

    d3dCommandList->RSSetScissorRects(1, &scissorRect);

    d3dCommandList->SetGraphicsRoot32BitConstants(0, sizeof(ShaderConstants) / 4, &shaderConstants, 0);

    D3D12_GPU_DESCRIPTOR_HANDLE texture_handle = {};
    texture_handle.ptr = mTextureData.resources[mTextureData.selectedTexture].srv.gpuDescriptorHandle.ptr;
    d3dCommandList->SetGraphicsRootDescriptorTable(1, texture_handle);

    D3D12_INDEX_BUFFER_VIEW ibv;
    ibv.BufferLocation = mIndexBuffer->GetGPUVirtualAddress();
    ibv.Format = DXGI_FORMAT_R16_UINT;
    ibv.SizeInBytes = sizeof(uint16_t) * 6;

    d3dCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    d3dCommandList->IASetIndexBuffer(&ibv);

    d3dCommandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
}

void Viewer::createRootSignature(ID3D12Device* d3dDevice)
{
    D3D12_DESCRIPTOR_RANGE descRange = {};
    descRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descRange.NumDescriptors = 1;
    descRange.BaseShaderRegister = 0;
    descRange.RegisterSpace = 0;
    descRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER param[2] = {};

    param[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    param[0].Constants.ShaderRegister = 0;
    param[0].Constants.RegisterSpace = 0;
    param[0].Constants.Num32BitValues = 16;
    param[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    param[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param[1].DescriptorTable.NumDescriptorRanges = 1;
    param[1].DescriptorTable.pDescriptorRanges = &descRange;
    param[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Bilinear sampling is required by default. Set 'io.Fonts->Flags |= ImFontAtlasFlags_NoBakedLines' or 'style.AntiAliasedLinesUseTex = false' to allow point/nearest sampling.
    std::array staticSamplers = {
        D3D12_STATIC_SAMPLER_DESC{
            .Filter = D3D12_FILTER_MIN_MAG_MIP_POINT,
            .AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            .AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            .AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            .MipLODBias = 0.f,
            .MaxAnisotropy = 0,
            .ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS,
            .BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
            .MinLOD = 0.f,
            .MaxLOD = 1000.f,
            .ShaderRegister = 0,
            .RegisterSpace = 0,
            .ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL},
        D3D12_STATIC_SAMPLER_DESC{
            .Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
            .AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            .AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            .AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            .MipLODBias = 0.f,
            .MaxAnisotropy = 0,
            .ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS,
            .BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
            .MinLOD = 0.f,
            .MaxLOD = 1000.f,
            .ShaderRegister = 1,
            .RegisterSpace = 0,
            .ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL}
    };


    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = _countof(param);
    desc.pParameters = param;
    desc.NumStaticSamplers = staticSamplers.size();
    desc.pStaticSamplers = staticSamplers.data();
    desc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    Microsoft::WRL::ComPtr<ID3DBlob> blob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errorBlob);
    if(FAILED(hr))
    {
        OutputDebugString((LPCSTR)errorBlob->GetBufferPointer());
        return;
    }

    hr = d3dDevice->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&mRootSignature));
}

void Viewer::createD3d12Textures(ID3D12Device* d3dDevice, ID3D12GraphicsCommandList* d3dCommandList, ID3D12DescriptorHeap* d3dSrvDescHeap)
{
    mTextureData.resources.resize(mTextureData.importResult.textureAllocator.getTextures().size());

    for(int i = 0; i < std::ssize(mTextureData.resources); ++i)
    {
        cputex::d3d12::TextureParams d3d12TextureParams = {};
        d3d12TextureParams.placedResource = false;
        d3d12TextureParams.committedParams.heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        d3d12TextureParams.committedParams.heapProperties.CreationNodeMask = 0;
        d3d12TextureParams.committedParams.heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        d3d12TextureParams.committedParams.heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
        d3d12TextureParams.committedParams.heapProperties.VisibleNodeMask = 0;

        cputex::d3d12::UploadBufferParams d3d12UploadBufferParams = {};
        d3d12UploadBufferParams.placedResource = false;
        d3d12UploadBufferParams.committedParams.heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        d3d12UploadBufferParams.committedParams.heapProperties.CreationNodeMask = 0;
        d3d12UploadBufferParams.committedParams.heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        d3d12UploadBufferParams.committedParams.heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
        d3d12UploadBufferParams.committedParams.heapProperties.VisibleNodeMask = 0;

        cputex::TextureView textureView = mTextureData.importResult.textureAllocator.getTextures()[i];
        auto createResult = cputex::d3d12::createTextureAndUpload(d3dDevice, d3dCommandList, textureView, d3d12TextureParams, d3d12UploadBufferParams);

        if(!createResult) { continue; }

        D3d12Resources& resources = mTextureData.resources[i];
        resources.d3d12Texture = createResult->textureResource;
        resources.d3d12UploadBuffer = createResult->uploadResource;
        resources.extent = textureView.extent();
        resources.arrayCount = textureView.arraySize();
        resources.faceCount = textureView.faces();
        resources.mipCount = textureView.mips();

        // The format must be valid because the texture was created successfully
        auto formatConversionResult = gpufmt::dxgi::translateFormat(textureView.format());

        const UINT descriptorHandleIncrementSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        cputex::d3d12::ResourceViewOptions options = {};
        
        resources.srv.descriptorIndex = *mAvailableDescriptorIndices.begin();
        resources.srv.gpuDescriptorHandle = d3dSrvDescHeap->GetGPUDescriptorHandleForHeapStart();
        resources.srv.gpuDescriptorHandle.ptr += descriptorHandleIncrementSize * resources.srv.descriptorIndex;

        mAvailableDescriptorIndices.erase(mAvailableDescriptorIndices.begin());

        D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle = d3dSrvDescHeap->GetCPUDescriptorHandleForHeapStart();
        descriptorHandle.ptr += descriptorHandleIncrementSize * resources.srv.descriptorIndex;

        HRESULT hr = cputex::d3d12::createShaderResourceView(d3dDevice, textureView, createResult->textureResource.Get(), descriptorHandle, options);

        if(FAILED(hr))
        {
            mTextureData.resources.clear();
            return;
        }
    }
}

void Viewer::createD3d12Meshes(ID3D12Device* d3dDevice, ID3D12GraphicsCommandList* d3dCommandList, ID3D12DescriptorHeap* d3dSrvDescHeap)
{
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.Width = sizeof(uint16_t) * 6;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        D3D12_HEAP_PROPERTIES heapProps;
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 0;
        heapProps.VisibleNodeMask = 0;

        HRESULT hr = d3dDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mIndexBuffer));
    }

    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.Width = sizeof(uint16_t) * 6;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        D3D12_HEAP_PROPERTIES heapProps;
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 0;
        heapProps.VisibleNodeMask = 0;

        HRESULT hr = d3dDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mIndexUploadBuffer));
    }

    void* mappedBuffer;
    mIndexUploadBuffer->Map(0, nullptr, &mappedBuffer);

    std::span<uint16_t> indexWriteBuffer((uint16_t*)mappedBuffer, sizeof(uint16_t) * 6);
    indexWriteBuffer[0] = 0;
    indexWriteBuffer[1] = 3;
    indexWriteBuffer[2] = 1;
    indexWriteBuffer[3] = 1;
    indexWriteBuffer[4] = 3;
    indexWriteBuffer[5] = 2;

    mIndexUploadBuffer->Unmap(0, nullptr);

    d3dCommandList->CopyResource(mIndexBuffer.Get(), mIndexUploadBuffer.Get());
}

void Viewer::createD3d12PipelineStates(ID3D12Device* d3dDevice)
{
    std::array variationDefines = {
        "TEXTURE_1D"sv,
        "TEXTURE_1D_ARRAY"sv,
        "TEXTURE_2D"sv,
        "TEXTURE_2D_ARRAY"sv,
        "TEXTURE_CUBE"sv,
        "TEXTURE_CUBE_ARRAY"sv,
        "TEXTURE_3D"sv
    };

    static_assert(ShaderVariation::Count == variationDefines.size());

    for(int i = 0; i < std::ssize(variationDefines); ++i)
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.NodeMask = 1;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.pRootSignature = mRootSignature.Get();
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count = 1;
        psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        
        // Create the blending setup
        {
            D3D12_BLEND_DESC& desc = psoDesc.BlendState;
            desc.AlphaToCoverageEnable = false;
            desc.RenderTarget[0].BlendEnable = true;
            desc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
            desc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
            desc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
            desc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            desc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            desc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        }

        // Create the rasterizer state
        {
            D3D12_RASTERIZER_DESC& desc = psoDesc.RasterizerState;
            desc.FillMode = D3D12_FILL_MODE_SOLID;
            desc.CullMode = D3D12_CULL_MODE_NONE;
            desc.FrontCounterClockwise = FALSE;
            desc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
            desc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
            desc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
            desc.DepthClipEnable = true;
            desc.MultisampleEnable = FALSE;
            desc.AntialiasedLineEnable = FALSE;
            desc.ForcedSampleCount = 0;
            desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
        }

        // Create depth-stencil State
        {
            D3D12_DEPTH_STENCIL_DESC& desc = psoDesc.DepthStencilState;
            desc.DepthEnable = false;
            desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            desc.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            desc.StencilEnable = false;
            desc.FrontFace.StencilFailOp = desc.FrontFace.StencilDepthFailOp = desc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
            desc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            desc.BackFace = desc.FrontFace;
        }

        psoDesc.InputLayout.pInputElementDescs = nullptr;
        psoDesc.InputLayout.NumElements = 0;

        std::array shaderMacros =
        {
            D3D_SHADER_MACRO{.Name = variationDefines[i].data(), .Definition = nullptr },
            D3D_SHADER_MACRO{.Name = nullptr, .Definition = nullptr }
        };

        Microsoft::WRL::ComPtr<ID3DBlob> vsCodeBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> errorMsgBlob;
        HRESULT hr = D3DCompileFromFile(L"../source/viewer/image.hlsl", shaderMacros.data(), nullptr, "vsMain", "vs_5_0", 0, 0, &vsCodeBlob, &errorMsgBlob);

        if(FAILED(hr))
        {
            OutputDebugString((LPCSTR)errorMsgBlob->GetBufferPointer());
            return;
        }

        Microsoft::WRL::ComPtr<ID3DBlob> psCodeBlob;
        hr = D3DCompileFromFile(L"../source/viewer/image.hlsl", shaderMacros.data(), nullptr, "psMain", "ps_5_0", 0, 0, &psCodeBlob, &errorMsgBlob);

        if(FAILED(hr))
        {
            OutputDebugString((LPCSTR)errorMsgBlob->GetBufferPointer());
            return;
        }

        psoDesc.VS.pShaderBytecode = vsCodeBlob->GetBufferPointer();
        psoDesc.VS.BytecodeLength = vsCodeBlob->GetBufferSize();

        psoDesc.PS.pShaderBytecode = psCodeBlob->GetBufferPointer();
        psoDesc.PS.BytecodeLength = psCodeBlob->GetBufferSize();

        d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPipelineStates[i]));
    }
}
