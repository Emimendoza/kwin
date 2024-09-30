/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2023 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "vulkan_device.h"
#include "core/graphicsbuffer.h"
#include "utils/drm_format_helper.h"
#include "vulkan_texture.h"

#include <QDebug>
#include <sys/stat.h>
#include <vulkan/vulkan_hash.hpp>

namespace KWin
{

VulkanDevice::VulkanDevice(vk::Instance instance, vk::PhysicalDevice physicalDevice, vk::Device logicalDevice, uint32_t queueFamilyIndex, vk::Queue queue,
                           std::optional<dev_t> primaryNode, std::optional<dev_t> renderNode)
    : m_physical(physicalDevice)
    , m_logical(logicalDevice)
    , m_queueFamilyIndex(queueFamilyIndex)
    , m_queue(queue)
    , m_primaryNode(primaryNode)
    , m_renderNode(renderNode)
    , m_formats(queryFormats(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst))
    , m_memoryProperties(physicalDevice.getMemoryProperties())
    , m_loader(instance, vkGetInstanceProcAddr, logicalDevice)
    , m_nextHandle(ALL + 1)
    , m_fences{{ALL, {}}}
{
    auto [result, cmdPool] = m_logical.createCommandPoolUnique(vk::CommandPoolCreateInfo{
        vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        m_queueFamilyIndex,
    });
    if (result != vk::Result::eSuccess) {
        qCWarning(KWIN_VULKAN) << "creating a command pool failed:" << KWin::VKto_string(result);
        return;
    }
    m_commandPool = std::move(cmdPool);
}

VulkanDevice::VulkanDeviceHandle VulkanDevice::acquireHandle()
{
    m_semaphores[m_nextHandle] = {};
    m_fences[m_nextHandle] = {};
    return m_nextHandle++;
}

bool VulkanDevice::releaseHandle(const VulkanDeviceHandle handle)
{
    if (handle == ALL) {
        qCWarning(KWIN_VULKAN) << "can't release the ALL handle";
        return false;
    }
    if (!waitFence(handle)) {
        return false;
    }
    m_semaphores.remove(handle);
    m_fences.remove(handle);
    return true;
}

VulkanDevice::VulkanDevice(VulkanDevice &&other) noexcept
    : m_physical(other.m_physical)
    , m_logical(std::exchange(other.m_logical, {}))
    , m_queueFamilyIndex(other.m_queueFamilyIndex)
    , m_queue(other.m_queue)
    , m_primaryNode(other.m_primaryNode)
    , m_renderNode(other.m_renderNode)
    , m_formats(other.m_formats)
    , m_memoryProperties(other.m_memoryProperties)
    , m_commandPool(std::move(other.m_commandPool))
    , m_loader(std::move(other.m_loader))
    , m_importedTextures(other.m_importedTextures)
    , m_nextHandle(other.m_nextHandle)
    , m_semaphores(std::move(m_semaphores))
    , m_fences(std::move(other.m_fences))
    , m_fenceSemaphores(std::move(other.m_fenceSemaphores))
    , m_freeSemaphores(std::move(other.m_freeSemaphores))
    , m_freeFences(std::move(other.m_freeFences))
{
}

VulkanDevice::~VulkanDevice()
{
    if (m_queue) {
        (void)m_queue.waitIdle();
    }
    m_importedTextures.clear();
    m_commandPool.reset();
    if (m_logical) {
        vkDestroyDevice(m_logical, nullptr);
    }
}

std::shared_ptr<VulkanTexture> VulkanDevice::importClientBuffer(GraphicsBuffer *buffer)
{
    auto it = m_importedTextures.find(buffer);
    if (it != m_importedTextures.end()) {
        return it.value();
    }
    auto opt = importDmabuf(buffer);
    if (!opt) {
        return nullptr;
    }
    const auto ret = std::make_shared<VulkanTexture>(std::move(*opt));
    m_importedTextures[buffer] = ret;
    connect(buffer, &QObject::destroyed, this, [this, buffer]() {
        m_importedTextures.remove(buffer);
    });
    return ret;
}

/**
 * A dmabuf can have multiple planes with fds pointing to the same image,
 * so checking the number of planes isn't enough to know if it's disjoint
 */
static bool isDisjoint(const DmaBufAttributes &attributes)
{
    if (attributes.planeCount == 1) {
        return false;
    }
    struct stat stat1
    {
    };
    if (fstat(attributes.fd[0].get(), &stat1) != 0) {
        qCWarning(KWIN_VULKAN) << "failed to fstat dmabuf";
        return true;
    }
    for (int i = 1; i < attributes.planeCount; i++) {
        struct stat stati
        {
        };
        if (fstat(attributes.fd[i].get(), &stati) != 0) {
            qCWarning(KWIN_VULKAN) << "failed to fstat dmabuf";
            return false;
        }
        if (stat1.st_ino != stati.st_ino) {
            return true;
        }
    }
    return false;
}

std::optional<VulkanTexture> VulkanDevice::importDmabuf(GraphicsBuffer *buffer) const
{
    const auto attributes = buffer->dmabufAttributes();
    if (!attributes) {
        return std::nullopt;
    }
    const auto format = FormatInfo::get(attributes->format);
    if (!format) {
        return std::nullopt;
    }
    auto formatIt = m_formats.find(attributes->format);
    if (formatIt == m_formats.end() || !formatIt->contains(attributes->modifier)) {
        return std::nullopt;
    }
    std::array queueFamilyIndices{
        m_queueFamilyIndex,
    };
    std::vector<vk::SubresourceLayout> subLayouts;
    for (int i = 0; i < attributes->planeCount; i++) {
        subLayouts.emplace_back(attributes->offset[i], 0, attributes->pitch[i], 0, 0);
    }
    vk::ImageDrmFormatModifierExplicitCreateInfoEXT modifierCI{
        attributes->modifier,
        subLayouts,
    };
    vk::ExternalMemoryImageCreateInfo externalCI{
        vk::ExternalMemoryHandleTypeFlagBits::eDmaBufEXT,
        &modifierCI,
    };
    const bool disjoint = isDisjoint(*attributes);
    vk::ImageCreateInfo imageCI{
        disjoint ? vk::ImageCreateFlagBits::eDisjoint : vk::ImageCreateFlags(),
        vk::ImageType::e2D,
        format->vulkanFormat,
        vk::Extent3D(attributes->width, attributes->height, 1),
        1,
        1,
        vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eDrmFormatModifierEXT,
        // TODO add some hint for how the texture will actually be used
        vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment,
        vk::SharingMode::eExclusive,
        queueFamilyIndices,
        vk::ImageLayout::eUndefined,
        &externalCI,
    };
    auto [imageResult, image] = m_logical.createImageUnique(imageCI);
    if (imageResult != vk::Result::eSuccess) {
        qCWarning(KWIN_VULKAN) << "creating vulkan image failed!" << KWin::VKto_string(imageResult);
        return std::nullopt;
    }

    std::array<vk::BindImageMemoryInfo, 4> bindInfos;
    std::array<vk::BindImagePlaneMemoryInfo, 4> planeInfo;
    std::vector<vk::UniqueDeviceMemory> deviceMemory;
    const uint32_t memoryCount = disjoint ? attributes->planeCount : 1;
    for (uint32_t i = 0; i < memoryCount; i++) {
        const auto [memoryFdResult, memoryFdProperties] = m_logical.getMemoryFdPropertiesKHR(vk::ExternalMemoryHandleTypeFlagBits::eDmaBufEXT, attributes->fd[i].get(), m_loader);
        if (memoryFdResult != vk::Result::eSuccess) {
            qCWarning(KWIN_VULKAN) << "failed to get memory fd properties!" << KWin::VKto_string(memoryFdResult);
            return std::nullopt;
        }
        vk::ImageMemoryRequirementsInfo2 memRequirementsInfo{image.get()};
        vk::ImagePlaneMemoryRequirementsInfo planeRequirementsInfo;
        if (disjoint) {
            switch (i) {
            case 0:
                planeRequirementsInfo.setPlaneAspect(vk::ImageAspectFlagBits::eMemoryPlane0EXT);
                break;
            case 1:
                planeRequirementsInfo.setPlaneAspect(vk::ImageAspectFlagBits::eMemoryPlane1EXT);
                break;
            case 2:
                planeRequirementsInfo.setPlaneAspect(vk::ImageAspectFlagBits::eMemoryPlane2EXT);
                break;
            case 3:
                planeRequirementsInfo.setPlaneAspect(vk::ImageAspectFlagBits::eMemoryPlane3EXT);
                break;
            }
            memRequirementsInfo.setPNext(&planeRequirementsInfo);
        }
        const vk::MemoryRequirements2 memRequirements = m_logical.getImageMemoryRequirements2(memRequirementsInfo);
        const auto memoryIndex = findMemoryType(memRequirements.memoryRequirements.memoryTypeBits & memoryFdProperties.memoryTypeBits, {});
        if (!memoryIndex) {
            qCWarning(KWIN_VULKAN) << "couldn't find a suitable memory type";
            return std::nullopt;
        }

        vk::ImportMemoryFdInfoKHR importInfo(vk::ExternalMemoryHandleTypeFlagBits::eDmaBufEXT, attributes->fd[i].duplicate().take());
        vk::MemoryAllocateInfo memoryAI(memRequirements.memoryRequirements.size, memoryIndex.value(), &importInfo);
        auto [allocateResult, memory] = m_logical.allocateMemoryUnique(memoryAI);
        if (allocateResult != vk::Result::eSuccess) {
            return std::nullopt;
        }

        bindInfos[i] = vk::BindImageMemoryInfo{image.get(), memory.get(), 0};
        if (disjoint) {
            planeInfo[i] = vk::BindImagePlaneMemoryInfo{
                planeRequirementsInfo.planeAspect,
            };
            bindInfos[i].setPNext(&planeInfo[i]);
        }
        deviceMemory.push_back(std::move(memory));
    }
    const vk::Result bindResult = m_logical.bindImageMemory2(memoryCount, bindInfos.data());
    if (bindResult != vk::Result::eSuccess) {
        qCWarning(KWIN_VULKAN) << "failed to bind image to memory";
        return std::nullopt;
    }

    auto [imageViewResult, imageView] = m_logical.createImageViewUnique(vk::ImageViewCreateInfo{
        vk::ImageViewCreateFlags(),
        image.get(),
        vk::ImageViewType::e2D,
        format->vulkanFormat,
        vk::ComponentMapping{
            vk::ComponentSwizzle::eIdentity,
            vk::ComponentSwizzle::eIdentity,
            vk::ComponentSwizzle::eIdentity,
            vk::ComponentSwizzle::eIdentity,
        },
        vk::ImageSubresourceRange{
            vk::ImageAspectFlagBits::eColor,
            0, // base mipmap level
            1, // mitmap level count
            0, // base array layer
            1, // array layer count
        },
    });
    if (imageViewResult != vk::Result::eSuccess) {
        qCWarning(KWIN_VULKAN) << "couldn't create image view!" << KWin::VKto_string(imageViewResult);
        return std::nullopt;
    }

    return std::make_optional<VulkanTexture>(format->vulkanFormat, std::move(image), std::move(deviceMemory), std::move(imageView));
}

QHash<uint32_t, QVector<uint64_t>> VulkanDevice::queryFormats(const vk::ImageUsageFlags flags) const
{
    QHash<uint32_t, QVector<uint64_t>> ret;
    for (const uint32_t drmFormat : s_knownDrmFormats) {
        const auto info = FormatInfo::get(drmFormat).value();
        if (info.vulkanFormat == vk::Format::eUndefined) {
            continue;
        }
        vk::DrmFormatModifierPropertiesListEXT modifierInfos;
        vk::FormatProperties2 formatProps{
            {},
            &modifierInfos,
        };
        m_physical.getFormatProperties2(info.vulkanFormat, &formatProps);
        if (modifierInfos.drmFormatModifierCount == 0) {
            continue;
        }
        std::vector<vk::DrmFormatModifierPropertiesEXT> formatModifierProps(modifierInfos.drmFormatModifierCount);
        modifierInfos.pDrmFormatModifierProperties = formatModifierProps.data();
        m_physical.getFormatProperties2(info.vulkanFormat, &formatProps);

        for (const auto &props : formatModifierProps) {
            vk::PhysicalDeviceImageDrmFormatModifierInfoEXT modifierInfo{
                props.drmFormatModifier,
                vk::SharingMode::eExclusive,
            };
            vk::PhysicalDeviceExternalImageFormatInfo externalInfo{
                vk::ExternalMemoryHandleTypeFlagBits::eDmaBufEXT,
                &modifierInfo,
            };
            vk::PhysicalDeviceImageFormatInfo2 imageInfo{
                info.vulkanFormat,
                vk::ImageType::e2D,
                vk::ImageTiling::eDrmFormatModifierEXT,
                flags,
                vk::ImageCreateFlags(),
                &externalInfo,
            };
            vk::ExternalImageFormatProperties externalProps;
            vk::ImageFormatProperties2 imageProps{
                {},
                &externalProps,
            };

            const vk::Result result = m_physical.getImageFormatProperties2(&imageInfo, &imageProps);
            if (result == vk::Result::eErrorFormatNotSupported) {
                qCWarning(KWIN_VULKAN) << "unsupported format:" << KWin::VKto_string(info.vulkanFormat);
                continue;
            } else if (result != vk::Result::eSuccess) {
                qCWarning(KWIN_VULKAN) << "failed to get image format properties:" << KWin::VKto_string(result);
                continue;
            }
            if (!(externalProps.externalMemoryProperties.externalMemoryFeatures & vk::ExternalMemoryFeatureFlagBits::eImportable)) {
                qCWarning(KWIN_VULKAN) << "can't import format" << KWin::VKto_string(info.vulkanFormat);
                continue;
            }
            if (!(imageProps.imageFormatProperties.sampleCounts & vk::SampleCountFlagBits::e1)) {
                // just in case there's a format that always requires multi sampling, skip that
                continue;
            }
            if (imageProps.imageFormatProperties.maxExtent.width < 16384 || imageProps.imageFormatProperties.maxExtent.height < 16384) {
                // we don't want to support size constraints right now, so just require a size we're unlikely to exceed
                continue;
            }

            ret[drmFormat].push_back(props.drmFormatModifier);
        }
    }
    return ret;
}

std::optional<uint32_t> VulkanDevice::findMemoryType(uint32_t typeBits, vk::MemoryPropertyFlags memoryPropertyFlags) const
{
    for (uint32_t i = 0; i < m_memoryProperties.memoryTypeCount; i++) {
        if ((typeBits & 1) && ((m_memoryProperties.memoryTypes[i].propertyFlags & memoryPropertyFlags) == memoryPropertyFlags)) {
            return i;
        }
    }
    return std::nullopt;
}

vk::UniqueCommandBuffer VulkanDevice::allocateCommandBuffer()
{
    auto [result, buffers] = m_logical.allocateCommandBuffersUnique(vk::CommandBufferAllocateInfo{
        m_commandPool.get(),
        vk::CommandBufferLevel::ePrimary,
        1,
    });
    if (result != vk::Result::eSuccess) {
        qCWarning(KWIN_VULKAN) << "failed to allocate oneshot command buffer:" << KWin::VKto_string(result);
        return {};
    }
    return std::move(buffers.front());
}

std::optional<vk::Fence> VulkanDevice::acquireFence()
{
    if (!m_freeFences.isEmpty()) {
        return m_freeFences.pop();
    }
    auto [fenceResult, newFence] = m_logical.createFence(vk::FenceCreateInfo{});
    if (fenceResult != vk::Result::eSuccess) {
        qCWarning(KWIN_VULKAN) << "failed to create fence:" << KWin::VKto_string(fenceResult);
        return std::nullopt;
    }
    return newFence;
}

std::optional<vk::Semaphore> VulkanDevice::acquireSemaphore()
{
    if (!m_freeSemaphores.isEmpty()) {
        return m_freeSemaphores.pop();
    }
    auto [semaphoreResult, newSemaphore] = m_logical.createSemaphore(vk::SemaphoreCreateInfo{});
    if (semaphoreResult != vk::Result::eSuccess) {
        qCWarning(KWIN_VULKAN) << "failed to create semaphore:" << KWin::VKto_string(semaphoreResult);
        return std::nullopt;
    }
    return newSemaphore;
}

template<bool wait, bool signal>
bool VulkanDevice::submitCommandBuffer(const vk::CommandBuffer cmd, const VulkanDeviceHandle handle)
{
    const auto f = acquireFence();
    if (f == std::nullopt) {
        return false;
    }
    const vk::Fence vk_fence = f.value();

    const std::array commandBuffers{cmd};
    constexpr std::array<vk::PipelineStageFlags, 0> waitDestinationStageMask{};

    std::vector<vk::Semaphore> signalSemaphores{};
    if constexpr (signal) {
        if (handle != ALL) {
            const auto semaphore = acquireSemaphore();
            if (semaphore == std::nullopt) {
                return false;
            }
            signalSemaphores.push_back(semaphore.value());
        } else {
            for (auto i = 0; i < m_semaphores.size(); i++) {
                const auto semaphore = acquireSemaphore();
                if (semaphore == std::nullopt) {
                    // return semaphores to the free list
                    for (const auto &s : signalSemaphores) {
                        m_freeSemaphores.push(s);
                    }
                    m_freeFences.push(vk_fence);
                    return false;
                }
                signalSemaphores.push_back(semaphore.value());
            }
        }
    }
    std::vector<vk::Semaphore> tempVec;
    std::vector<vk::Semaphore> *waitBeforeExecution = &tempVec;

    if constexpr (wait) {
        if (handle != ALL) {
            waitBeforeExecution = &m_semaphores[handle];
        } else {
            // We basically want to wait for all semaphores
            for (const auto &vec : m_semaphores) {
                // we copy the semaphores to our temporary vector
                tempVec.insert(tempVec.end(), vec.begin(), vec.end());
            }
        }
    }

    const auto submit_result = m_queue.submit(vk::SubmitInfo{
                                                  *waitBeforeExecution,
                                                  waitDestinationStageMask,
                                                  commandBuffers,
                                                  signalSemaphores,
                                              },
                                              vk_fence);

    switch (submit_result) {
    case vk::Result::eSuccess:
        break;
    case vk::Result::eErrorDeviceLost:
        // TODO: handle device lost
    default: // any other error
        qCWarning(KWIN_VULKAN) << "failed to submit command buffer:" << KWin::VKto_string(submit_result);
        // vulkan spec mentions that the synchronization primitives are not modified in case of an error
        // excluding the case of device lost, so we have to return them
        if constexpr (signal) {
            for (const auto &s : signalSemaphores) {
                m_freeSemaphores.push(s);
            }
        }
        m_freeFences.push(vk_fence);
        return false;
    }

    // we have to store the fence and the semaphores it depends on for later freeing
    m_fences[handle].push_back(vk_fence);
    m_fenceSemaphores[vk_fence] = std::move(*waitBeforeExecution);
    if (handle != ALL) {
        m_semaphores[handle] = std::move(signalSemaphores);
        return true;
    }
    size_t i = 0;
    for (auto &vec : m_semaphores) {
        vec.clear();
        vec.push_back(signalSemaphores[i]);
        i++;
    }
    return true;
}

template<bool wait>
bool VulkanDevice::submitCommandBufferBlocking(const vk::CommandBuffer cmd, const VulkanDeviceHandle handle)
{
    // No need to signal others since there is no others to signal
    if (!submitCommandBuffer<wait, false>(cmd, handle)) {
        return false;
    }
    return waitFence(handle);
}

bool VulkanDevice::waitFence(const VulkanDeviceHandle handle, size_t count)
{
    if (handle == ALL) {
        return waitAllFences();
    }

    while (!m_fences[ALL].isEmpty()) {
        if (!waitFence(m_fences[ALL].front())) {
            return false;
        }
        m_fences[ALL].pop_front();
    }

    if (count == 0 || count > m_fences[handle].size()) {
        count = m_fences[handle].size();
    }

    for (auto i = 0; i < count; i++) {
        if (!waitFence(m_fences[handle].front())) {
            return false;
        }
        m_fences[handle].pop_front();
    }
    return true;
}

bool VulkanDevice::waitAllFences()
{
    while (!m_fences[ALL].isEmpty()) {
        if (!waitFence(m_fences[ALL].front())) {
            return false;
        }
        m_fences[ALL].pop_front();
    }
    for (auto &fences : m_fences) {
        while (!fences.isEmpty()) {
            if (!waitFence(fences.front())) {
                return false;
            }
            fences.pop_front();
        }
    }
    return true;
}

bool VulkanDevice::waitFence(const vk::Fence &fence)
{
    auto &semaphores = m_fenceSemaphores[fence];
    auto result = m_logical.waitForFences(fence, VK_TRUE, UINT64_MAX);
    if (result != vk::Result::eSuccess) {
        qCWarning(KWIN_VULKAN) << "failed to wait for fence:" << KWin::VKto_string(result);
        return false;
    }
    result = m_logical.resetFences(fence);
    if (result != vk::Result::eSuccess) {
        qCWarning(KWIN_VULKAN) << "failed to reset fence:" << KWin::VKto_string(result) << " memory leak!";
        // TODO: Prevent memory leak
    } else {
        m_freeFences.push(fence);
    }
    for (const auto &s : semaphores) {
        m_freeSemaphores.push(s);
    }
    semaphores.clear();
    return true;
}

vk::UniqueDeviceMemory VulkanDevice::allocateMemory(vk::Buffer buffer, vk::MemoryPropertyFlags memoryProperties) const
{
    const auto requirements = m_logical.getBufferMemoryRequirements(buffer);
    if (const auto typeIndex = findMemoryType(requirements.memoryTypeBits, memoryProperties)) {
        auto [result, ret] = m_logical.allocateMemoryUnique(vk::MemoryAllocateInfo{
            requirements.size,
            *typeIndex,
        });
        if (result == vk::Result::eSuccess) {
            return std::move(ret);
        } else {
            qCWarning(KWIN_VULKAN) << "Allocating memory for a buffer failed:" << KWin::VKto_string(result);
            return {};
        }
    } else {
        qCWarning(KWIN_VULKAN) << "could not find a suitable memory index for a buffer";
        return {};
    }
}

vk::UniqueDeviceMemory VulkanDevice::allocateMemory(vk::Image image, vk::MemoryPropertyFlags memoryProperties) const
{
    const auto requirements = m_logical.getImageMemoryRequirements(image);
    if (const auto typeIndex = findMemoryType(requirements.memoryTypeBits, memoryProperties)) {
        auto [result, ret] = m_logical.allocateMemoryUnique(vk::MemoryAllocateInfo{
            requirements.size,
            *typeIndex,
        });
        if (result == vk::Result::eSuccess) {
            return std::move(ret);
        } else {
            qCWarning(KWIN_VULKAN) << "Allocating memory for an image failed:" << KWin::VKto_string(result);
            return {};
        }
    } else {
        qCWarning(KWIN_VULKAN) << "could not find a suitable memory index for an image";
        return {};
    }
}

QHash<uint32_t, QVector<uint64_t>> VulkanDevice::supportedFormats() const
{
    return m_formats;
}

vk::Device VulkanDevice::logicalDevice() const
{
    return m_logical;
}

uint32_t VulkanDevice::queueFamilyIndex() const
{
    return m_queueFamilyIndex;
}

std::optional<dev_t> VulkanDevice::primaryNode() const
{
    return m_primaryNode;
}

std::optional<dev_t> VulkanDevice::renderNode() const
{
    return m_renderNode;
}

vk::Queue VulkanDevice::renderQueue() const
{
    return m_queue;
}
// Template instantiation

template bool VulkanDevice::submitCommandBuffer<false, false>(vk::CommandBuffer cmd, VulkanDeviceHandle handle);
template bool VulkanDevice::submitCommandBuffer<false, true>(vk::CommandBuffer cmd, VulkanDeviceHandle handle);
template bool VulkanDevice::submitCommandBuffer<true, false>(vk::CommandBuffer cmd, VulkanDeviceHandle handle);
template bool VulkanDevice::submitCommandBuffer<true, true>(vk::CommandBuffer cmd, VulkanDeviceHandle handle);

template bool VulkanDevice::submitCommandBufferBlocking<false>(vk::CommandBuffer cmd, VulkanDeviceHandle handle);
template bool VulkanDevice::submitCommandBufferBlocking<true>(vk::CommandBuffer cmd, VulkanDeviceHandle handle);
}
