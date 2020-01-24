#include "stdafx_client.h"
#include <pragma/entities/entity_component_system_t.hpp>
#include <pragma/entities/entity_iterator.hpp>
#include <pragma/entities/entity_component_system_t.hpp>
#include <wrappers/memory_block.h>
#include <prosper_command_buffer.hpp>
#include <prosper_descriptor_set_group.hpp>
#include <image/prosper_sampler.hpp>
#include <image/prosper_render_target.hpp>
#include <image/prosper_image_view.hpp>
#include <sharedutils/util_library.hpp>
#include <sharedutils/util_image_buffer.hpp>
#include <pragma/console/sh_cmd.h>
#include <cmaterialmanager.h>
#include <texturemanager/texturemanager.h>
#include <texture_type.h>
#include <wgui/types/wirect.h>
#include <pragma/entities/baseentity_events.hpp>
#include <pragma/console/command_options.hpp>
#include <pragma/util/stb_image_write.h>
#include "pragma/entities/environment/c_env_reflection_probe.hpp"
#include "pragma/entities/c_entityfactories.h"
#include "pragma/lua/c_lentity_handles.hpp"
#include "pragma/gui/witexturedcubemap.hpp"
#include "pragma/gui/wiframe.h"
#include "pragma/gui/wislider.h"
#include "pragma/rendering/shaders/c_shader_equirectangular_to_cubemap.hpp"
#include "pragma/rendering/shaders/c_shader_convolute_cubemap_lighting.hpp"
#include "pragma/rendering/shaders/c_shader_compute_irradiance_map_roughness.hpp"
#include "pragma/rendering/shaders/c_shader_brdf_convolution.hpp"
#include "pragma/rendering/shaders/world/c_shader_pbr.hpp"
#include "pragma/rendering/raytracing/cycles.hpp"
#include "pragma/math/c_util_math.hpp"
#include "pragma/console/c_cvar_global_functions.h"
#include "pr_dds.hpp"

extern DLLCENGINE CEngine *c_engine;
extern DLLCLIENT ClientState *client;
extern DLLCLIENT CGame *c_game;

using namespace pragma;

LINK_ENTITY_TO_CLASS(env_reflection_probe,CEnvReflectionProbe);

#pragma optimize("",off)
rendering::IBLData::IBLData(const std::shared_ptr<prosper::Texture> &irradianceMap,const std::shared_ptr<prosper::Texture> &prefilterMap,const std::shared_ptr<prosper::Texture> &brdfMap)
	: irradianceMap{irradianceMap},prefilterMap{prefilterMap},brdfMap{brdfMap}
{}

void Console::commands::map_build_reflection_probes(NetworkState *state,pragma::BasePlayerComponent *pl,std::vector<std::string> &argv)
{
	if(c_game == nullptr)
		return;
	std::unordered_map<std::string,pragma::console::CommandOption> commandOptions {};
	pragma::console::parse_command_options(argv,commandOptions);
	auto rebuild = (commandOptions.find("rebuild") != commandOptions.end());
	CReflectionProbeComponent::BuildAllReflectionProbes(*c_game,rebuild);
}
static void print_status(uint32_t i,uint32_t count)
{
	auto percent = umath::ceil((count > 0u) ? (i /static_cast<float>(count) *100.f) : 100.f);
	Con::cout<<"Reflection probe update at "<<percent<<"%"<<Con::endl;
}

////////////////

CReflectionProbeComponent::RaytracingJobManager::RaytracingJobManager(CReflectionProbeComponent &probe)
	: probe{probe}
{}
CReflectionProbeComponent::RaytracingJobManager::~RaytracingJobManager()
{
	for(auto &job : jobs)
		job.Cancel();
	for(auto &job : jobs)
		job.Wait();
}
void CReflectionProbeComponent::RaytracingJobManager::StartNextJob()
{
	if(m_nextJobIndex >= jobs.size())
	{
		auto pThis = std::move(probe.m_raytracingJobManager); // Keep alive until end of scope
		Finalize();
		return;
	}
	auto jobId = m_nextJobIndex++;
	auto &job = jobs.at(jobId);
	auto preprocessCompletionHandler = job.GetCompletionHandler();
	job.SetCompletionHandler([this,jobId,preprocessCompletionHandler](util::ParallelWorker<std::shared_ptr<util::ImageBuffer>> &worker) {
		if(worker.IsSuccessful() == false)
		{
			Con::cwar<<"WARNING: Raytracing scene for reflection probe has failed: "<<worker.GetResultMessage()<<Con::endl;
			probe.m_raytracingJobManager = nullptr;
			return;
		}
		if(preprocessCompletionHandler)
			preprocessCompletionHandler(worker);
		auto imgBuffer = worker.GetResult();
		m_layerImageBuffers.at(jobId) = imgBuffer;
		StartNextJob();
	});
	job.Start();
	c_engine->AddParallelJob(job,"Reflection probe");
}
void CReflectionProbeComponent::RaytracingJobManager::Finalize()
{
	// Initialize cubemap image data from individual cubemap side buffers.
	// Since the cubemap image is in optimal layout with device memory,
	// we'll have to copy the data to a temporary buffer first and then
	// to the image via the command buffer.
	auto cubemapImage = probe.CreateCubemapImage();
	auto extents = cubemapImage->GetExtents();

	//auto *memBlock = cubemapImage->GetAnvilImage().get_memory_block();
	//auto totalSize = memBlock->get_create_info_ptr()->get_size();
	for(auto layerIndex=decltype(m_layerImageBuffers.size()){0u};layerIndex<m_layerImageBuffers.size();++layerIndex)
	{
		auto &imgBuffer = m_layerImageBuffers.at(layerIndex);
		auto imgDataSize = imgBuffer->GetSize();

		auto tmpBuf = c_engine->AllocateTemporaryBuffer(imgDataSize,imgBuffer->GetData());

		auto &setupCmd = c_engine->GetSetupCommandBuffer();
		prosper::util::BufferImageCopyInfo copyInfo {};
		copyInfo.baseArrayLayer = layerIndex;
		copyInfo.dstImageLayout = Anvil::ImageLayout::TRANSFER_DST_OPTIMAL;
		prosper::util::record_copy_buffer_to_image(**setupCmd,copyInfo,*tmpBuf,**cubemapImage);

		c_engine->FlushSetupCommandBuffer();

		// Don't need the image buffer anymore
		imgBuffer = nullptr;
	}
	probe.FinalizeCubemap(*cubemapImage);
}

