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

#include "surface_rotation.h"

#include "core/device.h"
#include "core/pipeline_layout.h"
#include "core/shader_module.h"
#include "gltf_loader.h"
#include "gui.h"
#include "platform/filesystem.h"
#include "platform/platform.h"
#include "rendering/subpasses/scene_subpass.h"
#include "scene_graph/components/material.h"
#include "scene_graph/components/pbr_material.h"
#include "stats.h"

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
#	include "platform/android/android_platform.h"
#endif

SurfaceRotation::SurfaceRotation()
{
	auto &config = get_configuration();

	config.insert<vkb::BoolSetting>(0, pre_rotate, false);
	config.insert<vkb::BoolSetting>(1, pre_rotate, true);
}

bool SurfaceRotation::prepare(vkb::Platform &platform)
{
	if (!VulkanSample::prepare(platform))
	{
		return false;
	}

	if (get_surface() == VK_NULL_HANDLE)
	{
		throw std::runtime_error("Requires a surface to run sample");
	}

	auto enabled_stats = {vkb::StatIndex::l2_ext_read_stalls, vkb::StatIndex::l2_ext_write_stalls};

	stats = std::make_unique<vkb::Stats>(enabled_stats);

	load_scene("scenes/sponza/Sponza01.gltf");
	auto &camera_node = add_free_camera("main_camera");
	camera            = dynamic_cast<vkb::sg::PerspectiveCamera *>(&camera_node.get_component<vkb::sg::Camera>());

	vkb::ShaderSource vert_shader(vkb::fs::read_shader("base.vert"));
	vkb::ShaderSource frag_shader(vkb::fs::read_shader("base.frag"));
	auto              scene_subpass = std::make_unique<vkb::SceneSubpass>(get_render_context(), std::move(vert_shader), std::move(frag_shader), *scene, *camera);

	auto render_pipeline = vkb::RenderPipeline();
	render_pipeline.add_subpass(std::move(scene_subpass));

	set_render_pipeline(std::move(render_pipeline));

	gui = std::make_unique<vkb::Gui>(*this, platform.get_window().get_dpi_factor());

	return true;
}

void SurfaceRotation::update(float delta_time)
{
	handle_no_resize_rotations();

	// Process GUI input
	if (pre_rotate != last_pre_rotate)
	{
		recreate_swapchain();

		last_pre_rotate = pre_rotate;
	}

	glm::mat4 pre_rotate_mat = glm::mat4(1.0f);

	// In pre-rotate mode, the application has to handle the rotation
	glm::vec3 rotation_axis = glm::vec3(0.0f, 0.0f, -1.0f);

	const auto &swapchain = get_render_context().get_swapchain();

	if (swapchain.get_transform() & VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR)
	{
		pre_rotate_mat = glm::rotate(pre_rotate_mat, glm::radians(90.0f), rotation_axis);
	}
	else if (swapchain.get_transform() & VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR)
	{
		pre_rotate_mat = glm::rotate(pre_rotate_mat, glm::radians(270.0f), rotation_axis);
	}
	else if (swapchain.get_transform() & VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR)
	{
		pre_rotate_mat = glm::rotate(pre_rotate_mat, glm::radians(180.0f), rotation_axis);
	}

	// Ensure that the camera uses the swapchain dimensions, since in pre-rotate
	// mode the aspect ratio never changes
	VkExtent2D extent = swapchain.get_extent();
	camera->set_aspect_ratio(static_cast<float>(extent.width) / extent.height);
	camera->set_pre_rotation(pre_rotate_mat);

	get_render_context().set_pre_transform(select_pre_transform());

	VulkanSample::update(delta_time);
}

