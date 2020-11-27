/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2020 Florian Weischer
 */

#ifndef __RENDER_QUEUE_HPP__
#define __RENDER_QUEUE_HPP__

#include "pragma/clientdefinitions.h"
#include "pragma/entities/c_baseentity.h"
#include "pragma/model/c_modelmesh.h"
#include <cmaterialmanager.h>
#include <shader/prosper_shader.hpp>

namespace pragma::rendering
{
	struct SortingKey
	{
		// Note: Order is important!
		// Distance should *not* be set unless necessary (e.g. translucent geometry),
		// otherwise instancing effectiveness will be reduced
		uint64_t distance : 32, shader : 15, material : 16, instantiable : 1;
		void SetDistance(const Vector3 &origin,const CCameraComponent &cam);
	};
	struct RenderQueueItem
	{
		static auto constexpr INSTANCED = std::numeric_limits<uint16_t>::max();
		static auto constexpr UNIQUE = std::numeric_limits<uint16_t>::max() -1;
		MaterialIndex material;
		prosper::ShaderIndex shader;
		EntityIndex entity;
		pragma::RenderMeshIndex mesh;
		SortingKey sortingKey;

		uint16_t instanceSetIndex;
	};

	// using SortingKey = uint32_t;
	using RenderQueueItemIndex = uint32_t;
	using RenderQueueItemSortPair = std::pair<RenderQueueItemIndex,SortingKey>;
	using RenderQueueSortList = std::vector<RenderQueueItemSortPair>;
	class DLLCLIENT RenderQueue
		: public std::enable_shared_from_this<RenderQueue>
	{
	public:
		struct InstanceSet
		{
			uint32_t instanceCount;
			std::shared_ptr<prosper::IBuffer> instanceBuffer;
			
			uint32_t meshCount;
			uint32_t startSkipIndex;
			uint32_t GetSkipCount() const {return instanceCount *meshCount;}
		};
		static std::shared_ptr<RenderQueue> Create();
		void Clear();
		void Reserve();
		void Add(const RenderQueueItem &item);
		void Add(CBaseEntity &ent,RenderMeshIndex meshIdx,CMaterial &mat,pragma::ShaderTextured3DBase &shader,const CCameraComponent *optCam=nullptr);
		void Sort();
		void Merge(const RenderQueue &other);
		std::vector<RenderQueueItem> queue;
		RenderQueueSortList sortedItemIndices;
		std::vector<InstanceSet> instanceSets;

		void Lock();
		void Unlock();
		void WaitForCompletion() const;
		bool IsComplete() const;
	private:
		RenderQueue();

		std::atomic<bool> m_locked = false;
		mutable std::condition_variable m_threadWaitCondition {};
		mutable std::mutex m_threadWaitMutex {};
	};

	class RenderQueueJob
	{
		std::shared_ptr<RenderQueue> m_renderQueue;
		bool IsLocked() const;
		void QueryForRendering();
		void Reset();
		void Start(); // ??
	};

	class DLLCLIENT RenderQueueBuilder
	{
	public:
		RenderQueueBuilder();
		~RenderQueueBuilder();
		void Append(const std::function<void()> &worker);
		void Flush();
		bool HasWork() const;
		uint32_t GetWorkQueueCount() const;
	private:
		void Exec();
		void BuildRenderQueues();
		std::thread m_thread;

		std::mutex m_workMutex;
		std::queue<std::function<void()>> m_workQueue;
		std::condition_variable m_threadWaitCondition {};
		std::mutex m_threadWaitMutex {};
		std::atomic<bool> m_threadRunning = false;
		std::atomic<bool> m_hasWork = false;
	};
};

#endif