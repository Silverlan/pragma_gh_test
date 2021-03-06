/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2021 Silverlan
 */

#include "stdafx_client.h"
#include "pragma/rendering/shaders/c_shader_lua.hpp"
#include "pragma/lua/libraries/c_lua_vulkan.h"
#include "pragma/model/c_modelmesh.h"
#include "pragma/rendering/renderers/rasterization_renderer.hpp"
#include "pragma/entities/components/renderers/c_renderer_component.hpp"
#include "pragma/entities/components/renderers/c_rasterization_renderer_component.hpp"
#include <pragma/lua/converters/game_type_converters_t.hpp>
#include <pragma/lua/lua_entity_component.hpp>
#include <pragma/lua/util.hpp>
#include <shader/prosper_pipeline_create_info.hpp>
#include <shader/prosper_shader_copy_image.hpp>
#include <prosper_command_buffer.hpp>
#include <prosper_render_pass.hpp>
#include <prosper_util.hpp>
#include <wgui/wibase.h>

extern DLLCLIENT CEngine *c_engine;

prosper::DescriptorSetInfo pragma::to_prosper_descriptor_set_info(const LuaDescriptorSetInfo &descSetInfo)
{
	prosper::DescriptorSetInfo shaderDescSetInfo {};
	shaderDescSetInfo.bindings.reserve(descSetInfo.bindings.size());
	auto bindingIdx = 0u;
	for(auto &lBinding : descSetInfo.bindings)
	{
		shaderDescSetInfo.bindings.push_back({});
		auto &binding = shaderDescSetInfo.bindings.back();
		binding.type = lBinding.type;
		binding.shaderStages = lBinding.shaderStages;
		binding.descriptorArraySize = lBinding.descriptorArraySize;
		binding.bindingIndex = (lBinding.bindingIndex != std::numeric_limits<uint32_t>::max()) ? lBinding.bindingIndex : bindingIdx;

		bindingIdx = binding.bindingIndex +1u;
	}
	return shaderDescSetInfo;
}

static std::unordered_map<prosper::BasePipelineCreateInfo*,pragma::LuaShaderBase*> s_pipelineToShader;

pragma::LuaDescriptorSetInfo::LuaDescriptorSetInfo(luabind::object lbindings,uint32_t setIndex)
	: setIndex(setIndex)
{
	::Lua::get_table_values<LuaDescriptorSetBinding>(lbindings.interpreter(),2u,bindings,[](lua_State *l,int32_t idx) -> LuaDescriptorSetBinding {
		return *::Lua::CheckDescriptorSetBinding(l,idx);
	});
}

pragma::LuaShaderBase *pragma::LuaShaderBase::GetShader(prosper::BasePipelineCreateInfo &pipelineInfo)
{
	auto it = s_pipelineToShader.find(&pipelineInfo);
	if(it == s_pipelineToShader.end())
		return nullptr;
	return it->second;
}
pragma::LuaShaderBase::LuaShaderBase(prosper::Shader &shader)
	: LuaObjectBase(),m_shader(shader)
{
	// No multi-threading for Lua callbacks
	shader.SetMultiThreadedPipelineInitializationEnabled(false);
}
void pragma::LuaShaderBase::Initialize(const luabind::object &o)
{
	m_baseLuaObj = std::shared_ptr<luabind::object>(new luabind::object(o));
	CallLuaMember("Initialize");
}
void pragma::LuaShaderBase::ClearLuaObject() {m_baseLuaObj = nullptr;}
void pragma::LuaShaderBase::OnInitialized() {CallLuaMember("OnInitialized");}
void pragma::LuaShaderBase::OnPipelinesInitialized() {CallLuaMember("OnPipelinesInitialized");}
void pragma::LuaShaderBase::OnPipelineInitialized(uint32_t pipelineIdx) {CallLuaMember<void,uint32_t>("OnPipelineInitialized",pipelineIdx);}
void pragma::LuaShaderBase::InitializePipeline(prosper::BasePipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx)
{
	m_currentPipelineInfo = &pipelineInfo;
	m_currentDescSetIndex = 0u;
	m_curPipelineIdx = pipelineIdx;

	auto it = s_pipelineToShader.insert(std::make_pair(&pipelineInfo,this)).first;
	CallLuaMember<void,std::reference_wrapper<prosper::BasePipelineCreateInfo>,uint32_t>("InitializePipeline",std::ref(pipelineInfo),pipelineIdx);
	s_pipelineToShader.erase(it);

	m_currentPipelineInfo = nullptr;
	m_currentDescSetIndex = 0u;
	m_curPipelineIdx = std::numeric_limits<uint32_t>::max();
}
bool pragma::LuaShaderBase::AttachDescriptorSetInfo(const pragma::LuaDescriptorSetInfo &descSetInfo,uint32_t pipelineIdx)
{
	if(m_currentPipelineInfo == nullptr)
		return false;
	auto shaderDescSetInfo = to_prosper_descriptor_set_info(descSetInfo);
	shaderDescSetInfo.setIndex = (m_currentDescSetIndex != std::numeric_limits<uint32_t>::max()) ? m_currentDescSetIndex : descSetInfo.setIndex;
	m_currentDescSetIndex = shaderDescSetInfo.setIndex +1u;
	GetShader().AddDescriptorSetGroup(*m_currentPipelineInfo,pipelineIdx,shaderDescSetInfo);
	return true;
}
prosper::Shader &pragma::LuaShaderBase::GetShader() const {return m_shader;}