void SurfaceRotation::draw_gui()
{
	std::string       rotation_by_str = pre_rotate ? "application" : "compositor";
	auto              prerotate_str   = "Pre-rotate (" + rotation_by_str + " rotates)";
	uint32_t          a_width         = get_render_context().get_swapchain().get_extent().width;
	uint32_t          a_height        = get_render_context().get_swapchain().get_extent().height;
	float             aspect_ratio    = static_cast<float>(a_width) / static_cast<float>(a_height);
	auto              transform       = SurfaceRotation::transform_to_string(get_render_context().get_swapchain().get_transform());
	auto              resolution_str  = "Res: " + std::to_string(a_width) + "x" + std::to_string(a_height);
	std::stringstream fov_stream;
	fov_stream << "FOV: " << std::fixed << std::setprecision(2) << camera->get_field_of_view() * 180.0f / glm::pi<float>();
	auto fov_str = fov_stream.str();

	// If pre-rotate is enabled, the aspect ratio will not change, therefore need to check if the
	// scene has been rotated
	auto rotated = get_render_context().get_swapchain().get_transform() & (VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR | VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR);
	if (aspect_ratio > 1.0f || (aspect_ratio < 1.0f && rotated))
	{
		// GUI landscape layout
		uint32_t lines = 2;
		gui->show_options_window(
		    /* body = */ [&]() {
			    ImGui::Checkbox(prerotate_str.c_str(), &pre_rotate);
			    ImGui::Text("%s | %s | %s", transform, resolution_str.c_str(), fov_str.c_str());
		    },
		    /* lines = */ lines);
	}
	else
	{
		// GUI portrait layout
		uint32_t lines = 3;
		gui->show_options_window(
		    /* body = */ [&]() {
			    ImGui::Checkbox(prerotate_str.c_str(), &pre_rotate);
			    ImGui::Text("%s", transform);
			    ImGui::Text("%s | %s", resolution_str.c_str(), fov_str.c_str());
		    },
		    /* lines = */ lines);
	}
}

VkSurfaceTransformFlagBitsKHR SurfaceRotation::select_pre_transform()
{
	VkSurfaceCapabilitiesKHR surface_properties;
	VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(get_device().get_physical_device(),
	                                                   get_surface(),
	                                                   &surface_properties));

	VkSurfaceTransformFlagBitsKHR pre_transform;

	if (pre_rotate)
	{
		// Best practice: adjust the preTransform attribute in the swapchain properties
		// so that it matches the value in the surface properties. This is to
		// communicate to the presentation engine that the application is pre-rotating
		pre_transform = surface_properties.currentTransform;
	}
	else
	{
		// Bad practice: keep preTransform as identity
		pre_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	}

	return pre_transform;
}

void SurfaceRotation::handle_no_resize_rotations()
{
	VkSurfaceCapabilitiesKHR surface_properties;
	VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(get_device().get_physical_device(),
	                                                   get_surface(),
	                                                   &surface_properties));

	if (surface_properties.currentExtent.width == get_render_context().get_surface_extent().width &&
	    surface_properties.currentExtent.height == get_render_context().get_surface_extent().height &&
	    (pre_rotate && surface_properties.currentTransform != get_render_context().get_swapchain().get_transform()))
	{
		recreate_swapchain();
	}
}

void SurfaceRotation::recreate_swapchain()
{
	LOGI("Recreating swapchain");

	get_device().wait_idle();

	auto surface_extent = get_render_context().get_surface_extent();

	get_render_context().update_swapchain(surface_extent, select_pre_transform());

	if (gui)
	{
		gui->resize(surface_extent.width, surface_extent.height);
	}
}

const char *SurfaceRotation::transform_to_string(VkSurfaceTransformFlagBitsKHR flag)
{
	switch (flag)
	{
		case VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR:
			return "SURFACE_TRANSFORM_IDENTITY";
		case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR:
			return "SURFACE_TRANSFORM_ROTATE_90";
		case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR:
			return "SURFACE_TRANSFORM_ROTATE_180";
		case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR:
			return "SURFACE_TRANSFORM_ROTATE_270";
		case VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_BIT_KHR:
			return "SURFACE_TRANSFORM_HORIZONTAL_MIRROR";
		case VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90_BIT_KHR:
			return "SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90";
		case VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_180_BIT_KHR:
			return "SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_180";
		case VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270_BIT_KHR:
			return "SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270";
		case VK_SURFACE_TRANSFORM_INHERIT_BIT_KHR:
			return "SURFACE_TRANSFORM_INHERIT";
		case VK_SURFACE_TRANSFORM_FLAG_BITS_MAX_ENUM_KHR:
			return "SURFACE_TRANSFORM_FLAG_BITS_MAX_ENUM";
		default:
			return "[Unknown transform flag]";
	}
}

std::unique_ptr<vkb::VulkanSample> create_surface_rotation()
{
	return std::make_unique<SurfaceRotation>();
}
