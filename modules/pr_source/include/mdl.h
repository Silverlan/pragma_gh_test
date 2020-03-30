#ifndef __MDL_H__
#define __MDL_H__

#include <string>
#include <smdmodel.h>
#include "mdl_shared.h"
#include "mdl_bone.h"
#include "mdl_animation.h"
#include "mdl_attachment.h"
#include "mdl_hitboxset.h"
#include "mdl_sequence.h"
#include "mdl_bodypart.h"
#include "mdl_pose_parameter.h"
#include "mdl_flexdesc.h"
#include "mdl_flexcontroller.h"
#include "mdl_flexrule.h"
#include "mdl_optimize.h"
#include <pragma/model/animation/animation.h>
#include <pragma/pragma_module.hpp>
#include <optional>

class ModelSubMesh;
class NetworkState;
class Game;
class Model;

#define STUDIOHDR_FLAGS_AUTOGENERATED_HITBOX	1 << 0	
#define STUDIOHDR_FLAGS_USES_ENV_CUBEMAP	1 << 1	
#define STUDIOHDR_FLAGS_FORCE_OPAQUE	1 << 2	
#define STUDIOHDR_FLAGS_TRANSLUCENT_TWOPASS	1 << 3	
#define STUDIOHDR_FLAGS_STATIC_PROP	1 << 4	
#define STUDIOHDR_FLAGS_USES_FB_TEXTURE	1 << 5	
#define STUDIOHDR_FLAGS_HASSHADOWLOD	1 << 6	
#define STUDIOHDR_FLAGS_USES_BUMPMAPPING	1 << 7	
#define STUDIOHDR_FLAGS_USE_SHADOWLOD_MATERIALS	1 << 8	
#define STUDIOHDR_FLAGS_OBSOLETE	1 << 9	
#define STUDIOHDR_FLAGS_UNUSED	1 << 10	
#define STUDIOHDR_FLAGS_NO_FORCED_FADE	1 << 11	
#define STUDIOHDR_FLAGS_FORCE_PHONEME_CROSSFADE	1 << 12	
#define STUDIOHDR_FLAGS_CONSTANT_DIRECTIONAL_LIGHT_DOT	1 << 13	
#define STUDIOHDR_FLAGS_FLEXES_CONVERTED	1 << 14	
#define STUDIOHDR_FLAGS_BUILT_IN_PREVIEW_MODE	1 << 15	
#define STUDIOHDR_FLAGS_AMBIENT_BOOST	1 << 16	
#define STUDIOHDR_FLAGS_DO_NOT_CAST_SHADOWS	1 << 17	
#define STUDIOHDR_FLAGS_CAST_TEXTURE_SHADOWS	1 << 18

#pragma pack(push,1)
namespace import
{
	namespace mdl
	{
		namespace util
		{
			EulerAngles convert_rotation_matrix_to_degrees(float m0,float m1,float m2,float m3,float m4,float m5,float m8);
			Mat4 euler_angles_to_matrix(const EulerAngles &ang);
			void translate_matrix(Mat4 &m,const Vector3 &offset);
			Mat4 mul_matrix(const Mat4 &m1,const Mat4 &m2);
			Vector3 get_translation(const Mat4 &m);
			Quat get_rotation(const Mat4 &m);
			void rotation_to_axis_angle(Quat &rot,Vector3 &axis,float &angle);
			void axis_angle_to_rotation(Vector3 &axis,float angle,Quat &rot);
			void convert_rotation(Quat &rot);
			Vector3 transform_physics_vertex(const std::shared_ptr<mdl::Bone> &bone,const Vector3 &v,bool sourcePhyIsCol=false);
			Vector3 vectori_transform(const Vector3 &v,const Vector3 &matCol0,const Vector3 &matCol1,const Vector3 &matCol2,const Vector3 &matCol3);
		};
		struct studiohdr_t
		{
			int32_t id;
			int32_t version;
			int32_t checksum; // this has to be the same in the phy and vtx files to load!
			std::array<char,64> name;
			int32_t length;

			Vector3 eyeposition; // ideal eye position

