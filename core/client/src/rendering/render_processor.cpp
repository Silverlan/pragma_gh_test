/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2020 Florian Weischer
 */

#include "stdafx_client.h"
#include "pragma/model/c_model.h"
#include "pragma/model/c_modelmesh.h"
#include "pragma/debug/renderdebuginfo.hpp"
#include "pragma/entities/entity_instance_index_buffer.hpp"
#include "pragma/rendering/render_processor.hpp"
#include "pragma/rendering/scene/util_draw_scene_info.hpp"
#include "pragma/rendering/renderers/rasterization_renderer.hpp"
#include "pragma/rendering/shaders/world/c_shader_textured.hpp"
#include "pragma/rendering/render_stats.hpp"

extern DLLCENGINE CEngine *c_engine;
extern DLLCLIENT ClientState *client;
extern DLLCLIENT CGame *c_game;
#pragma optimize("",off)
static bool g_collectRenderStats = false;
static CallbackHandle g_cbPreRenderScene = {};
static CallbackHandle g_cbPostRenderScene = {};
static void print_pass_stats(const RenderPassStats &stats,bool full)
{
	auto *cam = c_game->GetRenderCamera();
	struct EntityData
	{
		EntityHandle hEntity {};
		float distance = 0.f;
	};
	std::vector<EntityData> entities;
	entities.reserve(stats.entities.size());
	for(auto &hEnt : stats.entities)
	{
		if(hEnt.IsValid() == false)
			continue;
		entities.push_back({});
		entities.back().hEntity = hEnt;
		if(cam == nullptr)
			continue;
		auto dist = uvec::distance(hEnt.get()->GetPosition(),cam->GetEntity().GetPosition());
		entities.back().distance = dist;
	}
	std::sort(entities.begin(),entities.end(),[](const EntityData &entData0,const EntityData &entData1) {
		return entData0.distance < entData1.distance;
	});

	Con::cout<<"\nEntities:";
	if(full == false)
		Con::cout<<" "<<entities.size()<<Con::endl;
	else
	{
		Con::cout<<Con::endl;
		for(auto &entData : entities)
		{
			auto &hEnt = entData.hEntity;
			if(hEnt.IsValid() == false)
				continue;
			uint32_t lod = 0;
			auto mdlC = hEnt.get()->GetComponent<pragma::CModelComponent>();
			if(mdlC.valid())
				lod = mdlC->GetLOD();
			hEnt.get()->print(Con::cout);
			Con::cout<<" (Distance: "<<entData.distance<<") (Lod: "<<lod<<")"<<Con::endl;
		}
	}
	
	Con::cout<<"\nMaterials:";
	if(full == false)
		Con::cout<<" "<<stats.materials.size()<<Con::endl;
	else
	{
		Con::cout<<Con::endl;
		for(auto &hMat : stats.materials)
		{
			if(hMat.IsValid() == false)
				continue;
			auto *albedoMap = hMat.get()->GetAlbedoMap();
			Con::cout<<hMat.get()->GetName();
			if(albedoMap)
				Con::cout<<" ["<<albedoMap->name<<"]";
			Con::cout<<Con::endl;
		}
	}

	Con::cout<<"\nShaders:"<<Con::endl;
	for(auto &hShader : stats.shaders)
	{
		if(hShader.expired())
			continue;
		Con::cout<<hShader->GetIdentifier()<<Con::endl;
	}
	
	Con::cout<<"\nUnique meshes: "<<stats.meshes.size()<<Con::endl;
	Con::cout<<"Shader state changes: "<<stats.numShaderStateChanges<<Con::endl;
	Con::cout<<"Material state changes: "<<stats.numMaterialStateChanges<<Con::endl;
	Con::cout<<"Entity state changes: "<<stats.numEntityStateChanges<<Con::endl;
	Con::cout<<"Entity buffer updates: "<<stats.numEntityBufferUpdates<<Con::endl;
	Con::cout<<"Number of instance sets: "<<stats.numInstanceSets<<" ("<<stats.numInstanceSetMeshes<<" meshes)"<<Con::endl;
	Con::cout<<"Number of instanced entities: "<<stats.instancedEntities.size()<<Con::endl;
	Con::cout<<"Number of instanced meshes: "<<stats.numInstancedMeshes<<Con::endl;
	Con::cout<<"Number of render items skipped through instancing: "<<stats.numInstancedSkippedRenderItems<<Con::endl;
	Con::cout<<"Number of entities without instancing: "<<stats.numEntitiesWithoutInstancing<<Con::endl; // TODO: This should just match numEntityStateChanges -numInstanceSets
	Con::cout<<"Number of meshes drawn: "<<stats.numDrawnMeshes<<Con::endl;
	Con::cout<<"Number of vertices drawn: "<<stats.numDrawnVertices<<Con::endl;
	Con::cout<<"Number of triangles drawn: "<<stats.numDrawnTrianges<<Con::endl;
	Con::cout<<"Wait time: "<<(static_cast<long double>(stats.renderThreadWaitTime.count()) /static_cast<long double>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds{1}).count()))<<Con::endl;
	Con::cout<<"CPU Execution time: "<<(static_cast<long double>(stats.cpuExecutionTime.count()) /static_cast<long double>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds{1}).count()))<<"ms"<<Con::endl;
}
DLLCLIENT void print_debug_render_stats(const RenderStats &renderStats,bool full)
{
	g_collectRenderStats = false;
	auto t = renderStats.lightingPass.cpuExecutionTime +renderStats.lightingPassTranslucent.cpuExecutionTime +renderStats.prepass.cpuExecutionTime +renderStats.shadowPass.cpuExecutionTime;
	Con::cout<<"Total CPU Execution time: "<<(static_cast<long double>(t.count()) /static_cast<long double>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds{1}).count()))<<"ms"<<Con::endl;
	Con::cout<<"Light culling time time: "<<(static_cast<long double>(renderStats.lightCullingTime.count()) /static_cast<long double>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds{1}).count()))<<"ms"<<Con::endl;
	Con::cout<<"\n----- Depth prepass: -----"<<Con::endl;
	print_pass_stats(renderStats.prepass,full);

	Con::cout<<"\n----- Shadow pass: -----"<<Con::endl;
	print_pass_stats(renderStats.shadowPass,full);

	Con::cout<<"\n----- Lighting pass: -----"<<Con::endl;
	print_pass_stats(renderStats.lightingPass,full);

	Con::cout<<"\n----- Lighting translucent pass: -----"<<Con::endl;
	print_pass_stats(renderStats.lightingPassTranslucent,full);
}
DLLCLIENT void debug_render_stats(bool full)
{
	g_collectRenderStats = true;
	g_cbPreRenderScene = c_game->AddCallback("PreRenderScene",FunctionCallback<void,std::reference_wrapper<const util::DrawSceneInfo>>::Create([](std::reference_wrapper<const util::DrawSceneInfo> drawSceneInfo) {
		drawSceneInfo.get().renderStats = std::make_unique<RenderStats>();
	}));
	g_cbPostRenderScene = c_game->AddCallback("PostRenderScene",FunctionCallback<void,std::reference_wrapper<const util::DrawSceneInfo>>::Create([full](std::reference_wrapper<const util::DrawSceneInfo> drawSceneInfo) {
		if(drawSceneInfo.get().renderStats)
			print_debug_render_stats(*drawSceneInfo.get().renderStats,full);
		if(g_cbPreRenderScene.IsValid())
			g_cbPreRenderScene.Remove();
		if(g_cbPostRenderScene.IsValid())
			g_cbPostRenderScene.Remove();
	}));
}

