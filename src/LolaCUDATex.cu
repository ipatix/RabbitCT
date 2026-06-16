#include "rabbitCt.h"

#include <cassert>
#include <cstdint>
#include <memory>
#include <thrust/device_vector.h>

struct cuda_array
{
public:
    cuda_array(size_t width, size_t height)
        : width(width), height(height)
    {
        auto desc = cudaCreateChannelDesc(32, 0, 0, 0, cudaChannelFormatKindFloat);
        auto a = cudaMallocArray(&array, &desc, width, height);
        if (a != cudaSuccess)
            throw std::runtime_error("cudaMallocArray failed: " + std::to_string(a));
    }
    ~cuda_array()
    {
        (void)cudaFreeArray(array);
    }

    void assign(const float *data, size_t count)
    {
        assert(width * height == count);

        auto a = cudaMemcpy2DToArray(
            array,
            0,
            0,
            data,
            width * sizeof(*data),
            width * sizeof(*data),
            height,
            cudaMemcpyHostToDevice
        );
        if (a != cudaSuccess)
            throw std::runtime_error("cudaMemcpy2DToArray failed: " + std::to_string(a));
    }

    operator cudaArray_t()
    {
        return array;
    }

private:
    cudaArray_t array;
    float width, height;
};

struct cuda_texture
{
public:
    cuda_texture(cudaArray_t array)
    {
        cudaResourceDesc resDesc{};
        resDesc.resType = cudaResourceTypeArray;
        resDesc.res.array.array = array;

        cudaTextureDesc texDesc = {
            .addressMode = {
                cudaAddressModeBorder,
                cudaAddressModeBorder,
            },
            .filterMode = cudaFilterModeLinear,
            .readMode = cudaReadModeElementType,
            .normalizedCoords = 0,
        };

        auto a = cudaCreateTextureObject(&texObj, &resDesc, &texDesc, nullptr);
        if (a != cudaSuccess)
            throw std::runtime_error("cudaCreateTextureObject failed: " + std::to_string(a));
    }

    ~cuda_texture()
    {
        (void)cudaDestroyTextureObject(texObj);
    }

    operator cudaTextureObject_t()
    {
        return texObj;
    }

private:
    cudaTextureObject_t texObj;
};

struct device_texture
{
    device_texture(size_t width, size_t height)
        : arr(width, height), tex(arr)
    {
    }

    cuda_array arr;
    cuda_texture tex;
};

static std::unique_ptr<thrust::device_vector<float>> gpuMatrices;
static std::unique_ptr<device_texture> gpuImages;
static std::unique_ptr<thrust::device_vector<float>> gpuVoxels;

extern "C" int lolaCudaTexPrepare(RabbitCtGlobalData *rcgd)
{
    gpuMatrices = std::make_unique<thrust::device_vector<float>>();
    gpuImages = std::make_unique<device_texture>(rcgd->imageWidth, rcgd->imageHeight);
    gpuVoxels = std::make_unique<thrust::device_vector<float>>(rcgd->problemSize * rcgd->problemSize * rcgd->problemSize, 0.0f);
    return 1;
}

extern "C" int lolaCudaTexFinish(RabbitCtGlobalData *rcgd)
{
    thrust::copy(gpuVoxels->begin(), gpuVoxels->end(), rcgd->volumeData);
    (void)cudaDeviceSynchronize();
    gpuMatrices.reset();
    gpuImages.reset();
    gpuVoxels.reset();
    return 1;
}

__global__ static void kernel(
        float *matrix,
        cudaTextureObject_t texture,
        uint32_t imageWidth,
        uint32_t imageHeight,
        float *voxels,
        uint32_t voxelDim,
        float voxelSize,
        float O_Index)
{
    const uint32_t voxelIdx = blockIdx.x * blockDim.x + threadIdx.x;

    const uint32_t voxelIdxX = voxelIdx % voxelDim;
    const uint32_t voxelIdxY = (voxelIdx / voxelDim) % voxelDim;
    const uint32_t voxelIdxZ = voxelIdx / (voxelDim * voxelDim);

    const float x = O_Index + voxelIdxX * voxelSize;
    const float y = O_Index + voxelIdxY * voxelSize;
    const float z = O_Index + voxelIdxZ * voxelSize;

    const float w = matrix[2] * x + matrix[5] * y + matrix[8] * z + matrix[11];
    const float u = (matrix[0] * x + matrix[3] * y + matrix[6] * z + matrix[9]) / w;
    const float v = (matrix[1] * x + matrix[4] * y + matrix[7] * z + matrix[10]) / w;

    const float imgData = tex2D<float>(texture, u + 0.5f, v + 0.5f);

    const float voxelData = static_cast<float>(1.0f / (w * w) * imgData);
    float *voxel = &voxels[voxelIdxZ * voxelDim * voxelDim + voxelIdxY * voxelDim + voxelIdxX];
    *voxel += voxelData;
}

extern "C" int lolaCudaTexBackprojection(RabbitCtGlobalData *rcgd)
{
    const size_t matrixElemCount = 12;
    const size_t imageElemCount = rcgd->imageWidth * rcgd->imageHeight;

    const size_t voxelDim = rcgd->problemSize;

    const size_t numThreads = voxelDim * voxelDim * voxelDim;
    const size_t threadBlockSize = 256;
    const size_t numThreadBlocks= numThreads / threadBlockSize;

    // 1. Upload data to GPU
    gpuMatrices->resize(rcgd->numberOfProjections * matrixElemCount, 0.0);

    for (size_t i = 0; i < rcgd->numberOfProjections; i++) {
        std::vector<float> cpuMatrixSingle(matrixElemCount);
        for (size_t j = 0; j < cpuMatrixSingle.size(); j++)
            cpuMatrixSingle[j] = static_cast<float>(rcgd->projectionBuffer[i].matrix[j]);
        const auto gpuMatrixStart = gpuMatrices->begin() + (matrixElemCount * i);

        thrust::copy(cpuMatrixSingle.begin(), cpuMatrixSingle.end(), gpuMatrixStart);
    }

    // 2. Actually execute kernels
    for (size_t i = 0; i < rcgd->numberOfProjections; i++) {
        const float *cpuImageStart = rcgd->projectionBuffer[i].image;
        gpuImages->arr.assign(cpuImageStart, imageElemCount);

        kernel<<<numThreadBlocks, threadBlockSize>>>(
            thrust::raw_pointer_cast(&(*gpuMatrices)[matrixElemCount * i]),
            gpuImages->tex,
            rcgd->imageWidth,
            rcgd->imageHeight,
            thrust::raw_pointer_cast(&(*gpuVoxels)[0]),
            voxelDim,
            rcgd->voxelSize,
            rcgd->O_Index);
    }
    (void)cudaDeviceSynchronize();

    return 1;
}