/////////////////

pragma::LuaShaderGraphicsBase::LuaShaderGraphicsBase(prosper::ShaderGraphics &shader)
	: pragma::LuaShaderBase(shader)
{}
bool pragma::LuaShaderGraphicsBase::AttachVertexAttribute(const pragma::LuaVertexBinding &binding,const std::vector<pragma::LuaVertexAttribute> &attributes)
{
	if(m_currentPipelineInfo == nullptr)
		return false;
	std::vector<prosper::VertexInputAttribute> attributeData;
	attributeData.reserve(attributes.size());

	auto offset = 0u;
	for(auto &attr : attributes)
	{
		auto attrSize = prosper::util::get_byte_size(attr.format);
		auto attrOffset = (attr.offset != std::numeric_limits<uint32_t>::max()) ? attr.offset : offset;
		auto attrLocation = (attr.location != std::numeric_limits<uint32_t>::max()) ? attr.location : m_currentVertexAttributeLocation;
		attributeData.push_back({attrLocation,static_cast<prosper::Format>(attr.format),attrOffset});

		offset = attrOffset +attrSize;
		++m_currentVertexAttributeLocation;
	}
	auto stride = (binding.stride != std::numeric_limits<uint32_t>::max()) ? binding.stride : offset;

	return static_cast<prosper::GraphicsPipelineCreateInfo*>(m_currentPipelineInfo)->AddVertexBinding(
		m_currentVertexBinding++,
		static_cast<prosper::VertexInputRate>(binding.inputRate),
		stride,
		attributeData.size(),
		attributeData.data()
	);
}
void pragma::LuaShaderGraphicsBase::InitializeGfxPipeline(prosper::GraphicsPipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx)
{
	m_currentVertexAttributeLocation = 0u;
	m_currentVertexBinding = 0u;

	InitializePipeline(pipelineInfo,pipelineIdx);

	m_currentVertexAttributeLocation = 0u;
	m_currentVertexBinding = 0u;
}
void pragma::LuaShaderGraphicsBase::InitializeRenderPass(std::shared_ptr<prosper::IRenderPass> &outRenderPass,uint32_t pipelineIdx)
{
	auto ret = false;
	luabind::object r;
	if(CallLuaMember<luabind::object,uint32_t>("InitializeRenderPass",&r,pipelineIdx) == CallbackReturnType::HasReturnValue && r)
	{
		auto *l = r.interpreter();
		r.push(l); /* 1 */
		auto t = ::Lua::GetStackTop(l);
		::Lua::CheckTable(l,t);

		::Lua::PushInt(l,1); /* 2 */
		::Lua::GetTableValue(l,t); /* 2 */
		outRenderPass = ::Lua::Check<::Lua::Vulkan::RenderPass>(l,-1).shared_from_this();
		::Lua::Pop(l,1); /* 1 */

		::Lua::Pop(l,1); /* 0 */
		return;
	}
	InitializeDefaultRenderPass(outRenderPass,pipelineIdx);
}

/////////////////

pragma::LuaShaderComputeBase::LuaShaderComputeBase(prosper::ShaderCompute &shader)
	: pragma::LuaShaderBase(shader)
{}