			Vector3 illumposition; // illumination center
	
			Vector3 hull_min; // ideal movement hull size
			Vector3 hull_max;

			Vector3 view_bbmin; // clipping bounding box
			Vector3 view_bbmax;

			int32_t flags;

			int32_t numbones; // bones
			int32_t boneindex;

			int32_t numbonecontrollers; // bone controllers
			int32_t bonecontrollerindex;

			int32_t numhitboxsets;
			int32_t hitboxsetindex;

			int32_t numlocalanim; // animations/poses
			int32_t localanimindex; // animation descriptions

			int32_t numlocalseq; // sequences
			int32_t localseqindex;

			int32_t activitylistversion; // initialization flag - have the sequences been indexed?
			int32_t eventsindexed;

			int32_t numtextures;
			int32_t textureindex;

			// raw textures search paths
			int32_t numcdtextures;
			int32_t cdtextureindex;

			// replaceable textures tables
			int32_t numskinref;
			int32_t numskinfamilies;
			int32_t skinindex;

			int32_t numbodyparts;		
			int32_t bodypartindex;

			// queryable attachable points
			int32_t numlocalattachments;
			int32_t localattachmentindex;

			// animation node to animation node transition graph
			int32_t numlocalnodes;
			int32_t localnodeindex;
			int32_t localnodenameindex;

			int32_t numflexdesc;
			int32_t flexdescindex;

			int32_t numflexcontrollers;
			int32_t flexcontrollerindex;

			int32_t numflexrules;
			int32_t flexruleindex;

			int32_t numikchains;
			int32_t ikchainindex;

			int32_t nummouths;
			int32_t mouthindex;

			int32_t numlocalposeparameters;
			int32_t localposeparamindex;

			int32_t surfacepropindex;

			// Key values
			int32_t keyvalueindex;
			int32_t keyvaluesize;

			int32_t numlocalikautoplaylocks;
			int32_t localikautoplaylockindex;

			// The collision model mass that jay wanted
			float mass;
			int32_t contents;

			// external animations, models, etc.
			int32_t numincludemodels;
			int32_t includemodelindex;

			// implementation specific call to get a named model

			// implementation specific back pointer to virtual data
			int32_t virtualModel; // Pointer

			// for demand loaded animation blocks
			int32_t szanimblocknameindex;	
			int32_t numanimblocks;
			int32_t animblockindex;

			int32_t animblockModel; // Pointer

			int32_t bonetablebynameindex;

			// used by tools only that don't cache, but persist mdl's peer data
			// engine uses virtualModel to back link to cache pointers
			int32_t pVertexBase; // Pointer
			int32_t pIndexBase; // Pointer

			// if STUDIOHDR_FLAGS_CONSTANT_DIRECTIONAL_LIGHT_DOT is set,
			// this value is used to calculate directional components of lighting 
			// on static props
			byte constdirectionallightdot;

			// set during load of mdl data to track *desired* lod configuration (not actual)
			// the *actual* clamped root lod is found in studiohwdata
			// this is stored here as a global store to ensure the staged loading matches the rendering
			byte rootLOD;
	
			// set in the mdl data to specify that lod configuration should only allow first numAllowRootLODs
			// to be set as root LOD:
			//	numAllowedRootLODs = 0	means no restriction, any lod can be set as root lod.
			//	numAllowedRootLODs = N	means that lod0 - lod(N-1) can be set as root lod, but not lodN or lower.
			byte numAllowedRootLODs;

			std::array<byte,1> unused;

			int32_t unused4; // zero out if version < 47

			int32_t numflexcontrollerui;
			int32_t flexcontrolleruiindex;

			float flVertAnimFixedPointScale;

			std::array<int32_t,1> unused3;

			// FIXME: Remove when we up the model version. Move all fields of studiohdr2_t into studiohdr_t.
			int32_t studiohdr2index;

			// NOTE: No room to add stuff? Up the .mdl file format version 
			// [and move all fields in studiohdr2_t into studiohdr_t and kill studiohdr2_t],
			// or add your stuff to studiohdr2_t. See NumSrcBoneTransforms/SrcBoneTransform for the pattern to use.
			std::array<int32_t,1> unused2;
		};

