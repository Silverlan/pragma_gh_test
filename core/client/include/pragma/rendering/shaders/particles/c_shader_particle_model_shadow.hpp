/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2021 Silverlan
 */

#ifndef __C_SHADER_PARTICLE_MODEL_SHADOW_HPP__
#define __C_SHADER_PARTICLE_MODEL_SHADOW_HPP__
// prosper TODO
#if 0
#include "c_shader_shadow.h"

namespace Shader
{
	class DLLCLIENT ParticleModelShadow
		: public Shadow
	{
	public:
		ParticleModelShadow();
		void BindInstanceDescriptorSet(Vulkan::CommandBufferObject *cmdBuffer,const Vulkan::DescriptorSetObject *descSetInstance,const Mat4 &depthMvp);
		bool BeginDrawTest(Vulkan::BufferObject *particleBuffer,Vulkan::BufferObject *rotBuffer,Vulkan::CommandBufferObject *cmdBuffer,CLightBase *light,uint32_t w,uint32_t h);
		void DrawTest(CModelSubMesh *mesh,uint32_t instanceCount);
		enum class DLLCLIENT Location : uint32_t
		{
			Xyzs = umath::to_integral(Shadow::Location::Vertex) +1,
			Rotation = Xyzs +1
		};
		enum class DLLCLIENT Binding : uint32_t
		{
			Xyzs = umath::to_integral(Shadow::Binding::BoneWeight) +1,
			Rotation = Xyzs +1
		};
	protected:
		virtual void InitializeVertexDescriptions(std::vector<vk::VertexInputBindingDescription> &vertexBindingDescriptions,std::vector<vk::VertexInputAttributeDescription> &vertexAttributeDescriptions) override;
	};
};
#endif
#endif