pragma::rendering::BaseRenderProcessor::BaseRenderProcessor(const util::DrawSceneInfo &drawSceneInfo,RenderFlags flags,const Vector4 &drawOrigin)
	: m_drawSceneInfo{drawSceneInfo},m_drawOrigin{drawOrigin},m_renderFlags{flags}
{
	auto &scene = drawSceneInfo.scene;
	auto *renderer = scene->GetRenderer();
	if(renderer == nullptr || renderer->IsRasterizationRenderer() == false)
		return;
	auto bReflection = umath::is_flag_set(flags,RenderFlags::Reflection);
	m_renderer = static_cast<const pragma::rendering::RasterizationRenderer*>(renderer);
	m_pipelineType = pragma::ShaderTextured3DBase::GetPipelineIndex(m_renderer->GetSampleCount(),bReflection);
}
pragma::rendering::BaseRenderProcessor::~BaseRenderProcessor()
{
	UnbindShader();
}

void pragma::rendering::BaseRenderProcessor::SetCountNonOpaqueMaterialsOnly(bool b) {umath::set_flag(m_stateFlags,StateFlags::CountNonOpaqueMaterialsOnly,b);}

void pragma::rendering::BaseRenderProcessor::UnbindShader()
{
	if(umath::is_flag_set(m_stateFlags,StateFlags::ShaderBound) == false)
		return;
	m_shaderScene->EndDraw();
	m_curShader = nullptr;
	m_curShaderIndex = std::numeric_limits<decltype(m_curShaderIndex)>::max();
	m_curInstanceSet = nullptr;
	umath::set_flag(m_stateFlags,StateFlags::ShaderBound,false);
}