////////////////

//static auto *GUI_EL_NAME = "cubemap_generation_image";
static std::vector<CReflectionProbeComponent*> get_probes()
{
	if(c_game == nullptr)
		return {};
	EntityIterator entIt {*c_game,EntityIterator::FilterFlags::Default | EntityIterator::FilterFlags::Pending};
	entIt.AttachFilter<TEntityIteratorFilterComponent<CReflectionProbeComponent>>();
	auto numProbes = entIt.GetCount();
	std::vector<CReflectionProbeComponent*> probes {};
	probes.reserve(numProbes);
	for(auto *ent : entIt)
	{
		auto reflProbeC = ent->GetComponent<CReflectionProbeComponent>();
		probes.push_back(reflProbeC.get());
	}
	return probes;
}
static void build_next_reflection_probe()
{
	if(c_game == nullptr)
		return;
	auto probes = get_probes();
	for(auto *probe : probes)
	{
		if(probe->RequiresRebuild() == false)
		{
			probe->LoadIBLReflectionsFromFile();
			continue;
		}
		auto &ent = probe->GetEntity();
		auto pos = ent.GetPosition();
		Con::cout<<"Updating reflection probe at position ("<<pos.x<<","<<pos.y<<","<<pos.z<<")..."<<Con::endl;
		if(probe->UpdateIBLData(false))
			break;
		Con::cwar<<"WARNING: Unable to update reflection probe data for probe at position ("<<pos.x<<","<<pos.y<<","<<pos.z<<"). Probe will be unavailable!"<<Con::endl;
	}

	/*auto &wgui = WGUI::GetInstance();
	auto *p = dynamic_cast<WITexturedCubemap*>(wgui.GetBaseElement()->FindDescendantByName(GUI_EL_NAME));
	if(p)
	{
	auto hEl = p->GetHandle();
	c_game->CreateTimer(5.f,0,FunctionCallback<void>::Create([hEl]() {
	if(hEl.IsValid())
	hEl.get()->Remove();
	}),TimerType::RealTime);
	}*/
}

// These have been determined through experimentation and produce good results
// while not taking excessive time to render.
static constexpr uint32_t CUBEMAP_LAYER_WIDTH = 512;
static constexpr uint32_t CUBEMAP_LAYER_HEIGHT = 512;
static constexpr uint32_t RAYTRACING_SAMPLE_COUNT = 64;
void CReflectionProbeComponent::BuildAllReflectionProbes(Game &game,bool rebuild)
{
	if(rebuild)
	{
		// Clear existing IBL files
		for(auto *probe : get_probes())
		{
			auto path = probe->GetCubemapIBLMaterialPath();
			std::array<std::string,3> filePostfixes = {
				".wmi",
				"_irradiance.ktx",
				"_prefilter.ktx"
			};
			auto identifier = probe->GetCubemapIdentifier();
			for(auto &postfix : filePostfixes)
			{
				auto fpath = "materials/" +path +identifier +postfix;
				if(FileManager::Exists(fpath) == false)
					continue;
				Con::cout<<"Removing probe IBL file '"<<fpath<<"'..."<<Con::endl;
				if(FileManager::RemoveFile(fpath.c_str()))
					continue;
				Con::cwar<<"WARNING: Unable to remove IBL file '"<<fpath<<"'! This reflection probe may not be rebuilt!"<<Con::endl;
			}
		}
	}
	uint32_t numProbes = 0u;
	for(auto *probe : get_probes())
	{
		if(probe->RequiresRebuild())
			++numProbes;
	}
	Con::cout<<"Updating "<<numProbes<<" reflection probes... This may take a while!"<<Con::endl;
	build_next_reflection_probe();
}

Anvil::DescriptorSet *CReflectionProbeComponent::FindDescriptorSetForClosestProbe(const Vector3 &origin)
{
	if(c_game == nullptr)
		return nullptr;
	// Find closest reflection probe to camera position
	EntityIterator entIt {*c_game};
	entIt.AttachFilter<TEntityIteratorFilterComponent<pragma::CReflectionProbeComponent>>();
	auto dClosest = std::numeric_limits<float>::max();
	BaseEntity *entClosest = nullptr;
	for(auto *ent : entIt)
	{
		auto posEnt = ent->GetPosition();
		auto d = uvec::distance_sqr(origin,posEnt);
		if(d >= dClosest)
			continue;
		dClosest = d;
		entClosest = ent;
	}
	if(entClosest)
	{
		auto &reflectionProbeC = *entClosest->GetComponent<pragma::CReflectionProbeComponent>();
		return reflectionProbeC.GetIBLDescriptorSet();
	}
	return nullptr;
}

