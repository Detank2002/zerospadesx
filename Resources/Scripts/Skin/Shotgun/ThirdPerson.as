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

namespace spades {
	class ThirdPersonShotgunSkin : IToolSkin, IThirdPersonToolSkin, IWeaponSkin, IWeaponSkin2, IWeaponSkin3, IVRWeaponSkin {
		private float sprintState;
		private float raiseState;
		private Vector3 teamColor;
		private bool muted;
		private Matrix4 originMatrix;
		private float aimDownSightState;
		private float readyState;
		private bool reloading;
		private float reloadProgress;
		private int ammo, clipSize;

		private float environmentRoom;
		private float environmentSize;
		private float environmentDistance;
		private Vector3 soundOrigin;

		float SprintState { set { sprintState = value; } }
		float RaiseState { set { raiseState = value; } }
		Vector3 TeamColor { set { teamColor = value; } }
		bool IsMuted { set { muted = value; } }
		Matrix4 OriginMatrix { set { originMatrix = value; } }
		float AimDownSightState { set { aimDownSightState = value; } }
		bool IsReloading { set { reloading = value; } }
		float ReloadProgress { set { reloadProgress = value; } }
		int Ammo { set { ammo = value; } }
		int ClipSize { set { clipSize = value; } }
		float ReadyState { set { readyState = value; } }

		// IWeaponSkin2
		void SetSoundEnvironment(float room, float size, float distance) {
			environmentRoom = room;
			environmentSize = size;
			environmentDistance = distance;
		}
		Vector3 SoundOrigin { set { soundOrigin = value; } }

		// IVRWeaponSkin: in a VR player's own hand the gun is the first-person one,
		// with its own sights (View.as's models, attachments and sizes, at this
		// skin's scale): what you aim down. Everyone else sees Weapon.kv6.
		private bool vrHand = false;
		private ConfigItem cg_reflexScope("cg_reflexScope", "0");
		private Model@ vrGunModel;
		private Model@ vrPartModel;
		private Model@ vrRearSightModel;
		private Model@ vrSightFrontModel;
		private Model@ vrReflexSightModel;

		private Renderer@ renderer;
		private AudioDevice@ audioDevice;
		private Model@ model;

		private AudioChunk@ fireSound;
		private AudioChunk@ fireFarSound;
		private AudioChunk@ fireStereoSound;
		private AudioChunk@ fireSmallReverbSound;
		private AudioChunk@ fireLargeReverbSound;
		private AudioChunk@ reloadSound;
		private AudioChunk@ cockSound;

		ThirdPersonShotgunSkin(Renderer@ r, AudioDevice@ dev) {
			@renderer = r;
			@audioDevice = dev;
			@model = renderer.RegisterModel("Models/Weapons/Shotgun/Weapon.kv6");
			@vrGunModel = renderer.RegisterModel("Models/Weapons/Shotgun/WeaponNoPump.kv6");
			@vrPartModel = renderer.RegisterModel("Models/Weapons/Shotgun/Pump.kv6");
			@vrRearSightModel = renderer.RegisterModel("Models/Weapons/Shotgun/SightRear.kv6");
			@vrSightFrontModel = renderer.RegisterModel("Models/Weapons/Shotgun/SightFront.kv6");
			@vrReflexSightModel = renderer.RegisterModel("Models/Weapons/Shotgun/SightReflex.kv6");
			@fireSound = dev.RegisterSound("Sounds/Weapons/Shotgun/Fire.opus");
			@fireFarSound = dev.RegisterSound("Sounds/Weapons/Shotgun/FireFar.opus");
			@fireStereoSound = dev.RegisterSound("Sounds/Weapons/Shotgun/FireStereo.opus");
			@fireSmallReverbSound = dev.RegisterSound("Sounds/Weapons/Shotgun/V2AmbienceSmall.opus");
			@fireLargeReverbSound = dev.RegisterSound("Sounds/Weapons/Shotgun/V2AmbienceLarge.opus");
			@reloadSound = dev.RegisterSound("Sounds/Weapons/Shotgun/Reload.opus");
			@cockSound = dev.RegisterSound("Sounds/Weapons/Shotgun/Cock.opus");
		}

		void Update(float dt) {}

		void WeaponFired() {
			if (!muted) {
				Vector3 origin = soundOrigin;
				AudioParam param;
				param.volume = 8.0F;
				audioDevice.Play(fireSound, origin, param);

				param.volume = 8.0F * environmentRoom;
				audioDevice.Play((environmentSize < 0.5F)
					? fireSmallReverbSound : fireLargeReverbSound, origin, param);

				param.volume = 2.0F;
				param.referenceDistance = 4.0F;
				audioDevice.Play(fireFarSound, origin, param);
				param.referenceDistance = 1.0F;
				audioDevice.Play(fireStereoSound, origin, param);
			}
		}

