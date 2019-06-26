/* Copyright (c) 2019, Arm Limited and Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge,
 * to any person obtaining a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include "gui.h"
#include "platform/application.h"
#include "rendering/render_context.h"
#include "scene_graph/scene.h"
#include "scene_graph/scripts/node_animation.h"
#include "stats.h"
#include "utils.h"

#include <algorithm>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace vkb
{
/**
 * @mainpage Overview of the framework
 *
 * @section initialization Initialization
 *
 * @subsection platform_init Platform initialization
 * The lifecycle of a Vulkan sample starts by instantiating the correct Platform
 * (e.g. WindowsPlatform) and then calling initialize() on it, which sets up
 * the windowing system and logging. Then it calls the parent Platform::initialize(),
 * which takes ownership of the active application an calls Application::prepare().
 *
 * @subsection sample_init Sample initialization
 * The preparation step is divided in two steps, one in VulkanSample and the other in the
 * specific sample, such as SurfaceRotation.
 * VulkanSample::prepare() contains functions that do not require customization,
 * including creating a Vulkan instance, the surface and getting physical devices.
 * The prepare() function for the specific sample completes the initialization, including:
 * - setting enabled Stats
 * - creating the Device
 * - creating the Swapchain
 * - creating the RenderContext (or child class)
 * - preparing the RenderContext
 * - loading the sg::Scene
 * - creating the RenderPipeline with ShaderModule (s)
 * - creating the sg::Camera
 * - creating the Gui
 *
 * @section frame_rendering Frame rendering
 *
 * @subsection update Update function
 * Rendering happens in the update() function. Each sample can override it, e.g.
 * to recreate the Swapchain in SwapchainImages when required by user input.
 * Typically a sample will then call VulkanSample::update().
 *
 * @subsection rendering Rendering
 * A series of steps are performed, some of which can be customized (it will be
 * highlighted when that's the case):
 *
 * - calling sg::Script::update() for all sg::Script (s)
 * - beginning a frame in RenderContext (does the necessary waiting on fences and
 *   acquires an core::Image)
 * - requesting a CommandBuffer
 * - updating Stats and Gui
 * - getting an active RenderTarget constructed by the factory function of the RenderFrame
 * - setting up barriers for color and depth, note that these are only for the default RenderTarget
 * - calling VulkanSample::draw_swapchain_renderpass (see below)
 * - setting up a barrier for the Swapchain transition to present
 * - submitting the CommandBuffer and end the Frame (present)
 *
 * @subsection draw_swapchain Draw swapchain renderpass
 * The function starts and ends a RenderPass which includes setting up viewport, scissors,
 * blend state (etc.) and calling draw_scene.
 * Note that RenderPipeline::draw is not virtual in RenderPipeline, but internally it calls
 * Subpass::draw for each Subpass, which is virtual and can be customized.
 *
 * @section framework_classes Main framework classes
 *
 * - RenderContext
 * - RenderFrame
 * - RenderTarget
 * - RenderPipeline
 * - ShaderModule
 * - ResourceCache
 * - BufferPool
 * - Core classes: Classes in vkb::core wrap Vulkan objects for indexing and hashing.
 */

class VulkanSample : public Application
{
  public:
	VulkanSample();

	virtual ~VulkanSample();

	/**
	 * @brief Additional sample initialization
	 */
	virtual bool prepare(Platform &platform) override;

	/**
	 * @brief Main loop sample events
	 */
	virtual void update(float delta_time) override;

	virtual void resize(const uint32_t width, const uint32_t height) override;

	virtual void input_event(const InputEvent &input_event) override;

	virtual void finish() override;

	/**
	 * @return A suitable GPU
	 */
	VkPhysicalDevice get_gpu();

	VkSurfaceKHR get_surface();

	/** 
	 * @brief Loads the scene
	 * 
	 * @param path The path of the glTF file
	 */
	void load_scene(const std::string &path);

	RenderContext &get_render_context()
	{
		assert(render_context && "Render context is not valid");
		return *render_context;
	}

  protected:
	std::unique_ptr<Device> device{nullptr};

	std::unique_ptr<RenderContext> render_context{nullptr};

	sg::Scene scene;

	std::unique_ptr<Gui> gui{nullptr};

	std::unique_ptr<Stats> stats{nullptr};

	/**
	 * @brief Resets the stats view max values for high demanding configs
	 *        Should be overriden by the samples since they
	 *        know which configuration is resource demanding
	 */
	virtual void reset_stats_view(){};

	/**
	 * @brief Record render pass for drawing the scene
	 */
	virtual void draw_swapchain_renderpass(CommandBuffer &command_buffer, RenderTarget &render_target);

	/**
	 * @brief Draw scene meshes to the command buffer
	 *
	 * @param command_buffer The Vulkan command buffer
	 */
	virtual void draw_scene(CommandBuffer &command_buffer);

	virtual void draw_gui();

	/**
	 * @brief Updates the debug window, samples can override this to insert their own data elements
	 */
	virtual void update_debug_window();

	/**
	 * @brief Add free camera script to a node with a camera object.
	 *        Fallback to the default_camera if node not found.
	 *
	 * @param node_name The scene node name
	 *
	 * @return Node where the script was attached as component
	 */
	sg::Node &add_free_camera(const std::string &node_name);

  private:
	static constexpr float STATS_VIEW_RESET_TIME{10.0f};        // 10 seconds

#if defined(VKB_DEBUG) || defined(VKB_VALIDATION_LAYERS)
	/// The debug report callback
	VkDebugReportCallbackEXT debug_report_callback{VK_NULL_HANDLE};
#endif

	/**
	 * @brief The Vulkan instance
	 */
	VkInstance instance{VK_NULL_HANDLE};

	/**
	 * @brief The Vulkan surface
	 */
	VkSurfaceKHR surface{VK_NULL_HANDLE};

	/**
	 * @brief The physical devices found on the machine
	 */
	std::vector<VkPhysicalDevice> gpus;

	/**
	 * @brief Create a Vulkan instance
	 *
	 * @param required_instance_extensions The required Vulkan instance extensions
	 * @param required_instance_layers The required Vulkan instance layers
	 *
	 * @return Vulkan instance object
	 */
	VkInstance create_instance(const std::vector<const char *> &required_instance_extensions = {},
	                           const std::vector<const char *> &required_instance_layers     = {});
};
}        // namespace vkb