void CReflectionProbeComponent::Initialize()
{
	BaseEntityComponent::Initialize();
	GetEntity().AddComponent<CTransformComponent>();

	BindEvent(BaseEntity::EVENT_HANDLE_KEY_VALUE,[this](std::reference_wrapper<pragma::ComponentEvent> evData) -> util::EventReply {
		auto &kvData = static_cast<CEKeyValueData&>(evData.get());
		if(ustring::compare(kvData.key,"env_map",false))
			m_srcEnvMap = kvData.value;
		else
			return util::EventReply::Unhandled;
		return util::EventReply::Handled;
	});
}

void CReflectionProbeComponent::OnEntitySpawn()
{
	BaseEntityComponent::OnEntitySpawn();
	if(LoadIBLReflectionsFromFile() == false)
		Con::cwar<<"WARNING: Invalid/missing IBL reflection resources for cubemap "<<GetCubemapIdentifier()<<"! Please run 'map_build_reflection_probes' to build all reflection probes!"<<Con::endl;
}

std::string CReflectionProbeComponent::GetCubemapIBLMaterialFilePath() const
{
	return "materials/" +GetCubemapIBLMaterialPath() +GetCubemapIdentifier() +".wmi";
}

bool CReflectionProbeComponent::RequiresRebuild() const
{
	if(m_bBakingFailed)
		return false; // We've already tried baking the probe and it failed; don't try again!
	auto matPath = GetCubemapIBLMaterialFilePath();
	if(FileManager::Exists(matPath))
		return false; // IBL textures already exist; nothing to be done!
	return true;
}

bool CReflectionProbeComponent::UpdateIBLData(bool rebuild)
{
	if(rebuild == false && RequiresRebuild() == false)
		return true;
	if(m_srcEnvMap.empty() == false)
	{
		if(GenerateIBLReflectionsFromEnvMap("materials/" +m_srcEnvMap) == false)
			return false;
	}
	else if(CaptureIBLReflectionsFromScene() == false)
		return false;
	return true;
}

luabind::object CReflectionProbeComponent::InitializeLuaObject(lua_State *l) {return BaseEntityComponent::InitializeLuaObject<CReflectionProbeComponentHandleWrapper>(l);}

bool CReflectionProbeComponent::SaveIBLReflectionsToFile()
{
	if(m_iblData == nullptr)
		return false;
	auto relPath = GetCubemapIBLMaterialPath();
	auto absPath = "materials/" +relPath;
	FileManager::CreatePath(absPath.c_str());
	auto identifier = GetCubemapIdentifier();

	auto &imgPrefilter = m_iblData->prefilterMap->GetImage();
	auto &imgBrdf = m_iblData->brdfMap->GetImage();
	auto &imgIrradiance = m_iblData->irradianceMap->GetImage();

	auto fErrorHandler = [](const std::string &errMsg) {
		Con::cwar<<"WARNING: Unable to create IBL reflection files: "<<errMsg<<Con::endl;
	};
	const std::string pathBrdf = "materials/env/brdf.ktx";
	if(FileManager::Exists(pathBrdf) == false)
	{
		ImageWriteInfo imgWriteInfo {};
		imgWriteInfo.inputFormat = ImageWriteInfo::InputFormat::R16G16B16A16_Float;
		imgWriteInfo.outputFormat = ImageWriteInfo::OutputFormat::HDRColorMap;
		if(c_game->SaveImage(*imgBrdf,"materials/env/brdf",imgWriteInfo) == false)
		{
			fErrorHandler("Unable to save BRDF map!");
			return false;
		}
	}

	ImageWriteInfo imgWriteInfo {};
	imgWriteInfo.inputFormat = ImageWriteInfo::InputFormat::R16G16B16A16_Float;
	imgWriteInfo.outputFormat = ImageWriteInfo::OutputFormat::HDRColorMap;
	auto prefix = identifier +"_";
	if(c_game->SaveImage(*imgPrefilter,absPath +prefix +"prefilter",imgWriteInfo) == false)
	{
		fErrorHandler("Unable to save prefilter map!");
		return false;
	}
	if(c_game->SaveImage(*imgIrradiance,absPath +prefix +"irradiance",imgWriteInfo) == false)
	{
		fErrorHandler("Unable to save irradiance map!");
		return false;
	}

	auto *mat = client->CreateMaterial("ibl");
	if(mat == nullptr)
		return false;
	auto &dataBlock = mat->GetDataBlock();
	dataBlock->AddValue("texture","prefilter",relPath +prefix +"prefilter");
	dataBlock->AddValue("texture","irradiance",relPath +prefix +"irradiance");
	dataBlock->AddValue("texture","brdf","env/brdf");
	return mat->Save(relPath +identifier +".wmi");
}