void pragma::LuaShaderComputeBase::InitializeComputePipeline(prosper::ComputePipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx) {InitializePipeline(pipelineInfo,pipelineIdx);}

/////////////////

pragma::LuaShaderGraphics::LuaShaderGraphics()
	: TLuaShaderBase(c_engine->GetRenderContext(),"","","")
{}
void pragma::LuaShaderGraphics::Lua_InitializePipeline(prosper::BasePipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx)
{
	prosper::ShaderGraphics::InitializeGfxPipeline(static_cast<prosper::GraphicsPipelineCreateInfo&>(pipelineInfo),pipelineIdx);
}
void pragma::LuaShaderGraphics::InitializeGfxPipeline(prosper::GraphicsPipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx) {pragma::LuaShaderGraphicsBase::InitializeGfxPipeline(pipelineInfo,pipelineIdx);}
void pragma::LuaShaderGraphics::InitializeRenderPass(std::shared_ptr<prosper::IRenderPass> &outRenderPass,uint32_t pipelineIdx) {pragma::LuaShaderGraphicsBase::InitializeRenderPass(outRenderPass,pipelineIdx);}
void pragma::LuaShaderGraphics::InitializeDefaultRenderPass(std::shared_ptr<prosper::IRenderPass> &outRenderPass,uint32_t pipelineIdx) {prosper::ShaderGraphics::InitializeRenderPass(outRenderPass,pipelineIdx);}

/////////////////

pragma::LuaShaderPostProcessing::LuaShaderPostProcessing()
	: TLuaShaderBase(c_engine->GetRenderContext(),"","","")
{
	SetBaseShader<prosper::ShaderCopyImage>();
}

void pragma::LuaShaderPostProcessing::Lua_InitializePipeline(prosper::BasePipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx)
{
	pragma::ShaderPPBase::InitializeGfxPipeline(static_cast<prosper::GraphicsPipelineCreateInfo&>(pipelineInfo),pipelineIdx);
}

void pragma::LuaShaderPostProcessing::InitializeGfxPipeline(prosper::GraphicsPipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx) {pragma::LuaShaderGraphicsBase::InitializeGfxPipeline(pipelineInfo,pipelineIdx);}
void pragma::LuaShaderPostProcessing::InitializeRenderPass(std::shared_ptr<prosper::IRenderPass> &outRenderPass,uint32_t pipelineIdx) {pragma::LuaShaderGraphicsBase::InitializeRenderPass(outRenderPass,pipelineIdx);}
void pragma::LuaShaderPostProcessing::InitializeDefaultRenderPass(std::shared_ptr<prosper::IRenderPass> &outRenderPass,uint32_t pipelineIdx) {pragma::ShaderPPBase::InitializeRenderPass(outRenderPass,pipelineIdx);}

/////////////////

pragma::LuaShaderImageProcessing::LuaShaderImageProcessing()
	: TLuaShaderBase(c_engine->GetRenderContext(),"","","")
{
	SetBaseShader<prosper::ShaderCopyImage>();
}

void pragma::LuaShaderImageProcessing::Lua_InitializePipeline(prosper::BasePipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx)
{
	prosper::ShaderBaseImageProcessing::InitializeGfxPipeline(static_cast<prosper::GraphicsPipelineCreateInfo&>(pipelineInfo),pipelineIdx);
}

void pragma::LuaShaderImageProcessing::InitializeGfxPipeline(prosper::GraphicsPipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx) {pragma::LuaShaderGraphicsBase::InitializeGfxPipeline(pipelineInfo,pipelineIdx);}
void pragma::LuaShaderImageProcessing::InitializeRenderPass(std::shared_ptr<prosper::IRenderPass> &outRenderPass,uint32_t pipelineIdx) {pragma::LuaShaderGraphicsBase::InitializeRenderPass(outRenderPass,pipelineIdx);}
void pragma::LuaShaderImageProcessing::InitializeDefaultRenderPass(std::shared_ptr<prosper::IRenderPass> &outRenderPass,uint32_t pipelineIdx) {prosper::ShaderBaseImageProcessing::InitializeRenderPass(outRenderPass,pipelineIdx);}

/////////////////

pragma::LuaShaderGUI::LuaShaderGUI()
	: TLuaShaderBase(c_engine->GetRenderContext(),"","","")
{
	SetPipelineCount(umath::to_integral(wgui::StencilPipeline::Count) *2);
}

