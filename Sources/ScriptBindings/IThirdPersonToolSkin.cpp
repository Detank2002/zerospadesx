/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include "IThirdPersonToolSkin.h"
#include "ScriptManager.h"
#include <Core/Debug.h>

namespace spades {
	namespace client {
		ScriptIThirdPersonToolSkin::ScriptIThirdPersonToolSkin(asIScriptObject* obj) : obj(obj) {}

		void ScriptIThirdPersonToolSkin::SetOriginMatrix(Matrix4 m) {
			SPADES_MARK_FUNCTION_DEBUG();
			static ScriptFunction func("IThirdPersonToolSkin", "void set_OriginMatrix(Matrix4)");
			ScriptContextHandle ctx = func.Prepare();
			int r;
			r = ctx->SetObject((void*)obj);
			ScriptManager::CheckError(r);
			r = ctx->SetArgObject(0, &m);
			ScriptManager::CheckError(r);
			ctx.ExecuteChecked();
		}

		ScriptIVRWeaponSkin::ScriptIVRWeaponSkin(asIScriptObject* obj) : obj(obj) {}

		bool ScriptIVRWeaponSkin::ImplementsInterface() {
			return obj && obj->GetObjectType()->Implements(
			                obj->GetEngine()->GetTypeInfoByName("IVRWeaponSkin"));
		}

		void ScriptIVRWeaponSkin::SetVRHand(bool v) {
			SPADES_MARK_FUNCTION_DEBUG();
			static ScriptFunction func("IVRWeaponSkin", "void set_VRHand(bool)");
			ScriptContextHandle ctx = func.Prepare();
			int r;
			r = ctx->SetObject((void*)obj);
			ScriptManager::CheckError(r);
			r = ctx->SetArgByte(0, v ? 1 : 0);
			ScriptManager::CheckError(r);
			ctx.ExecuteChecked();
		}

		namespace {
			Vector3 GetVector(asIScriptObject* obj, ScriptFunction& func) {
				ScriptContextHandle ctx = func.Prepare();
				int r = ctx->SetObject((void*)obj);
				ScriptManager::CheckError(r);
				ctx.ExecuteChecked();
				return *reinterpret_cast<Vector3*>(ctx->GetReturnObject());
			}
		} // namespace

		Vector3 ScriptIVRWeaponSkin::GetVRSightRear() {
			SPADES_MARK_FUNCTION_DEBUG();
			static ScriptFunction func("IVRWeaponSkin", "Vector3 get_VRSightRear()");
			return GetVector(obj, func);
		}

		Vector3 ScriptIVRWeaponSkin::GetVRSightFront() {
			SPADES_MARK_FUNCTION_DEBUG();
			static ScriptFunction func("IVRWeaponSkin", "Vector3 get_VRSightFront()");
			return GetVector(obj, func);
		}

		Vector3 ScriptIVRWeaponSkin::GetVRReflexCenter() {
			SPADES_MARK_FUNCTION_DEBUG();
			static ScriptFunction func("IVRWeaponSkin", "Vector3 get_VRReflexCenter()");
			return GetVector(obj, func);
		}

		float ScriptIVRWeaponSkin::GetVRReflexRadius() {
			SPADES_MARK_FUNCTION_DEBUG();
			static ScriptFunction func("IVRWeaponSkin", "float get_VRReflexRadius()");
			ScriptContextHandle ctx = func.Prepare();
			int r = ctx->SetObject((void*)obj);
			ScriptManager::CheckError(r);
			ctx.ExecuteChecked();
			return ctx->GetReturnFloat();
		}

		class IThirdPersonToolSkinRegistrar : public ScriptObjectRegistrar {
		public:
			IThirdPersonToolSkinRegistrar() : ScriptObjectRegistrar("IThirdPersonToolSkin") {}
			virtual void Register(ScriptManager* manager, Phase phase) {
				asIScriptEngine* eng = manager->GetEngine();
				int r;
				eng->SetDefaultNamespace("spades");
				switch (phase) {
					case PhaseObjectType:
						r = eng->RegisterInterface("IThirdPersonToolSkin");
						manager->CheckError(r);
						r = eng->RegisterInterface("IVRWeaponSkin");
						manager->CheckError(r);
						break;
					case PhaseObjectMember:
						r = eng->RegisterInterfaceMethod("IThirdPersonToolSkin", "void set_OriginMatrix(Matrix4)");
						manager->CheckError(r);
						for (const char* decl :
						     {"void set_VRHand(bool)", "Vector3 get_VRSightRear()",
						      "Vector3 get_VRSightFront()", "Vector3 get_VRReflexCenter()",
						      "float get_VRReflexRadius()"}) {
							r = eng->RegisterInterfaceMethod("IVRWeaponSkin", decl);
							manager->CheckError(r);
						}
						break;
					default: break;
				}
			}
		};

		static IThirdPersonToolSkinRegistrar registrar;
	} // namespace client
} // namespace spades