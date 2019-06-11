#include "stdafx_client.h"
#include "pragma/rendering/shaders/world/raytracing/c_shader_raytracing.hpp"
#include <misc/compute_pipeline_create_info.h>

using namespace pragma;

extern DLLCENGINE CEngine *c_engine;

#pragma optimize("",off)
decltype(ShaderRayTracing::DESCRIPTOR_SET_IMAGE) ShaderRayTracing::DESCRIPTOR_SET_IMAGE = {
	{
		prosper::Shader::DescriptorSetInfo::Binding { // Image
			Anvil::DescriptorType::STORAGE_IMAGE,
			Anvil::ShaderStageFlagBits::COMPUTE_BIT
		}
	}
};
decltype(ShaderRayTracing::DESCRIPTOR_SET_BUFFERS) ShaderRayTracing::DESCRIPTOR_SET_BUFFERS = {
	{
		prosper::Shader::DescriptorSetInfo::Binding { // Spheres
			Anvil::DescriptorType::STORAGE_BUFFER,
			Anvil::ShaderStageFlagBits::COMPUTE_BIT
		},
		prosper::Shader::DescriptorSetInfo::Binding { // Planes
			Anvil::DescriptorType::STORAGE_BUFFER,
			Anvil::ShaderStageFlagBits::COMPUTE_BIT
		},
		prosper::Shader::DescriptorSetInfo::Binding { // Uniform App
			Anvil::DescriptorType::UNIFORM_BUFFER,
			Anvil::ShaderStageFlagBits::COMPUTE_BIT
		},
		prosper::Shader::DescriptorSetInfo::Binding { // Vertex Buffer
			Anvil::DescriptorType::STORAGE_BUFFER,
			Anvil::ShaderStageFlagBits::COMPUTE_BIT
		},
		prosper::Shader::DescriptorSetInfo::Binding { // Index Buffer
			Anvil::DescriptorType::STORAGE_BUFFER,
			Anvil::ShaderStageFlagBits::COMPUTE_BIT
		}
	}
};
ShaderRayTracing::ShaderRayTracing(prosper::Context &context,const std::string &identifier)
	: prosper::ShaderCompute(context,identifier,"world/raytracing/raytracing.gls")
{}

void ShaderRayTracing::InitializeComputePipeline(Anvil::ComputePipelineCreateInfo &pipelineInfo,uint32_t pipelineIdx)
{
	prosper::ShaderCompute::InitializeComputePipeline(pipelineInfo,pipelineIdx);

	// Currently not supported on some GPUs?
	// AddSpecializationConstant(pipelineInfo,0u /* constant id */,sizeof(TILE_SIZE),&TILE_SIZE);

	AttachPushConstantRange(pipelineInfo,0u,sizeof(PushConstants),Anvil::ShaderStageFlagBits::COMPUTE_BIT);

	AddDescriptorSetGroup(pipelineInfo,DESCRIPTOR_SET_IMAGE);
	AddDescriptorSetGroup(pipelineInfo,DESCRIPTOR_SET_BUFFERS);
}

bool ShaderRayTracing::Compute(Anvil::DescriptorSet &descSetImage,Anvil::DescriptorSet &descSetBuffers,uint32_t workGroupsX,uint32_t workGroupsY)
{
	return RecordBindDescriptorSet(descSetImage,DESCRIPTOR_SET_IMAGE.setIndex) &&
		RecordBindDescriptorSet(descSetBuffers,DESCRIPTOR_SET_BUFFERS.setIndex) &&
		RecordDispatch(workGroupsX,workGroupsY);
}

////////////////////////

#include <image/prosper_sampler.hpp>
#include <prosper_descriptor_set_group.hpp>

namespace rtx
{
struct Material
{
	Vector3 color;
	int type;

	Material();
	Material(Vector3, int);
};
Material::Material() { color = Vector3(0, 0, 0);  type = 1; }
Material::Material(Vector3 color, int type) : color(color), type(type) {}

struct Planee
{
	Vector3 normal;
	float distance;