void pragma::rendering::BaseRenderProcessor::UnbindMaterial()
{
	if(umath::is_flag_set(m_stateFlags,StateFlags::MaterialBound) == false)
		return;
	m_curMaterial = nullptr;
	m_curMaterialIndex = std::numeric_limits<decltype(m_curMaterialIndex)>::max();
	umath::set_flag(m_stateFlags,StateFlags::MaterialBound,false);
}

void pragma::rendering::BaseRenderProcessor::UnbindEntity()
{
	if(umath::is_flag_set(m_stateFlags,StateFlags::EntityBound) == false)
		return;
	m_curEntity = nullptr;
	m_curEntityIndex = std::numeric_limits<decltype(m_curEntityIndex)>::max();
	umath::set_flag(m_stateFlags,StateFlags::EntityBound,false);
}

bool pragma::rendering::BaseRenderProcessor::BindInstanceSet(pragma::ShaderGameWorld &shaderScene,const RenderQueue::InstanceSet *instanceSet)
{
	if(instanceSet == m_curInstanceSet)
		return true;
	m_curInstanceSet = instanceSet;
	return true;
}

bool pragma::rendering::BaseRenderProcessor::BindShader(prosper::Shader &shader)
{
	if(&shader == m_curShader)
		return umath::is_flag_set(m_stateFlags,StateFlags::ShaderBound);
	UnbindShader();
	UnbindMaterial();
	UnbindEntity();
	m_curShader = &shader;
	auto *shaderScene = dynamic_cast<pragma::ShaderGameWorld*>(&shader);
	if(shaderScene == nullptr)
		return false;
	if(shaderScene->BeginDraw(
		m_drawSceneInfo.commandBuffer,c_game->GetRenderClipPlane(),m_drawOrigin,
		m_pipelineType
	) == false)
		return false;
	auto &scene = *m_drawSceneInfo.scene;
	auto bView = (m_camType == CameraType::View) ? true : false;
	if(shaderScene->BindScene(const_cast<pragma::CSceneComponent&>(scene),const_cast<pragma::rendering::RasterizationRenderer&>(*m_renderer),bView) == false)
		return false;
	auto debugMode = scene.GetDebugMode();
	if(debugMode != ::pragma::SceneDebugMode::None)
		shaderScene->SetDebugMode(debugMode);
	shaderScene->Set3DSky(umath::is_flag_set(m_renderFlags,RenderFlags::RenderAs3DSky));
	
	if(m_stats)
	{
		++m_stats->numShaderStateChanges;
		m_stats->shaders.push_back(shader.GetHandle());
	}
	umath::set_flag(m_stateFlags,StateFlags::ShaderBound);

	m_shaderScene = shaderScene;
	m_curShaderIndex = shader.GetIndex();
	return true;
}
void pragma::rendering::BaseRenderProcessor::SetCameraType(CameraType camType)
{
	m_camType = camType;
	if(umath::is_flag_set(m_stateFlags,StateFlags::ShaderBound) == false || m_shaderScene == nullptr)
		return;
	auto &scene = *m_drawSceneInfo.scene.get();
	auto *renderer = scene.GetRenderer();
	if(renderer == nullptr)
		return;
	m_shaderScene->BindSceneCamera(scene,*static_cast<pragma::rendering::RasterizationRenderer*>(renderer),camType == CameraType::View);
}
void pragma::rendering::BaseRenderProcessor::Set3DSky(bool enabled)
{
	umath::set_flag(m_renderFlags,RenderFlags::RenderAs3DSky,enabled);
	if(umath::is_flag_set(m_stateFlags,StateFlags::ShaderBound) == false || m_shaderScene == nullptr)
		return;
	m_shaderScene->Set3DSky(enabled);
}
void pragma::rendering::BaseRenderProcessor::SetDrawOrigin(const Vector4 &drawOrigin)
{
	m_drawOrigin = drawOrigin;
	if(umath::is_flag_set(m_stateFlags,StateFlags::ShaderBound) == false || m_shaderScene == nullptr)
		return;
	m_shaderScene->BindDrawOrigin(drawOrigin);
}
bool pragma::rendering::BaseRenderProcessor::BindMaterial(CMaterial &mat)
{
	if(&mat == m_curMaterial)
		return umath::is_flag_set(m_stateFlags,StateFlags::MaterialBound);
	UnbindMaterial();
	m_curMaterial = &mat;
	if(umath::is_flag_set(m_stateFlags,StateFlags::ShaderBound) == false || mat.IsInitialized() == false || m_shaderScene->BindMaterial(mat) == false)
		return false;

	if(m_stats)
	{
		if(umath::is_flag_set(m_stateFlags,StateFlags::CountNonOpaqueMaterialsOnly) == false || mat.GetAlphaMode() != AlphaMode::Opaque)
		{
			++m_stats->numMaterialStateChanges;
			m_stats->materials.push_back(mat.GetHandle());
		}
	}
	umath::set_flag(m_stateFlags,StateFlags::MaterialBound);

	m_curMaterialIndex = mat.GetIndex();
	return true;
}
bool pragma::rendering::BaseRenderProcessor::BindEntity(CBaseEntity &ent)
{
	if(&ent == m_curEntity)
		return umath::is_flag_set(m_stateFlags,StateFlags::EntityBound);
	UnbindEntity();
	m_curEntity = &ent;
	auto *renderC = ent.GetRenderComponent();
	if(umath::is_flag_set(m_stateFlags,StateFlags::MaterialBound) == false || renderC == nullptr)
		return false;
	// if(m_stats && umath::is_flag_set(renderC->GetStateFlags(),CRenderComponent::StateFlags::RenderBufferDirty))
	// 	++m_stats->numEntityBufferUpdates;
	// renderC->UpdateRenderBuffers(m_drawSceneInfo.commandBuffer);
	if(m_shaderScene->BindEntity(ent) == false)
		return false;
	if(m_drawSceneInfo.renderFilter && m_drawSceneInfo.renderFilter(ent) == false)
		return false;
	
	m_curRenderC = renderC;
	m_curEntityMeshList = &renderC->GetRenderMeshes();
	auto *entClipPlane = m_curRenderC->GetRenderClipPlane();
	m_shaderScene->BindClipPlane(entClipPlane ? *entClipPlane : Vector4{});

	if(umath::is_flag_set(m_curRenderC->GetStateFlags(),pragma::CRenderComponent::StateFlags::HasDepthBias))
	{
		float constantFactor,biasClamp,slopeFactor;
		m_curRenderC->GetDepthBias(constantFactor,biasClamp,slopeFactor);
		m_drawSceneInfo.commandBuffer->RecordSetDepthBias(constantFactor,biasClamp,slopeFactor);
	}
	else
		m_drawSceneInfo.commandBuffer->RecordSetDepthBias();
	
	if(m_stats)
	{
		++m_stats->numEntityStateChanges;
		m_stats->entities.push_back(ent.GetHandle());
	}
	umath::set_flag(m_stateFlags,StateFlags::EntityBound);

	m_curEntityIndex = ent.GetLocalIndex();
	return true;
}