bool pragma::LuaShaderGUI::RecordBeginDraw(
	prosper::ShaderBindState &bindState,wgui::DrawState &drawState,uint32_t width,uint32_t height,
	wgui::StencilPipeline pipelineIdx,bool msaa,uint32_t testStencilLevel
) const
{
	return wgui::Shader::RecordBeginDraw(bindState,drawState,width,height,pipelineIdx,msaa) &&
		RecordSetStencilReference(bindState,testStencilLevel);
}

void pragma::LuaShaderGUI::Lua_InitializePipeline(prosper::BasePipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx)
{
	wgui::Shader::InitializeGfxPipeline(static_cast<prosper::GraphicsPipelineCreateInfo&>(pipelineInfo),pipelineIdx);
}
void pragma::LuaShaderGUI::InitializeGfxPipeline(prosper::GraphicsPipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx)
{
	pragma::LuaShaderGraphicsBase::InitializeGfxPipeline(pipelineInfo,pipelineIdx);
}
void pragma::LuaShaderGUI::InitializeRenderPass(std::shared_ptr<prosper::IRenderPass> &outRenderPass,uint32_t pipelineIdx)
{
	pragma::LuaShaderGraphicsBase::InitializeRenderPass(outRenderPass,pipelineIdx);
}
void pragma::LuaShaderGUI::InitializeDefaultRenderPass(std::shared_ptr<prosper::IRenderPass> &outRenderPass,uint32_t pipelineIdx)
{
	wgui::Shader::InitializeRenderPass(outRenderPass,pipelineIdx);
}

/////////////////

pragma::LuaShaderGUITextured::LuaShaderGUITextured()
	: TLuaShaderBase(c_engine->GetRenderContext(),"","","")
{}

void pragma::LuaShaderGUITextured::Lua_InitializePipeline(prosper::BasePipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx)
{
	wgui::ShaderTextured::InitializeGfxPipeline(static_cast<prosper::GraphicsPipelineCreateInfo&>(pipelineInfo),pipelineIdx);
}
void pragma::LuaShaderGUITextured::InitializeGfxPipeline(prosper::GraphicsPipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx) {pragma::LuaShaderGraphicsBase::InitializeGfxPipeline(pipelineInfo,pipelineIdx);}
void pragma::LuaShaderGUITextured::InitializeRenderPass(std::shared_ptr<prosper::IRenderPass> &outRenderPass,uint32_t pipelineIdx) {pragma::LuaShaderGraphicsBase::InitializeRenderPass(outRenderPass,pipelineIdx);}
void pragma::LuaShaderGUITextured::InitializeDefaultRenderPass(std::shared_ptr<prosper::IRenderPass> &outRenderPass,uint32_t pipelineIdx) {wgui::ShaderTextured::InitializeRenderPass(outRenderPass,pipelineIdx);}

/////////////////

pragma::LuaShaderGUIParticle2D::LuaShaderGUIParticle2D()
	: TLuaShaderBase(c_engine->GetRenderContext(),"","","")
{}
Vector3 pragma::LuaShaderGUIParticle2D::Lua_CalcVertexPosition(
	lua_State *l,pragma::CParticleSystemComponent &hPtC,uint32_t ptIdx,uint32_t localVertIdx,const Vector3 &camPos,const Vector3 &camUpWs,const Vector3 &camRightWs,float nearZ,float farZ
)
{
	return pragma::ShaderParticle2DBase::DoCalcVertexPosition(hPtC,ptIdx,localVertIdx,camPos,camUpWs,camRightWs,nearZ,farZ);
}
void pragma::LuaShaderGUIParticle2D::Lua_InitializePipeline(prosper::BasePipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx)
{
	pragma::ShaderParticle2DBase::InitializeGfxPipeline(static_cast<prosper::GraphicsPipelineCreateInfo&>(pipelineInfo),pipelineIdx);
}
Vector3 pragma::LuaShaderGUIParticle2D::DoCalcVertexPosition(
	const pragma::CParticleSystemComponent &ptc,uint32_t ptIdx,uint32_t localVertIdx,
	const Vector3 &camPos,const Vector3 &camUpWs,const Vector3 &camRightWs,float nearZ,float farZ
) const
{
	auto ret = false;
	luabind::object r;
	auto &lPtc = ptc.GetLuaObject();
	if(const_cast<pragma::LuaShaderGUIParticle2D*>(this)->CallLuaMember<
		luabind::object,luabind::object,uint32_t,uint32_t,const Vector3&,const Vector3&,const Vector3&,float,float
	>("CalcVertexPosition",&r,lPtc,ptIdx,localVertIdx,camPos,camUpWs,camRightWs,nearZ,farZ) == CallbackReturnType::HasReturnValue && r)
	{
		auto *l = r.interpreter();
		r.push(l); /* 1 */
		auto &v = ::Lua::Check<Vector3>(l,-1);
		::Lua::Pop(l,1);
		return v;
	}
	return pragma::ShaderParticle2DBase::DoCalcVertexPosition(ptc,ptIdx,localVertIdx,camPos,camUpWs,camRightWs,nearZ,farZ);
}