		struct studiohdr2_t
		{
			// ??
			int32_t srcbonetransform_count;
			int32_t srcbonetransform_index;

			int32_t illumpositionattachmentindex;

			float flMaxEyeDeflection;	//  If set to 0, then equivalent to cos(30)

			// mstudiolinearbone_t
			int32_t linearbone_index;

			std::array<int32_t,64> unknown;
		};

		struct mstudiotexture_t
		{
			int32_t sznameindex;
			int32_t flags;
			int32_t used;
			int32_t unused1;
			int32_t material; // Pointer; fixme: this needs to go away . .isn't used by the engine, but is used by studiomdl
			int32_t clientmaterial; // Pointer; gary, replace with client material pointer if used
	
			std::array<int32_t,10> unused;
		};

		Vector3 transform_coordinate_system(const Vector3 &v);
		EulerAngles transform_coordinate_system(const EulerAngles &ang);
	};
	struct MdlInfo
	{
		MdlInfo(Model &mdl)
			: model(mdl)
		{}
		Model &model;
		mdl::studiohdr_t header;
		std::optional<mdl::studiohdr2_t> header2 = {};
		std::vector<std::string> textures;
		std::vector<std::string> texturePaths;
		std::vector<std::shared_ptr<mdl::Bone>> bones;
		std::vector<mdl::AnimationDesc> animationDescs;
		std::vector<std::shared_ptr<::Animation>> animations;
		std::vector<mdl::Sequence> sequences;
		std::vector<mdl::mstudioanimblock_t> animationBlocks;
		std::vector<std::shared_ptr<mdl::Attachment>> attachments;
		std::vector<mdl::HitboxSet> hitboxSets;
		std::vector<mdl::BodyPart> bodyParts;
		std::vector<mdl::PoseParameter> poseParameters;
		std::vector<mdl::FlexDesc> flexDescs;
		std::vector<mdl::FlexController> flexControllers;
		std::vector<mdl::FlexRule> flexRules;
		std::vector<mdl::FlexControllerUi> flexControllerUis;
		std::vector<std::vector<uint16_t>> skinFamilies;
		std::vector<std::vector<uint32_t>> fixedLodVertexIndices;
		void ConvertTransforms(const std::vector<std::shared_ptr<ModelSubMesh>> &meshesSkip,Animation *reference);
		void GenerateReference(const std::vector<std::shared_ptr<import::mdl::Bone>> &bones);
	};
	bool load_mdl(
		NetworkState *nw,const VFilePtr &f,const std::function<std::shared_ptr<Model>()> &fCreateModel,
		const std::function<bool(const std::shared_ptr<Model>&,const std::string&,const std::string&)> &fCallback,
		bool bCollision,MdlInfo &mdlInfo,std::ostream *optLog=nullptr
	);
	std::shared_ptr<Model> load_mdl(
		NetworkState *nw,const std::unordered_map<std::string,VFilePtr> &files,const std::function<std::shared_ptr<Model>()> &fCreateModel,
		const std::function<bool(const std::shared_ptr<Model>&,const std::string&,const std::string&)> &fCallback,bool bCollision,
		std::vector<std::string> &textures,std::ostream *optLog=nullptr
	);
	std::shared_ptr<Model> load_source2_mdl(
		Game &game,VFilePtr f,
		const std::function<bool(const std::shared_ptr<Model>&,const std::string&,const std::string&)> &fCallback,bool bCollision,
		std::vector<std::string> &textures,std::ostream *optLog=nullptr
	);
};
#pragma pack(pop)

extern "C" {
	PRAGMA_EXPORT bool convert_hl2_model(NetworkState *nw,const std::function<std::shared_ptr<Model>()> &fCreateModel,const std::function<bool(const std::shared_ptr<Model>&,const std::string&,const std::string&)> &fCallback,const std::string &path,const std::string &mdlName,std::ostream *optLog);
};

#endif