pragma::ShaderGameWorld *pragma::rendering::BaseRenderProcessor::GetCurrentShader()
{
	return umath::is_flag_set(m_stateFlags,StateFlags::ShaderBound) ? m_shaderScene : nullptr;
}

bool pragma::rendering::BaseRenderProcessor::Render(CModelSubMesh &mesh,pragma::RenderMeshIndex meshIdx,const RenderQueue::InstanceSet *instanceSet)
{
	if(umath::is_flag_set(m_stateFlags,StateFlags::EntityBound) == false || m_curRenderC == nullptr)
		return false;
	++m_numShaderInvocations;
	
	auto bUseVertexAnim = false;
	auto &mdlComponent = m_curRenderC->GetModelComponent();
	if(mdlComponent.valid())
	{
		auto &vertAnimBuffer = static_cast<CModel&>(*mdlComponent->GetModel()).GetVertexAnimationBuffer();
		if(vertAnimBuffer != nullptr)
		{
			auto pVertexAnimatedComponent = m_curEntity->GetComponent<pragma::CVertexAnimatedComponent>();
			if(pVertexAnimatedComponent.valid())
			{
				auto offset = 0u;
				auto animCount = 0u;
				if(pVertexAnimatedComponent->GetVertexAnimationBufferMeshOffset(mesh,offset,animCount) == true)
				{
					auto vaData = ((offset<<16)>>16) | animCount<<16;
					m_shaderScene->BindVertexAnimationOffset(vaData);
					bUseVertexAnim = true;
				}
			}
		}
	}
	if(bUseVertexAnim == false)
		m_shaderScene->BindVertexAnimationOffset(0u);

	BindInstanceSet(*m_shaderScene,instanceSet);
	auto instanceCount = instanceSet ? instanceSet->instanceCount : 1;
	if(m_stats)
	{
		m_stats->numDrawnMeshes += instanceCount;
		m_stats->numDrawnVertices += mesh.GetVertexCount() *instanceCount;
		m_stats->numDrawnTrianges += mesh.GetTriangleCount() *instanceCount;
		m_stats->meshes.push_back(std::static_pointer_cast<CModelSubMesh>(mesh.shared_from_this()));
	}
	auto instanceBuffer = m_curInstanceSet ? m_curInstanceSet->instanceBuffer : CSceneComponent::GetEntityInstanceIndexBuffer()->GetBuffer();
	m_shaderScene->Draw(mesh,meshIdx,*instanceBuffer,instanceCount);
	return true;
}

