#pragma once

#include <d3d12.h>
#include <teximp/teximp.h>
#include <wrl/client.h>

#include <chrono>
#include <filesystem>
#include <set>
#include <vector>

constexpr int kDescriptorHeapIndexStart = 1;
constexpr int kDescriptorHeapIndexCount = 1023;

struct D3d12Srv
{
    D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptorHandle;
    D3D12_GPU_DESCRIPTOR_HANDLE gpuDescriptorHandle;
    int descriptorIndex = -1;
};

struct D3d12Resources
{
    int64_t lastUsedFrame = -1;
    Microsoft::WRL::ComPtr<ID3D12Resource> d3d12Texture;
    Microsoft::WRL::ComPtr<ID3D12Resource> d3d12UploadBuffer;
    D3d12Srv srv;

    cputex::Extent extent = {0, 0, 0};
    cputex::CountType arrayCount = 1;
    cputex::CountType faceCount = 1;
    cputex::CountType mipCount = 1;

    int selectedArraySlice = 0;
    int selectedFace = 0;
    int selectedMip = 0;
    int selectedVolumeSlice = 0;
};

struct TextureData
{
    teximp::TextureImportResult importResult;
    std::vector<D3d12Resources> resources;
    int selectedTexture = 0;
    
    bool valid() const { return importResult.importer != nullptr; }
    bool hasImportError() const { return valid() && importResult.importer->error() != teximp::TextureImportError::None; }
};

namespace ShaderVariation
{
enum Value
{
    Texture1d,
    Texture1dArray,
    Texture2d,
    Texture2dArray,
    TextureCube,
    TextureCubeArray,
    Texture3d,
    Count
};
}

class Viewer
{
public:
    std::filesystem::path mBaseDirectory;
    teximp::FileFormat mSelectedFileFormat = teximp::FileFormat::Bitmap;
    int mSelectedTestFile = 0;
    teximp::PreferredBackends mPreferredBackeds;

    std::vector<D3d12Resources> mFreeQueue;
    TextureData mTextureData;

    bool mAutoMode = false;
    std::chrono::duration<float> mAutoPauseDuration{0.0f};
    std::chrono::steady_clock::time_point mLastAutoTime;

    Viewer();
    Viewer(std::filesystem::path baseDirectory);

    void prevFileFormat();
    void nextFileFormat();
    void prevTestImage();
    void nextTestImage();
    int currentFileFormatTestImageCount();
    bool isLastTestImage();

    void drawUI();

    void renderUpdate(ID3D12Device* d3dDevice, ID3D12GraphicsCommandList* d3dCommandList, ID3D12DescriptorHeap* d3dSrvDescHeap);
    void renderPass(ID3D12Device* d3dDevice, ID3D12GraphicsCommandList* d3dCommandList, ID3D12DescriptorHeap* srvDescriptorHeap);

private:
    void createRootSignature(ID3D12Device* d3dDevice);
    void createD3d12Textures(ID3D12Device* d3dDevice, ID3D12GraphicsCommandList* d3dCommandList, ID3D12DescriptorHeap* d3dSrvDescHeap);
    void createD3d12Meshes(ID3D12Device* d3dDevice, ID3D12GraphicsCommandList* d3dCommandList, ID3D12DescriptorHeap* d3dSrvDescHeap);
    void createD3d12PipelineStates(ID3D12Device* d3dDevice);

    bool mSelectionChanged = true;
    int64_t mFrame = 0;
    std::set<int> mAvailableDescriptorIndices;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature;
    Microsoft::WRL::ComPtr<ID3D12Resource> mIndexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> mIndexUploadBuffer;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, ShaderVariation::Count> mPipelineStates;
};