void pragma::LuaShaderGUIParticle2D::InitializeGfxPipeline(prosper::GraphicsPipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx) {pragma::LuaShaderGraphicsBase::InitializeGfxPipeline(pipelineInfo,pipelineIdx);}
void pragma::LuaShaderGUIParticle2D::InitializeRenderPass(std::shared_ptr<prosper::IRenderPass> &outRenderPass,uint32_t pipelineIdx) {pragma::LuaShaderGraphicsBase::InitializeRenderPass(outRenderPass,pipelineIdx);}
void pragma::LuaShaderGUIParticle2D::InitializeDefaultRenderPass(std::shared_ptr<prosper::IRenderPass> &outRenderPass,uint32_t pipelineIdx) {pragma::ShaderParticle2DBase::InitializeRenderPass(outRenderPass,pipelineIdx);}

/////////////////

pragma::LuaShaderCompute::LuaShaderCompute()
	: TLuaShaderBase(c_engine->GetRenderContext(),"","")
{}
void pragma::LuaShaderCompute::Lua_InitializePipeline(prosper::BasePipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx)
{
	prosper::ShaderCompute::InitializeComputePipeline(static_cast<prosper::ComputePipelineCreateInfo&>(pipelineInfo),pipelineIdx);
}
void pragma::LuaShaderCompute::InitializeComputePipeline(prosper::ComputePipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx) {pragma::LuaShaderComputeBase::InitializeComputePipeline(pipelineInfo,pipelineIdx);}

/////////////////