	Material mat;

	Planee();
	Planee(Vector3 normal, float distance);
};
Planee::Planee() { distance = 1; }
Planee::Planee(Vector3 normal, float distance) : normal(normal), distance(distance) {}

struct Sphere
{
	Vector3 position;
	float radius;

	Material mat;

	Sphere();
	Sphere(Vector3 position, float radius);
};

Sphere::Sphere() { radius = 1; }
Sphere::Sphere(Vector3 position, float radius) : position(position), radius(radius) {}
};

struct RTXTest
{
	std::shared_ptr<prosper::Texture> texture;
	std::shared_ptr<prosper::Buffer> sphereBuffer;
	std::shared_ptr<prosper::Buffer> planeBuffer;
	std::shared_ptr<prosper::Buffer> uniformBuffer;

	std::shared_ptr<prosper::DescriptorSetGroup> dsgImage;
	std::shared_ptr<prosper::DescriptorSetGroup> dsgBuffers;

	uint32_t numTris;
};

static void InitGameObjects(std::vector<rtx::Planee> &planes, std::vector<rtx::Sphere> &spheres)
{
	auto AddPlanee = [&planes](rtx::Planee* go, Vector3 color, int type)
	{
		go->mat = rtx::Material(color, type);
		planes.push_back(*go);
	};

	auto AddSphere = [&spheres](rtx::Sphere* go, Vector3 color, int type)
	{
		go->mat = rtx::Material(color, type);
		spheres.push_back(*go);
	};

	auto sphere = new rtx::Sphere(Vector3(-0.55, -1.55, -4.0), 1.0);
	auto sphere2 = new rtx::Sphere(Vector3(1.3, 1.2, -4.2), 0.8);

	auto bottom = new rtx::Planee(Vector3(0, 1, 0), 2.5);
	auto back = new rtx::Planee(Vector3(0, 0, 1), 5.5);
	auto left = new rtx::Planee(Vector3(1, 0, 0), 2.75);
	auto right = new rtx::Planee(Vector3(-1, 0, 0), 2.75);
	auto ceiling = new rtx::Planee(Vector3(0, -1, 0), 3.0);
	auto front = new rtx::Planee(Vector3(0, 0, -1), 0.5);


	AddSphere(sphere, Vector3(0.3, 0.9, 0.76), 2);
	AddSphere(sphere2, Vector3(0.062, 0.917, 0.078), 1);

	AddPlanee(bottom, Vector3(0.8, 0.8, 0.8), 1);
	AddPlanee(back, Vector3(0.8, 0.8, 0.8), 1);
	AddPlanee(left, Vector3(1, 0.250, 0.019), 1);
	AddPlanee(right, Vector3(0.007, 0.580, 0.8), 1);
	AddPlanee(ceiling, Vector3(0.8, 0.8, 0.8), 1);
	AddPlanee(front, Vector3(0.8, 0.8, 0.8), 1);
}

#include "pragma/model/c_model.h"
#include "pragma/model/c_modelmesh.h"
#include "pragma/model/vk_mesh.h"

