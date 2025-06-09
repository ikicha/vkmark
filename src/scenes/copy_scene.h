#pragma once

#include "scene.h"
#include "managed_resource.h"

#include <vulkan/vulkan.hpp>

class CopyScene : public Scene
{
public:
    CopyScene();

    void setup(VulkanState&, std::vector<VulkanImage> const&) override;
    void update() override;
    void teardown() override;

private:
    VulkanState* vulkan;
};