pragma::LuaShaderTextured3D::LuaShaderTextured3D()
	: TLuaShaderBase(c_engine->GetRenderContext(),"","","")
{
	// SetBaseShader<ShaderTextured3DBase>();
}
void pragma::LuaShaderTextured3D::Lua_InitializePipeline(prosper::BasePipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx)
{
	ShaderGameWorldLightingPass::InitializeGfxPipeline(static_cast<prosper::GraphicsPipelineCreateInfo&>(pipelineInfo),pipelineIdx);
}
void pragma::LuaShaderTextured3D::Lua_InitializeGfxPipelineVertexAttributes(prosper::BasePipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx)
{
	ShaderGameWorldLightingPass::InitializeGfxPipelineVertexAttributes(static_cast<prosper::GraphicsPipelineCreateInfo&>(pipelineInfo),pipelineIdx);
}
void pragma::LuaShaderTextured3D::Lua_InitializeGfxPipelinePushConstantRanges(prosper::BasePipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx)
{
	ShaderGameWorldLightingPass::InitializeGfxPipelinePushConstantRanges(static_cast<prosper::GraphicsPipelineCreateInfo&>(pipelineInfo),pipelineIdx);
}
void pragma::LuaShaderTextured3D::Lua_InitializeGfxPipelineDescriptorSets(prosper::BasePipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx)
{
	ShaderGameWorldLightingPass::InitializeGfxPipelineDescriptorSets(static_cast<prosper::GraphicsPipelineCreateInfo&>(pipelineInfo),pipelineIdx);
}
void pragma::LuaShaderTextured3D::Lua_OnBindMaterial(Material &mat) {}
int32_t pragma::LuaShaderTextured3D::Lua_OnDraw(ModelSubMesh &mesh) {return umath::to_integral(util::EventReply::Unhandled);}
void pragma::LuaShaderTextured3D::Lua_OnBindEntity(EntityHandle &hEnt) {}
void pragma::LuaShaderTextured3D::Lua_OnBindScene(CRasterizationRendererComponent &renderer,bool bView) {}
void pragma::LuaShaderTextured3D::Lua_OnBeginDraw(prosper::ICommandBuffer &drawCmd,const Vector4 &clipPlane,uint32_t pipelineIdx,uint32_t recordFlags) {}
void pragma::LuaShaderTextured3D::Lua_OnEndDraw() {}
void pragma::LuaShaderTextured3D::InitializeGfxPipelineVertexAttributes(prosper::GraphicsPipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx)
{
	CallLuaMember<void,std::reference_wrapper<prosper::BasePipelineCreateInfo>,uint32_t>("InitializeGfxPipelineVertexAttributes",std::ref(static_cast<prosper::BasePipelineCreateInfo&>(pipelineInfo)),pipelineIdx);
}
void pragma::LuaShaderTextured3D::InitializeGfxPipelinePushConstantRanges(prosper::GraphicsPipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx)
{
	CallLuaMember<void,std::reference_wrapper<prosper::BasePipelineCreateInfo>,uint32_t>("InitializeGfxPipelinePushConstantRanges",std::ref(static_cast<prosper::BasePipelineCreateInfo&>(pipelineInfo)),pipelineIdx);
}
void pragma::LuaShaderTextured3D::InitializeGfxPipelineDescriptorSets(prosper::GraphicsPipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx)
{
	CallLuaMember<void,std::reference_wrapper<prosper::BasePipelineCreateInfo>,uint32_t>("InitializeGfxPipelineDescriptorSets",std::ref(static_cast<prosper::BasePipelineCreateInfo&>(pipelineInfo)),pipelineIdx);
}
void pragma::LuaShaderTextured3D::InitializeGfxPipeline(prosper::GraphicsPipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx) {pragma::LuaShaderGraphicsBase::InitializeGfxPipeline(pipelineInfo,pipelineIdx);}
void pragma::LuaShaderTextured3D::InitializeRenderPass(std::shared_ptr<prosper::IRenderPass> &outRenderPass,uint32_t pipelineIdx) {pragma::LuaShaderGraphicsBase::InitializeRenderPass(outRenderPass,pipelineIdx);}
void pragma::LuaShaderTextured3D::InitializeDefaultRenderPass(std::shared_ptr<prosper::IRenderPass> &outRenderPass,uint32_t pipelineIdx) {ShaderGameWorldLightingPass::InitializeRenderPass(outRenderPass,pipelineIdx);}

/////////////////