static std::pair<std::shared_ptr<prosper::Buffer>,std::shared_ptr<prosper::Buffer>> create_box_mesh(const Vector3 &cmin,const Vector3 &cmax,uint32_t &outNumTris)
{
	auto min = cmin;
	auto max = cmax;
	uvec::to_min_max(min,max);
	//auto mesh = std::make_shared<CModelSubMesh>();
	std::vector<Vector3> uniqueVertices {
		min, // 0
		Vector3(max.x,min.y,min.z), // 1
		Vector3(max.x,min.y,max.z), // 2
		Vector3(max.x,max.y,min.z), // 3
		max, // 4
		Vector3(min.x,max.y,min.z), // 5
		Vector3(min.x,min.y,max.z), // 6
		Vector3(min.x,max.y,max.z) // 7
	};
	std::vector<Vector3> verts {
		uniqueVertices[0],uniqueVertices[6],uniqueVertices[7], // 1
		uniqueVertices[0],uniqueVertices[7],uniqueVertices[5], // 1
		uniqueVertices[3],uniqueVertices[0],uniqueVertices[5], // 2
		uniqueVertices[3],uniqueVertices[1],uniqueVertices[0], // 2
		uniqueVertices[2],uniqueVertices[0],uniqueVertices[1], // 3
		uniqueVertices[2],uniqueVertices[6],uniqueVertices[0], // 3
		uniqueVertices[7],uniqueVertices[6],uniqueVertices[2], // 4
		uniqueVertices[4],uniqueVertices[7],uniqueVertices[2], // 4
		uniqueVertices[4],uniqueVertices[1],uniqueVertices[3], // 5
		uniqueVertices[1],uniqueVertices[4],uniqueVertices[2], // 5
		uniqueVertices[4],uniqueVertices[3],uniqueVertices[5], // 6
		uniqueVertices[4],uniqueVertices[5],uniqueVertices[7], // 6
	};
	std::vector<Vector3> faceNormals {
		Vector3(-1,0,0),Vector3(-1,0,0),
		Vector3(0,0,-1),Vector3(0,0,-1),
		Vector3(0,-1,0),Vector3(0,-1,0),
		Vector3(0,0,1),Vector3(0,0,1),
		Vector3(1,0,0),Vector3(1,0,0),
		Vector3(0,1,0),Vector3(0,1,0)
	};
	std::vector<::Vector2> uvs {
		::Vector2(0,1),::Vector2(1,1),::Vector2(1,0), // 1
		::Vector2(0,1),::Vector2(1,0),::Vector2(0,0), // 1
		::Vector2(0,0),::Vector2(1,1),::Vector2(1,0), // 2
		::Vector2(0,0),::Vector2(0,1),::Vector2(1,1), // 2
		::Vector2(0,1),::Vector2(1,0),::Vector2(0,0), // 3
		::Vector2(0,1),::Vector2(1,1),::Vector2(1,0), // 3
		::Vector2(0,0),::Vector2(0,1),::Vector2(1,1), // 4
		::Vector2(1,0),::Vector2(0,0),::Vector2(1,1), // 4
		::Vector2(0,0),::Vector2(1,1),::Vector2(1,0), // 5
		::Vector2(1,1),::Vector2(0,0),::Vector2(0,1), // 5
		::Vector2(1,1),::Vector2(1,0),::Vector2(0,0), // 6
		::Vector2(1,1),::Vector2(0,0),::Vector2(0,1) // 6
	};
	for(auto &uv : uvs)
		uv.y = 1.f -uv.y;

	std::vector<Vector4> meshVerts {};
	std::vector<uint16_t> meshTris {};
	for(auto i=decltype(verts.size()){0};i<verts.size();i+=3)
	{
		auto &n = faceNormals[i /3];
		//mesh->AddVertex(::Vertex{verts[i],uvs[i],n});
		//mesh->AddVertex(::Vertex{verts[i +1],uvs[i +1],n});
		//mesh->AddVertex(::Vertex{verts[i +2],uvs[i +2],n});
		meshVerts.push_back({verts[i].x,verts[i].y,verts[i].z,0.f});
		meshVerts.push_back({verts[i +1].x,verts[i +1].y,verts[i +1].z,0.f});
		meshVerts.push_back({verts[i +2].x,verts[i +2].y,verts[i +2].z,0.f});

		//mesh->AddTriangle(static_cast<uint32_t>(i),static_cast<uint32_t>(i +1),static_cast<uint32_t>(i +2));
		meshTris.push_back(i);
		meshTris.push_back(i +1);
		meshTris.push_back(i +2);
	}
	//mesh->SetTexture(0);
	//mesh->Update();

	prosper::util::BufferCreateInfo createInfo {};
	createInfo.memoryFeatures = prosper::util::MemoryFeatureFlags::DeviceLocal;
	createInfo.size = meshVerts.size() *sizeof(meshVerts.front());
	createInfo.usageFlags = Anvil::BufferUsageFlagBits::STORAGE_BUFFER_BIT;
	auto vertexBuffer = prosper::util::create_buffer(c_engine->GetDevice(),createInfo,meshVerts.data());

	createInfo.size = meshTris.size() *sizeof(meshTris.front());
	createInfo.usageFlags = Anvil::BufferUsageFlagBits::STORAGE_BUFFER_BIT;
	auto indexBuffer = prosper::util::create_buffer(c_engine->GetDevice(),createInfo,meshTris.data());

	outNumTris = meshTris.size() /3;
	return {vertexBuffer,indexBuffer};
}

