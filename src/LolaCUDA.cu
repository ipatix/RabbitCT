#include "rabbitCt.h"

#include <cassert>
#include <cstdint>
#include <memory>
#include <thrust/device_vector.h>

static std::unique_ptr<thrust::device_vector<float>> gpuMatrices;
static std::unique_ptr<thrust::device_vector<float>> gpuImages;
static std::unique_ptr<thrust::device_vector<float>> gpuVoxels;

extern "C" int lolaCudaPrepare(RabbitCtGlobalData *rcgd)
{
    gpuMatrices = std::make_unique<thrust::device_vector<float>>();
    gpuImages = std::make_unique<thrust::device_vector<float>>();
    gpuVoxels = std::make_unique<thrust::device_vector<float>>(rcgd->problemSize * rcgd->problemSize * rcgd->problemSize, 0.0f);
    return 1;
}

extern "C" int lolaCudaFinish(RabbitCtGlobalData *rcgd)
{
    thrust::copy(gpuVoxels->begin(), gpuVoxels->end(), rcgd->volumeData);
    cudaDeviceSynchronize();
    gpuMatrices.reset();
    gpuImages.reset();
    gpuVoxels.reset();
    return 1;
}

__global__ static void kernel(
        float *matrix,
        float *image,
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

    const int32_t imgCoordX0 = static_cast<int32_t>(u);
    const int32_t imgCoordY0 = static_cast<int32_t>(v);
    const int32_t imgCoordX1 = imgCoordX0 + 1;
    const int32_t imgCoordY1 = imgCoordY0 + 1;

    const float alpha = u - static_cast<float>(imgCoordX0);
    const float beta = v - static_cast<float>(imgCoordY0);

    float imgDataX0Y0 = 0.0f, imgDataX0Y1 = 0.0f, imgDataX1Y0 = 0.0f, imgDataX1Y1 = 0.0f;
    if (imgCoordX0 >= 0 && imgCoordX0 < imageWidth) {
        if (imgCoordY0 >= 0 && imgCoordY0 < imageHeight)
            imgDataX0Y0 = image[imgCoordY0 * imageWidth + imgCoordX0];
        if (imgCoordY1 >= 0 && imgCoordY1 < imageHeight)
            imgDataX0Y1 = image[imgCoordY1 * imageWidth + imgCoordX0];
    }
    if (imgCoordX1 >= 0 && imgCoordX1 < imageWidth) {
        if (imgCoordY0 >= 0 && imgCoordY0 < imageHeight)
            imgDataX1Y0 = image[imgCoordY0 * imageWidth + imgCoordX1];
        if (imgCoordY1 >= 0 && imgCoordY1 < imageHeight)
            imgDataX1Y1 = image[imgCoordY1 * imageWidth + imgCoordX1];
    }

    const float imgDataY0 = imgDataX0Y0 + alpha * (imgDataX1Y0 - imgDataX0Y0);
    const float imgDataY1 = imgDataX0Y1 + alpha * (imgDataX1Y1 - imgDataX0Y1);
    const float imgData = imgDataY0 + beta * (imgDataY1 - imgDataY0);

    const float voxelData = static_cast<float>(1.0 / (w * w) * imgData);
    float *voxel = &voxels[voxelIdxZ * voxelDim * voxelDim + voxelIdxY * voxelDim + voxelIdxX];
    *voxel += voxelData;
}

extern "C" int lolaCudaBackprojection(RabbitCtGlobalData *rcgd)
{
    const size_t matrixElemCount = 12;
    const size_t imageElemCount = rcgd->imageWidth * rcgd->imageHeight;

    const size_t voxelDim = rcgd->problemSize;

    const size_t numThreads = voxelDim * voxelDim * voxelDim;
    const size_t threadBlockSize = 256;
    const size_t numThreadBlocks= numThreads / threadBlockSize;

    // 1. Upload data to GPU
    gpuMatrices->resize(rcgd->numberOfProjections * matrixElemCount, 0.0);
    gpuImages->resize(rcgd->numberOfProjections * imageElemCount, 0.0f);

    for (size_t i = 0; i < rcgd->numberOfProjections; i++) {
        std::vector<float> cpuMatrixSingle(matrixElemCount);
        for (size_t j = 0; j < cpuMatrixSingle.size(); j++)
            cpuMatrixSingle[j] = static_cast<float>(rcgd->projectionBuffer[i].matrix[j]);
        const auto gpuMatrixStart = gpuMatrices->begin() + (matrixElemCount * i);

        thrust::copy(cpuMatrixSingle.begin(), cpuMatrixSingle.end(), gpuMatrixStart);

        const float *cpuImageStart = rcgd->projectionBuffer[i].image;
        const float *cpuImageEnd = cpuImageStart + imageElemCount;
        const auto gpuImageStart = gpuImages->begin() + (imageElemCount * i);

        thrust::copy(cpuImageStart, cpuImageEnd, gpuImageStart);
    }

    // 2. Actually execute kernels
    for (size_t i = 0; i < rcgd->numberOfProjections; i++) {
        kernel<<<numThreadBlocks, threadBlockSize>>>(
            thrust::raw_pointer_cast(&(*gpuMatrices)[matrixElemCount * i]),
            thrust::raw_pointer_cast(&(*gpuImages)[imageElemCount * i]),
            rcgd->imageWidth,
            rcgd->imageHeight,
            thrust::raw_pointer_cast(&(*gpuVoxels)[0]),
            voxelDim,
            rcgd->voxelSize,
            rcgd->O_Index);
    }

    return 1;
}
