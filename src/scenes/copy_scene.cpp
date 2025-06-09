#include "copy_scene.h"

#include "util.h"
#include "vulkan_state.h"
#include "vulkan_image.h"
#include "vkutil/vkutil.h"

#include <iostream>

#include <cmath>

CopyScene::CopyScene() : Scene{"copy"}
{
}

void copy_test(VulkanState& vulkan);

void CopyScene::setup(VulkanState& vulkan_, std::vector<VulkanImage> const& images)
{
    Scene::setup(vulkan_, images);

    vulkan = &vulkan_;
    copy_test(vulkan_);
}

void CopyScene::teardown()
{
    vulkan->device().waitIdle();
    Scene::teardown();
}

void CopyScene::update()
{
    // no-op
}

void copy_test(VulkanState& vulkan)
{
    std::cout << "[ISO_TEST] Starting isolated copy_buffer test..." << std::endl;
    auto const& device = vulkan.device();
    const vk::DeviceSize TEST_BUFFER_SIZE = 4096;

    // 1. Prepare source data on CPU
    std::vector<uint8_t> source_cpu_data(TEST_BUFFER_SIZE);
    for (size_t i = 0; i < TEST_BUFFER_SIZE; ++i) {
        source_cpu_data[i] = static_cast<uint8_t>(i % 250 + 1);
    }
    std::cout << "[ISO_TEST] Source CPU data prepared." << std::endl;

    // 2. Create host-visible source buffer and fill it
    vk::DeviceMemory src_buffer_memory;
    auto src_buffer_obj = vkutil::BufferBuilder{vulkan}
        .set_size(TEST_BUFFER_SIZE)
        .set_usage(vk::BufferUsageFlagBits::eTransferSrc)
        .set_memory_properties(vk::MemoryPropertyFlagBits::eHostVisible)
        .set_memory_out(src_buffer_memory)
        .build();

    void* p_src_map = device.mapMemory(src_buffer_memory, 0, TEST_BUFFER_SIZE);
    memcpy(p_src_map, source_cpu_data.data(), TEST_BUFFER_SIZE);
    device.unmapMemory(src_buffer_memory);
    std::cout << "[ISO_TEST] Source Vulkan buffer created and populated." << std::endl;

    void* p_src_map2 = device.mapMemory(src_buffer_memory, 0, TEST_BUFFER_SIZE);
    if (memcmp(source_cpu_data.data(), p_src_map2, TEST_BUFFER_SIZE) == 0) {
        std::cout << "[ISO_TEST] Source Vulkan buffers are identical." << std::endl;
    } else {
        std::cerr << "[ISO_TEST] FAILURE: Isolated copy_buffer test FAILED. Even src buffers are different." << std::endl;
        const unsigned char* expected_bytes = source_cpu_data.data();
        const unsigned char* actual_bytes = static_cast<const unsigned char*>(p_src_map2);
        size_t mismatches_found = 0;
        const size_t max_mismatches_to_print = 128;

        for (size_t i = 0; i < TEST_BUFFER_SIZE; ++i) {
            if (expected_bytes[i] != actual_bytes[i]) {
                if (mismatches_found < max_mismatches_to_print) {
                    std::cerr << "[ISO_TEST] Mismatch at byte " << i << ":"
                              << " Expected (Src): 0x" << std::hex << static_cast<int>(expected_bytes[i])
                              << " Actual (Dst): 0x" << static_cast<int>(actual_bytes[i]) << std::dec << std::endl;
                }
                mismatches_found++;
            }
        }
        std::cerr << "[ISO_TEST] Total byte mismatches in isolated test: " << mismatches_found << " out of " << TEST_BUFFER_SIZE << " bytes." << std::endl;
         if (mismatches_found > max_mismatches_to_print && mismatches_found > 0) {
             std::cerr << "[ISO_TEST] (Further mismatches not printed for isolated test)" << std::endl;
         }
    }
    device.unmapMemory(src_buffer_memory);

    // *** ADDED FOR CHECKING: Flush after host write ***
    // This ensures CPU writes are visible to the GPU. Not needed for HOST_COHERENT, but we add it for testing.
    auto const flush_range = vk::MappedMemoryRange{}
        .setMemory(src_buffer_memory)
        .setOffset(0)
        .setSize(VK_WHOLE_SIZE);
    device.flushMappedMemoryRanges(flush_range);
    std::cout << "[ISO_TEST] Manually flushed src_buffer_memory." << std::endl;


    // 3. Create host-visible destination buffer
    vk::DeviceMemory dst_buffer_memory;
    auto dst_buffer_obj = vkutil::BufferBuilder{vulkan}
        .set_size(TEST_BUFFER_SIZE)
        .set_usage(vk::BufferUsageFlagBits::eTransferDst)
        .set_memory_properties(vk::MemoryPropertyFlagBits::eHostVisible)
        .set_memory_out(dst_buffer_memory)
        .build();
    std::cout << "[ISO_TEST] Destination Vulkan buffer created." << std::endl;

    // Optional: Fill destination buffer with a different pattern
    void* p_dst_init_map = device.mapMemory(dst_buffer_memory, 0, TEST_BUFFER_SIZE);
    memset(p_dst_init_map, 0x00, TEST_BUFFER_SIZE);
    device.unmapMemory(dst_buffer_memory);
    std::cout << "[ISO_TEST] Destination buffer pre-filled with 0x00 pattern." << std::endl;

    // 4. Call vkutil::copy_buffer
    std::cout << "[ISO_TEST] Calling vkutil::copy_buffer to copy " << TEST_BUFFER_SIZE << " bytes..." << std::endl;
    vkutil::copy_buffer(vulkan, src_buffer_obj.raw, dst_buffer_obj.raw, TEST_BUFFER_SIZE);
    std::cout << "[ISO_TEST] vkutil::copy_buffer returned." << std::endl;


    // *** ADDED FOR CHECKING: Invalidate before host read ***
    // This ensures the CPU sees the fresh data written by the GPU. Not needed for HOST_COHERENT.
    auto const invalidate_range = vk::MappedMemoryRange{}
        .setMemory(dst_buffer_memory)
        .setOffset(0)
        .setSize(VK_WHOLE_SIZE);
    device.invalidateMappedMemoryRanges(invalidate_range);
    std::cout << "[ISO_TEST] Manually invalidated dst_buffer_memory." << std::endl;


    // 5. Verify destination buffer contents
    std::cout << "[ISO_TEST] Verifying destination buffer contents..." << std::endl;
    void* p_dst_check_map = device.mapMemory(dst_buffer_memory, 0, TEST_BUFFER_SIZE);
    if (memcmp(source_cpu_data.data(), p_dst_check_map, TEST_BUFFER_SIZE) == 0) {
        std::cout << "[ISO_TEST] SUCCESS: Isolated copy_buffer test PASSED. Buffers are identical." << std::endl;
    } else {
        std::cerr << "[ISO_TEST] FAILURE: Isolated copy_buffer test FAILED. Buffers DIFFER." << std::endl;
        const unsigned char* expected_bytes = source_cpu_data.data();
        const unsigned char* actual_bytes = static_cast<const unsigned char*>(p_dst_check_map);
        size_t mismatches_found = 0;
        const size_t max_mismatches_to_print = 128;

        for (size_t i = 0; i < TEST_BUFFER_SIZE; ++i) {
            if (expected_bytes[i] != actual_bytes[i]) {
                if (mismatches_found < max_mismatches_to_print) {
                    std::cerr << "[ISO_TEST] Mismatch at byte " << i << ":"
                              << " Expected (Src): 0x" << std::hex << static_cast<int>(expected_bytes[i])
                              << " Actual (Dst): 0x" << static_cast<int>(actual_bytes[i]) << std::dec << std::endl;
                }
                mismatches_found++;
            }
        }
        std::cerr << "[ISO_TEST] Total byte mismatches in isolated test: " << mismatches_found << " out of " << TEST_BUFFER_SIZE << " bytes." << std::endl;
         if (mismatches_found > max_mismatches_to_print && mismatches_found > 0) {
             std::cerr << "[ISO_TEST] (Further mismatches not printed for isolated test)" << std::endl;
         }
    }
    device.unmapMemory(dst_buffer_memory);

    // 6. Cleanup
    std::cout << "[ISO_TEST] Isolated copy_buffer test finished." << std::endl;
}