util::ParallelJob<std::shared_ptr<util::ImageBuffer>> CReflectionProbeComponent::CaptureRaytracedIBLReflectionsFromScene(
	uint32_t width,uint32_t height,uint32_t layerIndex,
	const Vector3 &camPos,const Quat &camRot,float nearZ,float farZ,umath::Degree fov
)
{
	rendering::cycles::SceneInfo sceneInfo {};
	sceneInfo.width = width;
	sceneInfo.height = height;

	rendering::cycles::RenderImageInfo renderImgInfo {};
	renderImgInfo.cameraPosition = camPos;
	renderImgInfo.cameraRotation = camRot;
	renderImgInfo.nearZ = nearZ;
	renderImgInfo.farZ = farZ;
	renderImgInfo.fov = fov;

	// TODO: Replace these with command arguments?
	sceneInfo.sky = "skies/dusk379.hdr";
	sceneInfo.skyAngles = {0.f,160.f,0.f};
	sceneInfo.skyStrength = 72.f;

	sceneInfo.samples = RAYTRACING_SAMPLE_COUNT;
	sceneInfo.denoise = true;
	sceneInfo.hdrOutput = true;

	// Top and bottom layers have to be flipped vertically, all others horizontally.
	// Most likely to do with the view matrices being set up incorrectly.
	auto flipVertically = (layerIndex == 2 || layerIndex == 3);
	std::shared_ptr<util::ImageBuffer> imgBuffer = nullptr;
	renderImgInfo.entityFilter = [](BaseEntity &ent) -> bool {
		return ent.IsStatic();
	};
	auto job = rendering::cycles::render_image(*client,sceneInfo,renderImgInfo);
	if(job.IsValid() == false)
		return {};
	job.SetCompletionHandler([flipVertically](util::ParallelWorker<std::shared_ptr<util::ImageBuffer>> &worker) {
		if(worker.IsSuccessful() == false)
		{
			Con::cwar<<"WARNING: Raytracing scene for IBL reflections has failed: "<<worker.GetResultMessage()<<Con::endl;
			return;
		}
		auto imgBuffer = worker.GetResult();
		if(flipVertically)
			imgBuffer->FlipVertically();
	});
	return job;
}

std::shared_ptr<prosper::Image> CReflectionProbeComponent::CreateCubemapImage()
{
	prosper::util::ImageCreateInfo createInfo {};
	createInfo.format = Anvil::Format::R16G16B16A16_SFLOAT; // We need HDR colors for the cubemap
	createInfo.flags = prosper::util::ImageCreateInfo::Flags::Cubemap | prosper::util::ImageCreateInfo::Flags::FullMipmapChain;
	// The rendered cubemap itself will be discarded, so we render it at a high resolution
	// to get the best results for the subsequent stages.
	createInfo.width = CUBEMAP_LAYER_WIDTH;
	createInfo.height = CUBEMAP_LAYER_HEIGHT;
	createInfo.memoryFeatures = prosper::util::MemoryFeatureFlags::GPUBulk;
	createInfo.postCreateLayout = Anvil::ImageLayout::TRANSFER_DST_OPTIMAL;
	createInfo.tiling = Anvil::ImageTiling::OPTIMAL;
	createInfo.usage = Anvil::ImageUsageFlagBits::SAMPLED_BIT | Anvil::ImageUsageFlagBits::TRANSFER_SRC_BIT | Anvil::ImageUsageFlagBits::TRANSFER_DST_BIT;
	auto &dev = c_engine->GetDevice();
	return prosper::util::create_image(dev,createInfo);
}