RTXTest rtxTest {};
void ShaderRayTracing::Test()
{
	auto &dev = c_engine->GetDevice();
	prosper::util::ImageCreateInfo imgCreateInfo {};
	imgCreateInfo.format = Anvil::Format::R8G8B8A8_UNORM;
	imgCreateInfo.height = 256;
	imgCreateInfo.width = 256;
	imgCreateInfo.memoryFeatures = prosper::util::MemoryFeatureFlags::GPUBulk;
	imgCreateInfo.tiling = Anvil::ImageTiling::OPTIMAL;
	imgCreateInfo.usage = Anvil::ImageUsageFlagBits::STORAGE_BIT | Anvil::ImageUsageFlagBits::TRANSFER_SRC_BIT;
	imgCreateInfo.usage |= Anvil::ImageUsageFlagBits::SAMPLED_BIT;

	auto img = prosper::util::create_image(dev,imgCreateInfo);
	
	prosper::util::ImageViewCreateInfo imgViewCreateInfo {};
	prosper::util::SamplerCreateInfo samplerCreateInfo {};
	auto tex = prosper::util::create_texture(dev,prosper::util::TextureCreateInfo{},img,&imgViewCreateInfo,&samplerCreateInfo);
	rtxTest.texture = tex;

	auto descSetImage = prosper::util::create_descriptor_set_group(dev,DESCRIPTOR_SET_IMAGE);
	prosper::util::set_descriptor_set_binding_storage_image(*(*descSetImage)->get_descriptor_set(0),*tex,0u);
	rtxTest.dsgImage = descSetImage;

	std::vector<rtx::Planee> planes;
	std::vector<rtx::Sphere> spheres;
	InitGameObjects(planes,spheres);

	prosper::util::BufferCreateInfo bufCreateInfo {};
	bufCreateInfo.memoryFeatures = prosper::util::MemoryFeatureFlags::CPUToGPU;
	bufCreateInfo.usageFlags = Anvil::BufferUsageFlagBits::STORAGE_BUFFER_BIT;
	bufCreateInfo.size = spheres.size() *sizeof(spheres.front());
	auto sphereBuffer = prosper::util::create_buffer(dev,bufCreateInfo,spheres.data());
	rtxTest.sphereBuffer = sphereBuffer;

	bufCreateInfo.size = planes.size() *sizeof(planes.front());
	auto planeBuffer = prosper::util::create_buffer(dev,bufCreateInfo,planes.data());
	rtxTest.planeBuffer = planeBuffer;

	struct App
	{
		float time;
	} app;


	bufCreateInfo.usageFlags = Anvil::BufferUsageFlagBits::UNIFORM_BUFFER_BIT;
	bufCreateInfo.size = sizeof(app);
	auto uniformBuffer = prosper::util::create_buffer(dev,bufCreateInfo,&app);
	rtxTest.uniformBuffer = uniformBuffer;

	auto descSetBuffers = prosper::util::create_descriptor_set_group(dev,DESCRIPTOR_SET_BUFFERS);
	prosper::util::set_descriptor_set_binding_storage_buffer(*(*descSetBuffers)->get_descriptor_set(0),*sphereBuffer,0);
	prosper::util::set_descriptor_set_binding_storage_buffer(*(*descSetBuffers)->get_descriptor_set(0),*planeBuffer,1);
	prosper::util::set_descriptor_set_binding_uniform_buffer(*(*descSetBuffers)->get_descriptor_set(0),*uniformBuffer,2);
	rtxTest.dsgBuffers = descSetBuffers;

	// Model test
	auto mdl = c_engine->GetClientState()->GetGameState()->LoadModel("player/soldier.wmd");
	//auto &subMesh = mdl->GetMeshGroup(0)->GetMeshes().at(0)->GetSubMeshes().at(0);
	static auto buffers = create_box_mesh({-1.f,-1.f,-1.f},{1.f,1.f,1.f},rtxTest.numTris);
	//subMesh->Update(ModelUpdateFlags::UpdateBuffers);
	//auto &vkMesh = static_cast<CModelSubMesh*>(subMesh.get())->GetVKMesh();
	auto &vertBuffer = buffers.first;//vkMesh->GetVertexBuffer();
	auto &indexBuffer = buffers.second;//vkMesh->GetIndexBuffer();

	prosper::util::set_descriptor_set_binding_storage_buffer(*(*descSetBuffers)->get_descriptor_set(0),*vertBuffer,3);
	prosper::util::set_descriptor_set_binding_storage_buffer(*(*descSetBuffers)->get_descriptor_set(0),*indexBuffer,4);
}

