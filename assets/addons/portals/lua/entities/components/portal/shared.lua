--[[
    Copyright (C) 2021 Silverlan

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.
]]

local Component = util.register_class("ents.PortalComponent",BaseEntityComponent)

Component:RegisterMember("Target",ents.MEMBER_TYPE_ENTITY,"",{onChange = CLIENT and function(self) self:UpdateTarget() end or nil},"def+trans")
Component:RegisterMember("PortalOrigin",ents.MEMBER_TYPE_VECTOR3,"",{onChange = function(self) self:UpdatePortalOrigin() self:InvokeEventCallbacks(ents.PortalComponent.EVENT_ON_PORTAL_ORIGIN_CHANGED) end},"def+trans")
Component:RegisterMember("Mirrored",ents.MEMBER_TYPE_BOOLEAN,false,{onChange = function(self) self:UpdateMirrored() end},"def+trans+is")

function Component:Initialize()
	BaseEntityComponent.Initialize(self)

	self:AddEntityComponent(ents.COMPONENT_MODEL)
	self:AddEntityComponent(ents.COMPONENT_RENDER)
	self.m_relativePortalOrigin = Vector()
	self:SetReflectionPlane(math.Plane(Vector(0,0,1),0))
	self:UpdatePortalOrigin()
	if(CLIENT) then
		self:SetResolution(1024,1024)

		self.m_drawSceneInfo = game.DrawSceneInfo()
		self.m_drawSceneInfo.toneMapping = shader.TONE_MAPPING_NONE

		self:SetMirrored(false)
	end

	if(SERVER) then
		self:AddEntityComponent(ents.COMPONENT_PHYSICS)
		self:AddEntityComponent(ents.COMPONENT_TOUCH)
		self:SetTickPolicy(ents.TICK_POLICY_NEVER)

		self:BindEvent(ents.TouchComponent.EVENT_ON_START_TOUCH,"OnStartTouch")
		self:BindEvent(ents.TouchComponent.EVENT_ON_END_TOUCH,"OnEndTouch")
		self:BindEvent(ents.TouchComponent.EVENT_CAN_TRIGGER,"CanTrigger")
	end
	self:BindEvent(ents.TransformComponent.EVENT_ON_POSE_CHANGED,"OnPoseChanged")
end

function Component:OnEntitySpawn()
	if(CLIENT) then self:InitializeReflectionScene() end
	self:UpdateTarget()
end

function Component:UpdateMirrored()
	if(CLIENT) then
		self.m_drawSceneInfo.flags = self:IsMirrored() and bit.bor(self.m_drawSceneInfo.flags,game.DrawSceneInfo.FLAG_REFLECTION_BIT) or bit.band(self.m_drawSceneInfo.flags,bit.bnot(game.DrawSceneInfo.FLAG_REFLECTION_BIT))
		return
	end
	self:UpdateMirrorState()
end

function Component:OnPoseChanged()
	self:UpdatePortalOrigin()
end

function Component:UpdateTarget()
	self.m_posesDirty = true

	util.remove({self.m_cbOnTargetPoseChanged,self.m_cbOnTargetPoseChanged})
	local target = self:GetTarget()
	local trC = util.is_valid(target) and target:GetComponent(ents.COMPONENT_TRANSFORM) or nil
	if(trC == nil) then return end
	self.m_cbOnTargetPoseChanged = trC:AddEventCallback(ents.TransformComponent.EVENT_ON_POSE_CHANGED,function() self.m_posesDirty = true end)

	local portalC = target:GetComponent(ents.COMPONENT_PORTAL)
	if(portalC ~= nil) then self.m_cbOnTargetPoseChanged = portalC:AddEventCallback(ents.PortalComponent.EVENT_ON_PORTAL_ORIGIN_CHANGED,function() self.m_posesDirty = true end) end
end

function Component:OnRemove()
	util.remove({self.m_cbOnTargetPoseChanged,self.m_cbOnTargetPoseChanged})

	if(SERVER) then return end
	util.remove({self.m_dbgElTex,self.m_cbRenderScenes})
	self:ClearScene()
end

function Component:UpdatePoses()
	if(self.m_posesDirty ~= true) then return end
	self.m_posesDirty = nil

	local srcPos = self:GetEntity():GetPose() *self:GetRelativePortalOrigin()
	local srcRot = self:GetPlaneRotation()
	local srcPose = math.Transform(srcPos,srcRot)

	local tgtPose = srcPose:Copy()
	local tgtPlane = self:GetReflectionPlane()

	local target = self:GetTarget()
	if(util.is_valid(target)) then
		local portalC = target:GetComponent(ents.COMPONENT_PORTAL)
		if(portalC ~= nil) then
			local tgtPos = target:GetPose() *portalC:GetRelativePortalOrigin()
			local tgtRot = portalC:GetPlaneRotation()
			tgtPose = math.Transform(tgtPos,tgtRot)
			tgtPlane = portalC:GetReflectionPlane()
		else
			tgtPose = target:GetPose()
			tgtPlane = tgtPose:ToPlane()
		end
	end
	self.m_surfacePose = srcPose
	self.m_targetPose = tgtPose
	self.m_targetPlane = tgtPlane

	if(CLIENT) then
		self.m_drawSceneInfo.pvsOrigin = self:GetTargetPose():GetOrigin()
		self.m_drawSceneInfo.clipPlane = Vector4(tgtPlane:GetNormal(),tgtPlane:GetDistance())
	end
end

function Component:UpdatePortalOrigin()
	local origin = self:GetPortalOrigin()
	self.m_relativePortalOrigin = self:GetEntity():GetPose():GetInverse() *origin
	self.m_posesDirty = true
end

function Component:GetRelativePortalOrigin() return self.m_relativePortalOrigin end

function Component:GetPlaneRotation()
	local n = self.m_plane:GetNormal()
	local d = self.m_plane:GetDistance()
	local up = vector.UP -vector.UP:Project(n)
	up:Normalize()
	return Quaternion(n,up)
end

function Component:SetSurfacePose(pose)
	self.m_surfacePose = pose
	self:SetReflectionPlane(math.Plane(pose:GetForward(),pose:GetRight(),pose:GetUp()))
end
function Component:GetSurfacePose() self:UpdatePoses() return self.m_surfacePose end
function Component:SetTargetPose(pose) self.m_targetPose = pose end
function Component:GetTargetPose() self:UpdatePoses() return self.m_targetPose end
function Component:GetTargetPlane() self:UpdatePoses() return self.m_targetPlane end

function Component:SetReflectionPlane(plane)
	self.m_plane = plane
	self.m_posesDirty = true
end
function Component:GetReflectionPlane() return self.m_plane end

local rot180Yaw = EulerAngles(0,180,0):ToQuaternion()
function Component:ProjectPoseToTarget(pose)
	local newPose = self:GetSurfacePose():GetInverse() *pose
	local rot = newPose:GetRotation()
	rot = rot180Yaw *rot
	if(self:IsMirrored()) then rot:MirrorAxis(math.AXIS_X) end
	newPose:SetRotation(rot)
	local pos = newPose:GetOrigin()
	if(self:IsMirrored() == false) then pos:Rotate(rot180Yaw)
	else pos.z = -pos.z end
	newPose:SetOrigin(pos)
	return self:GetTargetPose() *newPose
end
ents.COMPONENT_PORTAL = ents.register_component("portal",Component,ents.EntityComponent.FREGISTER_BIT_NETWORKED)
Component.EVENT_ON_PORTAL_ORIGIN_CHANGED = ents.register_component_event(ents.COMPONENT_PORTAL,"on_portal_origin_changed")