bool CReflectionProbeComponent::CaptureIBLReflectionsFromScene()
{
	m_bBakingFailed = true; // Mark as failed until complete
	auto pos = GetEntity().GetPosition();
	Con::cout<<"Capturing reflection probe IBL reflections for probe at position ("<<pos.x<<","<<pos.y<<","<<pos.z<<")..."<<Con::endl;

	auto &scene = c_game->GetScene();
	auto hCam = scene->GetActiveCamera();
	if(hCam.expired())
	{
		Con::cwar<<"WARNING: Unable to capture scene: Game scene camera is invalid!"<<Con::endl;
		return false;
	}

	auto hShaderPbr = c_engine->GetShader("pbr");
	if(hShaderPbr.expired())
	{
		Con::cwar<<"WARNING: Unable to capture scene: PBR shader is not valid!"<<Con::endl;
		return false;
	}

	constexpr auto useRaytracing = true;

	static const std::array<std::pair<Vector3,Vector3>,6> forwardUpDirs = {
		std::pair<Vector3,Vector3>{Vector3{-1.0f,0.0f,0.0f},Vector3{0.0f,1.0f,0.0f}},
		std::pair<Vector3,Vector3>{Vector3{1.0f,0.0f,0.0f},Vector3{0.0f,1.0f,0.0f}},
		std::pair<Vector3,Vector3>{Vector3{0.0f,1.0f,0.0f},Vector3{0.0f,0.0f,1.0f}},
		std::pair<Vector3,Vector3>{Vector3{0.0f,-1.0f,0.0f},Vector3{0.0f,0.0f,-1.0f}},
		std::pair<Vector3,Vector3>{Vector3{0.0f,0.0f,1.0f},Vector3{0.0f,1.0f,0.0f}},
		std::pair<Vector3,Vector3>{Vector3{0.0f,0.0f,-1.0f},Vector3{0.0f,1.0f,0.0f}}
	};
	static const std::array<Mat4,6> cubemapViewMatrices = {
		glm::lookAtRH(Vector3{0.0f,0.0f,0.0f},forwardUpDirs.at(0).first,forwardUpDirs.at(0).second),
		glm::lookAtRH(Vector3{0.0f,0.0f,0.0f},forwardUpDirs.at(1).first,forwardUpDirs.at(1).second),
		glm::lookAtRH(Vector3{0.0f,0.0f,0.0f},forwardUpDirs.at(2).first,forwardUpDirs.at(2).second),
		glm::lookAtRH(Vector3{0.0f,0.0f,0.0f},forwardUpDirs.at(3).first,forwardUpDirs.at(3).second),
		glm::lookAtRH(Vector3{0.0f,0.0f,0.0f},forwardUpDirs.at(4).first,forwardUpDirs.at(4).second),
		glm::lookAtRH(Vector3{0.0f,0.0f,0.0f},forwardUpDirs.at(5).first,forwardUpDirs.at(5).second)
	};

	if(useRaytracing)
	{
		m_raytracingJobManager = std::unique_ptr<RaytracingJobManager>{new RaytracingJobManager{*this}};
		uint32_t numJobs = 0u;
		for(uint8_t iLayer=0;iLayer<6u;++iLayer)
		{
			auto &forward = forwardUpDirs.at(iLayer).first;
			auto &up = forwardUpDirs.at(iLayer).second;
			auto right = uvec::cross(forward,up);
			uvec::normalize(&right);
			auto rot = uquat::create(forward,right,up);
			auto job = CaptureRaytracedIBLReflectionsFromScene(CUBEMAP_LAYER_WIDTH,CUBEMAP_LAYER_HEIGHT,iLayer,pos,rot,hCam->GetNearZ(),hCam->GetFarZ(),90.f /* fov */);
			if(job.IsValid() == false)
				break;
			m_raytracingJobManager->jobs.at(iLayer) = job;
			++numJobs;
		}
		if(numJobs < 6)
		{
			Con::cwar<<"WARNING: Unable to set scene up for reflection probe raytracing!"<<Con::endl;
			m_raytracingJobManager = nullptr;
			return false;
		}
		m_raytracingJobManager->StartNextJob();
		return true;
	}

	// We're generating the IBL textures now, so we have to fall back to non-ibl mode.
	static_cast<pragma::ShaderPBR*>(hShaderPbr.get())->SetForceNonIBLMode(true);
	ScopeGuard sgIblMode {[hShaderPbr]() {
		static_cast<pragma::ShaderPBR*>(hShaderPbr.get())->SetForceNonIBLMode(false);
	}};

	auto oldRenderResolution = c_engine->GetRenderResolution();
	if(useRaytracing == false)
	{
		Con::cerr<<"ERROR: Custom render resolutions currently not supported for reflection probes!"<<Con::endl;
		c_engine->SetRenderResolution(Vector2i{CUBEMAP_LAYER_WIDTH,CUBEMAP_LAYER_HEIGHT});
	}

	auto oldProjMat = hCam->GetProjectionMatrix();
	auto oldViewMat = hCam->GetViewMatrix();

	auto mProj = glm::perspectiveRH<float>(glm::radians(90.0f),1.f,hCam->GetNearZ(),hCam->GetFarZ());
	mProj = glm::scale(mProj,Vector3(-1.f,-1.f,1.f));
	hCam->SetProjectionMatrix(mProj);

	auto img = CreateCubemapImage();
	for(uint8_t iLayer=0;iLayer<6;++iLayer)
	{
		hCam->SetViewMatrix(cubemapViewMatrices.at(iLayer));
		hCam->GetEntity().SetPosition(pos);
		hCam->UpdateViewMatrix(); // TODO: Remove this?

		auto drawCmd = c_engine->GetSetupCommandBuffer();
		scene->UpdateBuffers(drawCmd); // TODO: Remove this?

		// TODO: FRender::Reflection is required to flip the winding order, but why is this needed in the first place?
		c_game->RenderScene(drawCmd,*img,(FRender::All | FRender::HDR | FRender::Reflection) &~(FRender::View | FRender::Dynamic),iLayer);

		// We're flushing the command buffer for each layer
		// individually to make sure we're not gonna hit the TDR
		c_engine->FlushSetupCommandBuffer();
	}

	hCam->SetProjectionMatrix(oldProjMat);
	hCam->SetViewMatrix(oldViewMat);

	if(useRaytracing == false)
	{
		// Restore old render resolution TODO: Do this only once when capturing all cubemaps
		c_engine->SetRenderResolution(oldRenderResolution);
	}
	return FinalizeCubemap(*img);
}