uint32_t pragma::rendering::BaseRenderProcessor::Render(const pragma::rendering::RenderQueue &renderQueue,bool bindShaders,RenderPassStats *optStats,std::optional<uint32_t> worldRenderQueueIndex)
{
	std::chrono::steady_clock::time_point t;
	if(optStats)
		t = std::chrono::steady_clock::now();
	renderQueue.WaitForCompletion();
	if(optStats)
		optStats->renderThreadWaitTime += std::chrono::steady_clock::now() -t;
	if(m_renderer == nullptr || (bindShaders == false && umath::is_flag_set(m_stateFlags,StateFlags::ShaderBound) == false))
		return 0;
	m_stats = optStats;
	if(bindShaders)
		UnbindShader();

	auto &shaderManager = c_engine->GetShaderManager();
	auto &matManager = client->GetMaterialManager();
	auto &sceneRenderDesc = m_drawSceneInfo.scene->GetSceneRenderDesc();
	uint32_t numShaderInvocations = 0;
	const RenderQueue::InstanceSet *curInstanceSet = nullptr;
	auto inInstancedEntityGroup = false;
	for(auto i=decltype(renderQueue.sortedItemIndices.size()){0u};i<renderQueue.sortedItemIndices.size();++i)
	{
		auto &itemSortPair = renderQueue.sortedItemIndices[i];
		auto &item = renderQueue.queue.at(itemSortPair.first);
		auto newInstance = false;
		if(item.instanceSetIndex == RenderQueueItem::INSTANCED)
		{
			// We've already covered this item through instancing,
			// we can skip this and all other instanced items for this set
			assert(curInstanceSet);
			i = static_cast<size_t>(curInstanceSet->startSkipIndex) +curInstanceSet->GetSkipCount() -1; // -1, since it'll get incremented again with the next iteration
			curInstanceSet = nullptr;
			inInstancedEntityGroup = false;
			continue;
		}
		else if(item.instanceSetIndex != RenderQueueItem::UNIQUE && curInstanceSet == nullptr)
		{
			curInstanceSet = &renderQueue.instanceSets[item.instanceSetIndex];
			newInstance = true;
		}
		if(worldRenderQueueIndex.has_value() && sceneRenderDesc.IsWorldMeshVisible(*worldRenderQueueIndex,item.mesh) == false)
			continue;
		if(bindShaders)
		{
			if(item.shader != m_curShaderIndex)
			{
				auto *shader = shaderManager.GetShader(item.shader);
				assert(shader);
				BindShader(*shader);
			}
			if(umath::is_flag_set(m_stateFlags,StateFlags::ShaderBound) == false)
				continue;
		}
		if(item.material != m_curMaterialIndex)
		{
			auto *mat = matManager.GetMaterial(item.material);
			assert(mat);
			BindMaterial(static_cast<CMaterial&>(*mat));
		}
		if(umath::is_flag_set(m_stateFlags,StateFlags::MaterialBound) == false)
			continue;
		if(item.entity != m_curEntityIndex)
		{
			// During instanced rendering, the entity index may flip between the mesh instances (because entity indices are *not* included in the sort key),
			// but we don't really care about which of the entity is bound, as long as *one of them*
			// is bound. That means if we have already bound one, we can skip this block.
			if(inInstancedEntityGroup == false)
			{
				auto *ent = c_game->GetEntityByLocalIndex(item.entity);
				assert(ent);
				// TODO: If we're instancing, there's technically no need to bind
				// the entity (except for resetting the clip plane, etc.)
				BindEntity(static_cast<CBaseEntity&>(*ent));
				if(m_stats && umath::is_flag_set(m_stateFlags,StateFlags::EntityBound))
				{
					if(item.instanceSetIndex == RenderQueueItem::UNIQUE)
						++m_stats->numEntitiesWithoutInstancing;
				}
				if(newInstance)
					inInstancedEntityGroup = true;
			}
		}
		if(umath::is_flag_set(m_stateFlags,StateFlags::EntityBound) == false || item.mesh >= m_curEntityMeshList->size())
			continue;
		if(m_stats && curInstanceSet)
		{
			++m_stats->numInstanceSets;
			if(newInstance)
			{
				m_stats->numInstanceSetMeshes += curInstanceSet->meshCount;
				m_stats->numInstancedMeshes += curInstanceSet->meshCount *curInstanceSet->instanceCount;
				m_stats->numInstancedSkippedRenderItems += curInstanceSet->GetSkipCount() -curInstanceSet->meshCount;
			}
		}
		auto &mesh = static_cast<CModelSubMesh&>(*m_curEntityMeshList->at(item.mesh));
		if(BaseRenderProcessor::Render(mesh,item.mesh,curInstanceSet))
			++numShaderInvocations;
	}
	if(optStats)
	{
		optStats->cpuExecutionTime += std::chrono::steady_clock::now() -t;

		for(auto &item : renderQueue.queue)
		{
			if(item.instanceSetIndex == RenderQueueItem::UNIQUE)
				continue;
			optStats->instancedEntities.insert(item.entity);
		}
	}
	return numShaderInvocations;
}
#pragma optimize("",on)