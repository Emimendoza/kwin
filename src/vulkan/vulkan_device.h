/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2023 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once
#include "kwin_export.h"
#include "vulkan_include.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QQueue>
#include <QStack>
#include <QVector>
#include <memory>
#include <optional>

namespace KWin
{

class VulkanTexture;
class GraphicsBuffer;

class KWIN_EXPORT VulkanDevice : public QObject
{
    Q_OBJECT
public:
    // Used to track ownership of resources/synchronization objects
    typedef uint64_t VulkanDeviceHandle;
    static constexpr VulkanDeviceHandle ALL = 0;

    explicit VulkanDevice(vk::Instance instance, vk::PhysicalDevice physicalDevice, vk::Device logicalDevice, uint32_t queueFamilyIndex, vk::Queue queue,
                          std::optional<dev_t> primaryNode, std::optional<dev_t> renderNode);
    VulkanDevice(VulkanDevice &&other) noexcept;
    VulkanDevice(const VulkanDevice &) = delete;
    ~VulkanDevice() override;

    std::shared_ptr<VulkanTexture> importClientBuffer(GraphicsBuffer *buffer);
    std::optional<VulkanTexture> importDmabuf(GraphicsBuffer *buffer) const;

    [[nodiscard]] QHash<uint32_t, QVector<uint64_t>> supportedFormats() const;
    [[nodiscard]] vk::Device logicalDevice() const;
    [[nodiscard]] uint32_t queueFamilyIndex() const;
    [[nodiscard]] vk::Queue renderQueue() const;

    [[nodiscard]] std::optional<dev_t> primaryNode() const;
    [[nodiscard]] std::optional<dev_t> renderNode() const;

    [[nodiscard]] vk::UniqueCommandBuffer allocateCommandBuffer();
    [[nodiscard]] vk::UniqueDeviceMemory allocateMemory(vk::Buffer buffer, vk::MemoryPropertyFlags memoryProperties) const;
    [[nodiscard]] vk::UniqueDeviceMemory allocateMemory(vk::Image image, vk::MemoryPropertyFlags memoryProperties) const;

    /**
     * Submit a command buffer to the queue and wait for it to finish
     * @param cmd the command buffer to submit
     * @param handle the handle to associate the command buffer with
     * @tparam wait if true, the command buffer will wait for the corresponding semaphore(s) to be signaled before executing
     * @details if handle is ALL, the command buffer is submitted so that it runs after all previously submitted command buffers
     * and before any subsequent command buffers, regardless of the handle they belong to
     */
    template<bool wait = true>
    [[nodiscard]] bool submitCommandBufferBlocking(vk::CommandBuffer cmd, VulkanDeviceHandle handle = ALL);

    /**
     * Submit a command buffer to the queue
     * @param cmd the command buffer to submit
     * @param handle the handle to associate the command buffer with
     * @tparam wait if true, the command buffer will wait for the corresponding semaphore(s) to be signaled before executing
     * @tparam signal if true, the command buffer will signal the corresponding semaphore(s) when it has finished executing
     * @details if handle is ALL, the command buffer is submitted so that it runs after all previously submitted command buffers
     * and before any subsequent command buffers, regardless of the handle they belong to
     * @details note that this function is non-blocking, the wait parameter only affects the command buffer itself
     */
    template<bool wait = true, bool signal = true>
    [[nodiscard]] bool submitCommandBuffer(vk::CommandBuffer cmd, VulkanDeviceHandle handle = ALL);

    /**
     * Wait for count number of fences
     * @param handle the handle to which the fences belong to
     * @param count number of fences to wait for
     * @details If handle is ALL, wait for fences belonging to all buffers, implies count = 0
     * @details If count is 0, wait for all fences, if count is greater than the number of fences, it is treated as if count = 0
     */
    [[nodiscard]] bool waitFence(VulkanDeviceHandle handle = ALL, size_t count = 0);

    /**
     * Waits for all fences of all handles to be signaled
     */
    [[nodiscard]] bool waitAllFences();

    /**
     * Grants a unique handle to associate resources with
     */
    [[nodiscard]] VulkanDeviceHandle acquireHandle();

    /**
     * Releases a handle and all associated resources
     * @param handle the handle to release
     * @returns true if the handle was released, false if the handle was invalid or unsuccessfully released
     * @details releasing a handle will cause it to wait for all associated resources to be freed before returning
     */
    [[nodiscard]] bool releaseHandle(VulkanDeviceHandle handle);

private:
    [[nodiscard]] bool waitFence(const vk::Fence &fence);
    [[nodiscard]] QHash<uint32_t, QVector<uint64_t>> queryFormats(vk::ImageUsageFlags flags) const;
    [[nodiscard]] std::optional<uint32_t> findMemoryType(uint32_t typeBits, vk::MemoryPropertyFlags memoryPropertyFlags) const;

    [[nodiscard]] std::optional<vk::Semaphore> acquireSemaphore();
    [[nodiscard]] std::optional<vk::Fence> acquireFence();

    vk::PhysicalDevice m_physical;
    vk::Device m_logical;
    uint32_t m_queueFamilyIndex;
    vk::Queue m_queue;
    std::optional<dev_t> m_primaryNode;
    std::optional<dev_t> m_renderNode;
    QHash<uint32_t, QVector<uint64_t>> m_formats;
    vk::PhysicalDeviceMemoryProperties m_memoryProperties;
    vk::UniqueCommandPool m_commandPool;

    vk::DispatchLoaderDynamic m_loader;
    QHash<GraphicsBuffer *, std::shared_ptr<VulkanTexture>> m_importedTextures;

    VulkanDeviceHandle m_nextHandle;
    QHash<VulkanDeviceHandle, std::vector<vk::Semaphore>> m_semaphores;
    QHash<VulkanDeviceHandle, QQueue<vk::Fence>> m_fences;
    QHash<vk::Fence, std::vector<vk::Semaphore>> m_fenceSemaphores;
    QStack<vk::Semaphore> m_freeSemaphores;
    QStack<vk::Fence> m_freeFences;
};

}