bool CReflectionProbeComponent::FinalizeCubemap(prosper::Image &imgCubemap)
{
	auto drawCmd = c_engine->GetSetupCommandBuffer();
	// Generate cubemap mipmaps
	prosper::util::record_image_barrier(**drawCmd,*imgCubemap,Anvil::ImageLayout::TRANSFER_DST_OPTIMAL,Anvil::ImageLayout::TRANSFER_SRC_OPTIMAL);
	prosper::util::record_generate_mipmaps(
		**drawCmd,*imgCubemap,
		Anvil::ImageLayout::TRANSFER_SRC_OPTIMAL,
		Anvil::AccessFlagBits::TRANSFER_READ_BIT,
		Anvil::PipelineStageFlagBits::TRANSFER_BIT
	);
	c_engine->FlushSetupCommandBuffer();
	prosper::util::ImageViewCreateInfo imgViewCreateInfo {};
	prosper::util::SamplerCreateInfo samplerCreateInfo {};
	samplerCreateInfo.addressModeU = Anvil::SamplerAddressMode::CLAMP_TO_EDGE;
	samplerCreateInfo.addressModeV = Anvil::SamplerAddressMode::CLAMP_TO_EDGE;
	samplerCreateInfo.addressModeW = Anvil::SamplerAddressMode::CLAMP_TO_EDGE;
	samplerCreateInfo.minFilter = Anvil::Filter::LINEAR;
	samplerCreateInfo.magFilter = Anvil::Filter::LINEAR;
	auto tex = prosper::util::create_texture(c_engine->GetDevice(),{},imgCubemap.shared_from_this(),&imgViewCreateInfo,&samplerCreateInfo);

	Con::cout<<"Generating IBL reflection textures from reflection probe..."<<Con::endl;
	auto result = GenerateIBLReflectionsFromCubemap(*tex);
	if(result == false)
	{
		Con::cwar<<"WARNING: Generating IBL reflection textures has failed! Reflection probe will be unavailable."<<Con::endl;
		build_next_reflection_probe();
		return result;
	}

	auto success = (m_iblData && SaveIBLReflectionsToFile());
	m_bBakingFailed = !success;
	build_next_reflection_probe();
	/*auto &wgui = WGUI::GetInstance();
	auto *p = dynamic_cast<WITexturedCubemap*>(wgui.GetBaseElement()->FindDescendantByName(GUI_EL_NAME));
	if(p == nullptr)
	{
	p = wgui.Create<WITexturedCubemap>();
	p->SetName(GUI_EL_NAME);
	}
	p->SetTexture(*tex);
	p->SetSize(512,384);*/
	return success;
}

bool CReflectionProbeComponent::GenerateIBLReflectionsFromCubemap(prosper::Texture &cubemap)
{
	auto *shaderConvolute = static_cast<pragma::ShaderConvoluteCubemapLighting*>(c_engine->GetShader("convolute_cubemap_lighting").get());
	auto *shaderRoughness = static_cast<pragma::ShaderComputeIrradianceMapRoughness*>(c_engine->GetShader("compute_irradiance_map_roughness").get());
	auto *shaderBRDF = static_cast<pragma::ShaderBRDFConvolution*>(c_engine->GetShader("brdf_convolution").get());
	if(shaderConvolute == nullptr || shaderRoughness == nullptr || shaderBRDF == nullptr)
		return false;
	auto irradianceMap = shaderConvolute->ConvoluteCubemapLighting(cubemap,32);
	auto prefilterMap = shaderRoughness->ComputeRoughness(cubemap,128);

	TextureManager::LoadInfo loadInfo {};
	loadInfo.mipmapLoadMode = TextureMipmapMode::Ignore;
	loadInfo.flags = TextureLoadFlags::LoadInstantly;
	std::shared_ptr<prosper::Texture> brdfTex = nullptr;
	std::shared_ptr<void> texPtr = nullptr;

	// Load BRDF texture from disk, if it already exists
	if(static_cast<CMaterialManager&>(client->GetMaterialManager()).GetTextureManager().Load(*c_engine,"env/brdf.ktx",loadInfo,&texPtr) == true)
		brdfTex = std::static_pointer_cast<Texture>(texPtr)->GetVkTexture();

	// Otherwise generate it
	if(brdfTex == nullptr)
		brdfTex = shaderBRDF->CreateBRDFConvolutionMap(512);

	if(irradianceMap == nullptr || prefilterMap == nullptr)
		return false;
	m_iblDsg = nullptr;
	m_iblData = std::make_unique<rendering::IBLData>(irradianceMap,prefilterMap,brdfTex);
	InitializeDescriptorSet();
	
	// Debug test: Apply texture to skybox
	/*{
		for(auto &pair : client->GetMaterialManager().GetMaterials())
		{
			if(pair.first.find("skybox") == std::string::npos)
				continue;
			auto *texInfo = pair.second.get()->GetTextureInfo("skybox");
			std::static_pointer_cast<Texture>(texInfo->texture)->texture = m_iblData->prefilterMap;
			c_game->ReloadMaterialShader(static_cast<CMaterial*>(pair.second.get()));
			break;
		}
	}*/
	return true;
}

