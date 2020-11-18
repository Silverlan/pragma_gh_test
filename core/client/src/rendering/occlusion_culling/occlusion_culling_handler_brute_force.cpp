/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2020 Florian Weischer
 */

#include "stdafx_client.h"
#include "pragma/rendering/occlusion_culling/occlusion_culling_handler_brute_force.hpp"
#include "pragma/rendering/renderers/rasterization_renderer.hpp"
#include "pragma/model/c_modelmesh.h"
#include <pragma/entities/entity_iterator.hpp>

using namespace pragma;

extern DLLCLIENT CGame *c_game;

void OcclusionCullingHandlerBruteForce::PerformCulling(
	pragma::CSceneComponent &scene,const rendering::RasterizationRenderer &renderer,const Vector3 &camPos,
	std::vector<OcclusionMeshInfo> &culledMeshesOut,bool cullByViewFrustum
)
{
	//auto d = uvec::distance(m_lastLodCamPos,posCam);
	//auto bUpdateLod = (d >= LOD_SWAP_DISTANCE) ? true : false;
	culledMeshesOut.clear();

	EntityIterator entIt {*c_game};
	entIt.AttachFilter<TEntityIteratorFilterComponent<pragma::CRenderComponent>>();
	for(auto *e : entIt)
	{
		if(e == nullptr)
			continue;
		auto *ent = static_cast<CBaseEntity*>(e);
		if(ent->IsInScene(scene) == false)
			continue;
		auto pRenderComponent = ent->GetRenderComponent();
		bool bViewModel = false;
		std::vector<Plane> *planes = nullptr;
		if((ShouldExamine(scene,renderer,*ent,bViewModel,cullByViewFrustum ? &planes : nullptr) == true))
		{
			//if(bUpdateLod == true) // Needs to be updated every frame (in case the entity is moving towards or away from us)
			//pRenderComponent->GetModelComponent()->UpdateLOD(camPos);
			if(pRenderComponent)
			{
				auto pTrComponent = ent->GetTransformComponent();
				auto &meshes = pRenderComponent->GetLODMeshes();
				auto numMeshes = meshes.size();
				auto pos = pTrComponent != nullptr ? pTrComponent->GetPosition() : Vector3{};
				for(auto itMesh=meshes.begin();itMesh!=meshes.end();++itMesh)
				{
					auto *mesh = static_cast<CModelMesh*>(itMesh->get());
					if(ShouldExamine(*mesh,pos,bViewModel,numMeshes,planes) == true)
					{
						if(culledMeshesOut.capacity() -culledMeshesOut.size() == 0)
							culledMeshesOut.reserve(culledMeshesOut.capacity() +100);
						culledMeshesOut.push_back(OcclusionMeshInfo{*ent,*mesh});
					}
				}
			}
		}
	}
	//if(bUpdateLod == true)
	//	m_lastLodCamPos = posCam;
}