pragma::LuaShaderPbr::LuaShaderPbr()
	: TLuaShaderBase(c_engine->GetRenderContext(),"","","")
{
	// SetBaseShader<ShaderTextured3DBase>();
}
void pragma::LuaShaderPbr::Lua_InitializePipeline(prosper::BasePipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx)
{
	ShaderPBR::InitializeGfxPipeline(static_cast<prosper::GraphicsPipelineCreateInfo&>(pipelineInfo),pipelineIdx);
}
void pragma::LuaShaderPbr::Lua_InitializeGfxPipelineVertexAttributes(prosper::BasePipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx)
{
	ShaderPBR::InitializeGfxPipelineVertexAttributes(static_cast<prosper::GraphicsPipelineCreateInfo&>(pipelineInfo),pipelineIdx);
}
void pragma::LuaShaderPbr::Lua_InitializeGfxPipelinePushConstantRanges(prosper::BasePipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx)
{
	ShaderPBR::InitializeGfxPipelinePushConstantRanges(static_cast<prosper::GraphicsPipelineCreateInfo&>(pipelineInfo),pipelineIdx);
}
void pragma::LuaShaderPbr::Lua_InitializeGfxPipelineDescriptorSets(prosper::BasePipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx)
{
	ShaderPBR::InitializeGfxPipelineDescriptorSets(static_cast<prosper::GraphicsPipelineCreateInfo&>(pipelineInfo),pipelineIdx);
}
void pragma::LuaShaderPbr::Lua_OnBindMaterial(Material &mat) {}
int32_t pragma::LuaShaderPbr::Lua_OnDraw(ModelSubMesh &mesh) {return umath::to_integral(util::EventReply::Unhandled);}
void pragma::LuaShaderPbr::Lua_OnBindEntity(EntityHandle &hEnt) {}
void pragma::LuaShaderPbr::Lua_OnBindScene(CRasterizationRendererComponent &renderer,bool bView) {}
void pragma::LuaShaderPbr::Lua_OnBeginDraw(prosper::ICommandBuffer &drawCmd,const Vector4 &clipPlane,uint32_t pipelineIdx,uint32_t recordFlags) {}
void pragma::LuaShaderPbr::Lua_OnEndDraw() {}
void pragma::LuaShaderPbr::InitializeGfxPipelineVertexAttributes(prosper::GraphicsPipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx)
{
	CallLuaMember<void,std::reference_wrapper<prosper::BasePipelineCreateInfo>,uint32_t>("InitializeGfxPipelineVertexAttributes",std::ref(static_cast<prosper::BasePipelineCreateInfo&>(pipelineInfo)),pipelineIdx);
}
void pragma::LuaShaderPbr::InitializeGfxPipelinePushConstantRanges(prosper::GraphicsPipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx)
{
	CallLuaMember<void,std::reference_wrapper<prosper::BasePipelineCreateInfo>,uint32_t>("InitializeGfxPipelinePushConstantRanges",std::ref(static_cast<prosper::BasePipelineCreateInfo&>(pipelineInfo)),pipelineIdx);
}
void pragma::LuaShaderPbr::InitializeGfxPipelineDescriptorSets(prosper::GraphicsPipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx)
{
	CallLuaMember<void,std::reference_wrapper<prosper::BasePipelineCreateInfo>,uint32_t>("InitializeGfxPipelineDescriptorSets",std::ref(static_cast<prosper::BasePipelineCreateInfo&>(pipelineInfo)),pipelineIdx);
}
void pragma::LuaShaderPbr::InitializeGfxPipeline(prosper::GraphicsPipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx) {pragma::LuaShaderGraphicsBase::InitializeGfxPipeline(pipelineInfo,pipelineIdx);}
void pragma::LuaShaderPbr::InitializeRenderPass(std::shared_ptr<prosper::IRenderPass> &outRenderPass,uint32_t pipelineIdx) {pragma::LuaShaderGraphicsBase::InitializeRenderPass(outRenderPass,pipelineIdx);}
void pragma::LuaShaderPbr::InitializeDefaultRenderPass(std::shared_ptr<prosper::IRenderPass> &outRenderPass,uint32_t pipelineIdx) {ShaderPBR::InitializeRenderPass(outRenderPass,pipelineIdx);}

/////////////////

static void get_descriptor_set_layout_bindings(lua_State *l,std::vector<prosper::DescriptorSetInfo::Binding> &bindings,int32_t tBindings)
{
	auto numBindings = ::Lua::GetObjectLength(l,tBindings);
	bindings.reserve(numBindings);
	for(auto j=decltype(numBindings){0};j<numBindings;++j)
	{
		::Lua::PushInt(l,j +1); /* 1 */
		::Lua::GetTableValue(l,tBindings);

		::Lua::CheckTable(l,-1);
		auto tBinding = ::Lua::GetStackTop(l);

		auto type = prosper::DescriptorType::UniformBuffer;
		::Lua::get_table_value<ptrdiff_t,decltype(type)>(l,"type",tBinding,type,[](lua_State *l,int32_t idx) {
			return static_cast<ptrdiff_t>(::Lua::CheckInt(l,idx));
		});

		auto shaderStages = prosper::ShaderStageFlags::AllGraphics;
		::Lua::get_table_value<ptrdiff_t,decltype(shaderStages)>(l,"stage",tBinding,shaderStages,[](lua_State *l,int32_t idx) {
			return static_cast<ptrdiff_t>(::Lua::CheckInt(l,idx));
		});

		uint32_t arrayCount = 1;
		::Lua::get_table_value<ptrdiff_t,decltype(arrayCount)>(l,"arrayCount",tBinding,arrayCount,[](lua_State *l,int32_t idx) {
			return static_cast<ptrdiff_t>(::Lua::CheckInt(l,idx));
		});

		prosper::DescriptorSetInfo::Binding binding {type,shaderStages,arrayCount};
		bindings.push_back(binding);

		::Lua::Pop(l,1); /* 0 */
	}
}