bool CReflectionProbeComponent::GenerateIBLReflectionsFromEnvMap(const std::string &envMapFileName)
{
	auto *shaderEquiRectToCubemap = static_cast<pragma::ShaderEquirectangularToCubemap*>(c_engine->GetShader("equirectangular_to_cubemap").get());
	if(shaderEquiRectToCubemap == nullptr)
		return false;
	auto pos = GetEntity().GetPosition();
	Con::cout<<"Generating reflection probe IBL reflections for probe at position ("<<pos.x<<","<<pos.y<<","<<pos.z<<") using environment map '"<<envMapFileName<<"'..."<<Con::endl;
	auto cubemapTex = shaderEquiRectToCubemap->LoadEquirectangularImage(envMapFileName,512);
	if(cubemapTex == nullptr)
		return false;
	return GenerateIBLReflectionsFromCubemap(*cubemapTex);
}
bool CReflectionProbeComponent::LoadIBLReflectionsFromFile()
{
	auto relPath = GetCubemapIBLMaterialPath();
	auto identifier = GetCubemapIdentifier();
	auto *mat = client->LoadMaterial(relPath +identifier +".wmi",true,false);
	if(mat == nullptr)
		return false;
	auto *pPrefilter = mat->GetTextureInfo("prefilter");
	auto *pIrradiance = mat->GetTextureInfo("irradiance");
	auto *pBrdf = mat->GetTextureInfo("brdf");
	if(pPrefilter == nullptr || pIrradiance == nullptr || pBrdf == nullptr)
		return false;
	auto texPrefilter = std::static_pointer_cast<Texture>(pPrefilter->texture);
	auto texIrradiance = std::static_pointer_cast<Texture>(pIrradiance->texture);
	auto texBrdf = std::static_pointer_cast<Texture>(pBrdf->texture);
	if(
		texPrefilter == nullptr || texPrefilter->HasValidVkTexture() == false ||
		texIrradiance == nullptr || texIrradiance->HasValidVkTexture() == false ||
		texBrdf == nullptr || texBrdf->HasValidVkTexture() == false
	)
		return false;
	m_iblDsg = nullptr;
	m_iblData = std::make_unique<rendering::IBLData>(texIrradiance->GetVkTexture(),texPrefilter->GetVkTexture(),texBrdf->GetVkTexture());

	// TODO: Do this properly (e.g. via material attributes)
	static auto brdfSamplerInitialized = false;
	if(brdfSamplerInitialized == false)
	{
		brdfSamplerInitialized = true;
		prosper::util::SamplerCreateInfo samplerCreateInfo {};
		samplerCreateInfo.addressModeU = Anvil::SamplerAddressMode::CLAMP_TO_EDGE;
		samplerCreateInfo.addressModeV = Anvil::SamplerAddressMode::CLAMP_TO_EDGE;
		samplerCreateInfo.addressModeW = Anvil::SamplerAddressMode::CLAMP_TO_EDGE;
		samplerCreateInfo.minFilter = Anvil::Filter::LINEAR;
		samplerCreateInfo.magFilter = Anvil::Filter::LINEAR;
		auto sampler = prosper::util::create_sampler(c_engine->GetDevice(),samplerCreateInfo);
		texIrradiance->GetVkTexture()->SetSampler(*sampler);

		samplerCreateInfo.mipmapMode = Anvil::SamplerMipmapMode::LINEAR;
		sampler = prosper::util::create_sampler(c_engine->GetDevice(),samplerCreateInfo);
		texPrefilter->GetVkTexture()->SetSampler(*sampler);
	}

	InitializeDescriptorSet();
	return true;
}
void CReflectionProbeComponent::InitializeDescriptorSet()
{
	m_iblDsg = nullptr;
	if(m_iblData == nullptr)
		return;
	auto &dev = c_engine->GetDevice();
	m_iblDsg = prosper::util::create_descriptor_set_group(dev,pragma::ShaderPBR::DESCRIPTOR_SET_PBR);
	auto &ds = *m_iblDsg->GetDescriptorSet();
	prosper::util::set_descriptor_set_binding_texture(ds,*m_iblData->irradianceMap,umath::to_integral(pragma::ShaderPBR::PBRBinding::IrradianceMap));
	prosper::util::set_descriptor_set_binding_texture(ds,*m_iblData->prefilterMap,umath::to_integral(pragma::ShaderPBR::PBRBinding::PrefilterMap));
	prosper::util::set_descriptor_set_binding_texture(ds,*m_iblData->brdfMap,umath::to_integral(pragma::ShaderPBR::PBRBinding::BRDFMap));

	(*m_iblDsg)->get_descriptor_set(0u)->update();
}
std::string CReflectionProbeComponent::GetCubemapIBLMaterialPath() const
{
	return "maps/" +c_game->GetMapName() +"/ibl/";
}
std::string CReflectionProbeComponent::GetCubemapIdentifier() const
{
	if(m_srcEnvMap.empty() == false)
		return std::to_string(std::hash<std::string>{}(m_srcEnvMap));
	auto pos = GetEntity().GetPosition();
	auto identifier = std::to_string(pos.x) +std::to_string(pos.y) +std::to_string(pos.z);
	return std::to_string(std::hash<std::string>{}(identifier));
}
Anvil::DescriptorSet *CReflectionProbeComponent::GetIBLDescriptorSet()
{
	return m_iblDsg ? (*m_iblDsg)->get_descriptor_set(0u) : nullptr;
}

const rendering::IBLData *CReflectionProbeComponent::GetIBLData() const {return m_iblData.get();}

////////

void CEnvReflectionProbe::Initialize()
{
	CBaseEntity::Initialize();
	AddComponent<CReflectionProbeComponent>();
}

////////