#include <wgui/wgui.h>
#include <wgui/types/wirect.h>
#include <prosper_command_buffer.hpp>
bool ShaderRayTracing::ComputeTest()
{
	if(rtxTest.dsgBuffers == nullptr)
	{
		Test();

		auto &wgui = WGUI::GetInstance();
		auto *p = wgui.Create<WITexturedRect>();
		p->SetTexture(*rtxTest.texture);
		p->SetSize(1024,1024);
	}

	prosper::util::record_image_barrier(
		**c_engine->GetDrawCommandBuffer(),**rtxTest.texture->GetImage(),
		Anvil::PipelineStageFlagBits::FRAGMENT_SHADER_BIT,Anvil::PipelineStageFlagBits::COMPUTE_SHADER_BIT,
		Anvil::ImageLayout::SHADER_READ_ONLY_OPTIMAL,Anvil::ImageLayout::GENERAL,
		Anvil::AccessFlagBits::SHADER_READ_BIT,Anvil::AccessFlagBits::SHADER_WRITE_BIT
	);

	PushConstants pushConstants {rtxTest.numTris};

	auto swapChainWidth = 256;
	auto swapChainHeight = 256;
	auto result = RecordBindDescriptorSet(*rtxTest.dsgImage->GetAnvilDescriptorSetGroup().get_descriptor_set(0),DESCRIPTOR_SET_IMAGE.setIndex) &&
		RecordBindDescriptorSet(*rtxTest.dsgBuffers->GetAnvilDescriptorSetGroup().get_descriptor_set(0),DESCRIPTOR_SET_BUFFERS.setIndex) &&
		RecordPushConstants(pushConstants) &&
		RecordDispatch(swapChainWidth, swapChainHeight);

	prosper::util::record_image_barrier(
		**c_engine->GetDrawCommandBuffer(),**rtxTest.texture->GetImage(),
		Anvil::PipelineStageFlagBits::COMPUTE_SHADER_BIT,Anvil::PipelineStageFlagBits::FRAGMENT_SHADER_BIT,
		Anvil::ImageLayout::GENERAL,Anvil::ImageLayout::SHADER_READ_ONLY_OPTIMAL,
		Anvil::AccessFlagBits::SHADER_WRITE_BIT,Anvil::AccessFlagBits::SHADER_READ_BIT
	);
	return result;
}
#pragma optimize("",on)