pragma::LuaShaderManager::~LuaShaderManager()
{
	for(auto &pair : m_shaders)
	{
		if(pair.second.whShader.valid())
			c_engine->GetShaderManager().RemoveShader(*pair.second.whShader);
	}
}

void pragma::LuaShaderManager::RegisterShader(std::string className,luabind::object &o)
{
	ustring::to_lower(className);
	auto itShader = m_shaders.find(className);
	if(itShader != m_shaders.end())
	{
		Con::cwar<<"WARNING: Attempted to register shader '"<<className<<"', which has already been registered previously! Ignoring..."<<Con::endl;
		return;
	}
	auto *l = o.interpreter();
	luabind::object r;
#ifndef LUABIND_NO_EXCEPTIONS
	try
	{
#endif
		r = o();
#ifndef LUABIND_NO_EXCEPTIONS
	}
	catch(luabind::error&)
	{
		::Lua::HandleLuaError(l);
		return;
	}
#endif
	if(!r)
	{
		Con::ccl<<"WARNING: Unable to create lua shader '"<<className<<"'!"<<Con::endl;
		return;
	}
	
	auto &pair = m_shaders[className] = {};
	pair.luaClassObject = o;

	enum class ShaderType : uint8_t
	{
		Graphics = 0,
		Compute
	};
	auto shaderType = ShaderType::Graphics;
	auto *oShaderGraphics = luabind::object_cast_nothrow<pragma::LuaShaderGraphicsBase*>(r,static_cast<pragma::LuaShaderGraphicsBase*>(nullptr));
	auto *shader = oShaderGraphics ? dynamic_cast<pragma::LuaShaderBase*>(oShaderGraphics) : nullptr;
	if(shader == nullptr)
	{
		auto *oShaderCompute = luabind::object_cast_nothrow<pragma::LuaShaderComputeBase*>(r,static_cast<pragma::LuaShaderComputeBase*>(nullptr));
		shader = oShaderCompute ? dynamic_cast<pragma::LuaShaderBase*>(oShaderCompute) : nullptr;
		if(shader == nullptr)
		{
			Con::ccl<<"WARNING: Unable to create lua shader '"<<className<<"': Lua class is not derived from valid shader base!"<<Con::endl;
			return;
		}
		shaderType = ShaderType::Compute;
	}
	r.push(l); /* 1 */
	auto idx = ::Lua::GetStackTop(l);

	shader->Initialize(r);
	shader->SetIdentifier(className);

	if(shaderType == ShaderType::Graphics)
	{
		std::string fragmentShader;
		std::string vertexShader;
		std::string geometryShader;
		::Lua::GetProtectedTableValue(l,idx,"FragmentShader",fragmentShader);
		::Lua::GetProtectedTableValue(l,idx,"VertexShader",vertexShader);
		::Lua::GetProtectedTableValue(l,idx,"GeometryShader",geometryShader);
		auto &shaderGraphics = *dynamic_cast<prosper::Shader*>(shader);
		if(fragmentShader.empty() == false)
			shaderGraphics.SetStageSourceFilePath(prosper::ShaderStage::Fragment,fragmentShader);
		if(vertexShader.empty() == false)
			shaderGraphics.SetStageSourceFilePath(prosper::ShaderStage::Vertex,vertexShader);
		if(geometryShader.empty() == false)
			shaderGraphics.SetStageSourceFilePath(prosper::ShaderStage::Geometry,geometryShader);
	}
	else
	{
		std::string computeShader;
		::Lua::GetProtectedTableValue(l,idx,"ComputeShader",computeShader);
		auto &shaderCompute = *static_cast<pragma::LuaShaderCompute*>(shader);
		if(computeShader.empty() == false)
			shaderCompute.SetStageSourceFilePath(prosper::ShaderStage::Compute,computeShader);
	}
	::Lua::Pop(l);

	c_engine->GetShaderManager().RegisterShader(className,[shader](prosper::IPrContext &context,const std::string &identifier,bool &bExternalOwnership) {
		bExternalOwnership = true;
		return dynamic_cast<prosper::Shader*>(shader);
	});
	pair.whShader = c_engine->GetShaderManager().GetShader(className);
}

luabind::object *pragma::LuaShaderManager::GetClassObject(std::string className)
{
	ustring::to_lower(className);
	auto it = m_shaders.find(className);
	if(it == m_shaders.end())
		return nullptr;
	return &it->second.luaClassObject;
}