void Console::commands::debug_pbr_ibl(NetworkState *state,pragma::BasePlayerComponent *pl,std::vector<std::string> &argv)
{
	if(c_game == nullptr)
		return;
	const std::string name = "pbr_ibl_brdf";
	auto &wgui = WGUI::GetInstance();
	auto *pRoot = wgui.GetBaseElement();
	auto *p = pRoot->FindDescendantByName(name);
	if(p != nullptr)
	{
		p->Remove();
		return;
	}

	if(c_game == nullptr)
		return;

	EntityIterator entIt {*c_game};
	entIt.AttachFilter<TEntityIteratorFilterComponent<CReflectionProbeComponent>>();

	auto origin = pl->GetEntity().GetPosition();
	auto dClosest = std::numeric_limits<float>::max();
	BaseEntity *entClosest = nullptr;
	for(auto *ent : entIt)
	{
		auto trC = ent->GetTransformComponent();
		if(trC.expired())
			continue;
		auto d = uvec::distance_sqr(origin,trC->GetOrigin());
		if(d > dClosest)
			continue;
		dClosest = d;
		entClosest = ent;
	}

	if(entClosest == nullptr)
	{
		Con::cout<<"No reflection probe found!"<<Con::endl;
		return;
	}

	auto *cam = c_game->GetRenderCamera();
	if(cam)
		c_game->DrawLine(cam->GetEntity().GetPosition(),entClosest->GetPosition(),Color::Red,30.f);

	auto reflProbeC = entClosest->GetComponent<CReflectionProbeComponent>();
	if(reflProbeC.expired())
		return;
	auto *iblData = reflProbeC->GetIBLData();
	if(iblData == nullptr)
	{
		Con::cout<<"No IBL textures available for reflection probe!"<<Con::endl;
		return;
	}
	auto &brdfMap = iblData->brdfMap;
	auto &irradianceMap = iblData->irradianceMap;
	auto &prefilterMap = iblData->prefilterMap;

	auto *pElContainer = wgui.Create<WIBase>();
	pElContainer->SetAutoAlignToParent(true);
	pElContainer->SetName(name);
	pElContainer->TrapFocus(true);
	pElContainer->RequestFocus();

	auto *pFrameBrdf = wgui.Create<WIFrame>(pElContainer);
	pFrameBrdf->SetTitle("BRDF");
	auto *pBrdf = wgui.Create<WITexturedRect>(pFrameBrdf);
	pBrdf->SetSize(256,256);
	pBrdf->SetY(24);
	pBrdf->SetTexture(*brdfMap);
	pFrameBrdf->SizeToContents();
	pBrdf->SetAnchor(0.f,0.f,1.f,1.f);

	auto maxLod = brdfMap->GetImage()->GetMipmapCount();
	if(maxLod > 1)
	{
		auto *pSlider = wgui.Create<WISlider>(pBrdf);
		pSlider->SetSize(pSlider->GetParent()->GetWidth(),24);
		pSlider->SetRange(0.f,maxLod,0.01f);
		pSlider->SetPostFix(" LOD");
		auto hBrdf = pBrdf->GetHandle();
		pSlider->AddCallback("OnChange",FunctionCallback<void,float,float>::Create([hBrdf](float oldVal,float newVal) {
			if(hBrdf.IsValid() == false)
				return;
			static_cast<WITexturedRect*>(hBrdf.get())->SetLOD(newVal);
		}));
		pSlider->SetAnchor(0.f,0.f,1.f,0.f);
	}

	///

	auto *pFrameIrradiance = wgui.Create<WIFrame>(pElContainer);
	pFrameIrradiance->SetTitle("Irradiance");
	pFrameIrradiance->SetX(pFrameBrdf->GetRight());
	auto *pIrradiance = wgui.Create<WITexturedCubemap>(pFrameIrradiance);
	pIrradiance->SetY(24);
	pIrradiance->SetTexture(*irradianceMap);
	pFrameIrradiance->SizeToContents();
	pIrradiance->SetAnchor(0.f,0.f,1.f,1.f);

	maxLod = irradianceMap->GetImage()->GetMipmapCount();
	if(maxLod > 1)
	{
		auto *pSlider = wgui.Create<WISlider>(pIrradiance);
		pSlider->SetSize(pSlider->GetParent()->GetWidth(),24);
		pSlider->SetRange(0.f,maxLod,0.01f);
		pSlider->SetPostFix(" LOD");
		auto hIrradiance = pIrradiance->GetHandle();
		pSlider->AddCallback("OnChange",FunctionCallback<void,float,float>::Create([hIrradiance](float oldVal,float newVal) {
			if(hIrradiance.IsValid() == false)
				return;
			static_cast<WITexturedCubemap*>(hIrradiance.get())->SetLOD(newVal);
		}));
		pSlider->SetAnchor(0.f,0.f,1.f,0.f);
	}

	///

	auto *pFramePrefilter = wgui.Create<WIFrame>(pElContainer);
	pFramePrefilter->SetTitle("Prefilter");
	pFramePrefilter->SetY(pFrameIrradiance->GetBottom());
	auto *pPrefilter = wgui.Create<WITexturedCubemap>(pFramePrefilter);
	pPrefilter->SetY(24);
	pPrefilter->SetTexture(*prefilterMap);
	pFramePrefilter->SizeToContents();
	pPrefilter->SetAnchor(0.f,0.f,1.f,1.f);

	maxLod = prefilterMap->GetImage()->GetMipmapCount();
	if(maxLod > 1)
	{
		auto *pSlider = wgui.Create<WISlider>(pPrefilter);
		pSlider->SetSize(pSlider->GetParent()->GetWidth(),24);
		pSlider->SetRange(0.f,maxLod,0.01f);
		pSlider->SetPostFix(" LOD");
		auto hPrefilter = pPrefilter->GetHandle();
		pSlider->AddCallback("OnChange",FunctionCallback<void,float,float>::Create([hPrefilter](float oldVal,float newVal) {
			if(hPrefilter.IsValid() == false)
				return;
			static_cast<WITexturedCubemap*>(hPrefilter.get())->SetLOD(newVal);
		}));
		pSlider->SetAnchor(0.f,0.f,1.f,0.f);
	}
}
#pragma optimize("",on)