		void ReloadingWeapon() {
			if (!muted) {
				Vector3 origin = soundOrigin;
				AudioParam param;
				param.volume = 0.2F;
				audioDevice.Play(reloadSound, origin, param);
			}
		}

		void ReloadedWeapon() {
			if (!muted) {
				Vector3 origin = soundOrigin;
				AudioParam param;
				param.volume = 0.2F;
				audioDevice.Play(cockSound, origin, param);
			}
		}

		// IWeaponSkin3
		Vector3 MuzzlePosition { get { return originMatrix * (Vector3(0.4F, -0.9F, 0.0F) - Vector3(0.0F, 0.55F, 0.125F)); } }
		Vector3 CaseEjectPosition { get { return originMatrix * (Vector3(0.4F, -0.9F, 0.0F) - Vector3(0.0F, -0.25F, 0.125F)); } }

		// IVRWeaponSkin
		bool VRHand { set { vrHand = value; } }
		// The sights' line (View.as aims down it): through the rear sight's notch
		// and the front sight's top, in the world.
		Vector3 VRSightRear { get { return VRGunMatrix() * Vector3(0.025F, -9.0F, -4.411F); } }
		Vector3 VRSightFront { get { return VRGunMatrix() * Vector3(0.025F, 8.5F, -4.411F); } }
		// The reflex sight's window: its middle, and its radius (0: no reflex
		// sight). SightReflex.kv6's opening is 10 voxels across and 9 high, its
		// middle half a voxel to the side of its pivot and 1.5 behind (read off
		// the model: a centre half a voxel low and a radius of 4.5 lost the dot
		// at the window's edges, testers).
		Vector3 VRReflexCenter { get {
			return VRGunMatrix() * (Vector3(0.04F, -7.825F, -4.85F) + Vector3(-0.5F, -1.5F, 0.0F) * 0.08F);
		} }
		float VRReflexRadius { get {
			if (!cg_reflexScope.BoolValue)
				return 0.0F;
			Matrix4 m = VRGunMatrix();
			return (m * Vector3(5.0F * 0.08F, 0.0F, 0.0F) - m * Vector3(0.0F, 0.0F, 0.0F)).Length;
		} }

		// View.as's frame (its gun model's), where this skin draws its own
		private Matrix4 VRGunMatrix() {
			Matrix4 mat = CreateScaleMatrix(0.05F + 0.001F);
			mat = mat * CreateScaleMatrix(-1, -1, 1);
			mat = CreateTranslateMatrix(Vector3(0.4F, -0.9F, 0.0F)) * mat;
			return originMatrix * mat;
		}

		private void AddVRHandToScene() {
			Matrix4 gun = VRGunMatrix();
			ModelRenderParam param;
			param.customColor = teamColor;
			param.matrix = gun;
			renderer.AddModel(vrGunModel, param);
			if (!(false)) {
				param.matrix = gun;
				renderer.AddModel(vrPartModel, param);
			}
			if (cg_reflexScope.BoolValue) {
				param.matrix = gun * CreateTranslateMatrix(Vector3(0.04F, -7.825F, -4.85F)) * CreateScaleMatrix(0.08F);
				renderer.AddModel(vrReflexSightModel, param);
			} else {
				param.matrix = gun * CreateTranslateMatrix(Vector3(0.025F, -9.0F, -4.4F)) * CreateScaleMatrix(0.05F);
				renderer.AddModel(vrRearSightModel, param);
			}
			param.matrix = gun * CreateTranslateMatrix(Vector3(0.025F, 8.5F, -4.4F)) * CreateScaleMatrix(0.05F);
			renderer.AddModel(vrSightFrontModel, param);
		}

		void AddToScene() {
			if (vrHand) {
				AddVRHandToScene();
				return;
			}

			Matrix4 mat = CreateScaleMatrix(0.05F + 0.001F); // make model a little bigger to stop z-fighting
			mat = mat * CreateScaleMatrix(-1, -1, 1);

			Vector3 trans = Vector3(0.4F, -0.9F, 0.0F);
			mat = CreateTranslateMatrix(trans) * mat;

			ModelRenderParam param;
			param.matrix = originMatrix * mat;
			param.customColor = teamColor;
			renderer.AddModel(model, param);
		}
	}

	IWeaponSkin@ CreateThirdPersonShotgunSkin(Renderer@ r, AudioDevice@ dev) {
		return ThirdPersonShotgunSkin(r, dev);
	}
}