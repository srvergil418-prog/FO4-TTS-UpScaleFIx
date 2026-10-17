#pragma once

#include "Constants.h"
#include <RE/NetImmerse/NiCamera.hpp>
#include <mutex>   // ScopeNodeMutex: guards s_CurrentScopeNode across threads

#include "DataPersistence.h"

namespace ThroughScope
{
    class ScopeCamera
    {
	private:
        // Static member declarations (must be before inline methods that use them)
        static RE::NiCamera* s_ScopeCamera;
        static RE::NiCamera* s_OriginalCamera;
        static float s_TargetFOV;
		static float s_MinMagnification;
		static float s_MaxMagnification;
        static bool s_OriginalFirstPerson;
        static bool s_OriginalRenderDecals;
        static bool s_IsRenderingForScope;
		static bool s_IsUserAdjustingZoomData;

    public:
        // Initialize scope camera system
        static bool Initialize();
        static void Shutdown();
        
        // Create and setup a scope camera instance
        static void CreateScopeCamera();
        
        // Camera getters/setters
        static RE::NiCamera* GetScopeCamera() { return s_ScopeCamera; }
        static void SetScopeCamera(RE::NiCamera* camera) { s_ScopeCamera = camera; }
        
		static void ApplyScopeTransform(RE::NiNode* scopeNode, const DataPersistence::CameraAdjustments& adjustments);
		static void ApplyScopeSettings(const DataPersistence::ScopeConfig* config);
		static void SetupScopeForWeapon(const DataPersistence::WeaponInfo& weaponInfo);
		static int GetScopeNodeIndexCount();
		static void CleanupScopeResources();

		// Thread-safe access to s_CurrentScopeNode.
		//
		// Why this exists: EquipWatcher runs its delayed setup on its OWN thread
		// (EventHandler.cpp DelayedSetupScopeForWeapon) and calls
		// CleanupScopeResources() from there, which does DecRefCount() and then
		// nulls the static. The render path reads that same static every frame.
		// Nothing synchronises the two -- the mutex inside EquipWatcher only
		// guards EquipWatcher's own state -- so the render thread can read a
		// non-null pointer and then use an object the setup thread has already
		// destroyed (DecRefCount hitting zero deletes it). That is a
		// use-after-free, and it surfaces as a crash inside library
		// string/memory routines, because the node being walked is gone.
		//
		// AcquireScopeNode takes a reference under the lock, so the node cannot be
		// torn down while the render thread is using it. Pair every acquire with
		// ReleaseScopeNode.
		static RE::NiNode* AcquireScopeNode();
		static void ReleaseScopeNode(RE::NiNode* a_node);
		static std::mutex& ScopeNodeMutex();

		// Big lock covering "rebuild the scope node" against "walk the scope node".
		//
		// Holding a reference (AcquireScopeNode) keeps the node object alive, but it
		// does NOT stop another thread from mutating its children while the render
		// thread is iterating them. Rebuilding does exactly that: LoadNIF, then
		// name/transform, then weaponNode->AttachChild(), then Update(). A walk that
		// crosses that mutation reads freed child pointers -- the crash lands inside
		// library code, and the attached model disappears because the tree it hung
		// off was rebuilt underneath it.
		//
		// Rebuilds happen on weapon swaps (EquipWatcher's own thread and the main
		// thread's HK PCUpdate path), while walks happen every frame on the render
		// thread. They must not overlap. Recursive because a rebuild path may call
		// helpers that also take it.
		static std::recursive_mutex& ScopeRebuildMutex();

		// Atomically replace s_CurrentScopeNode: drop the reference held by the old
		// node and take over the caller's reference to the new one.
		//
		// The UI panels (CameraAdjustmentPanel / ModelSwitcherPanel) used to assign
		// to the static directly when the user switched scope models. That both
		// leaked the previous node (nothing released it) and raced the render
		// thread, in exactly the way that made weapon swaps crash. Route those
		// assignments through here instead.
		static void PublishScopeNode(RE::NiNode* a_node);
		static void RestoreZoomDataForCurrentWeapon();  // 恢复ZoomData的方法
        // Get target FOV
        static float GetTargetFOV() { return s_TargetFOV; }
		static void SetTargetFOV(float fov)
		{
			// 使用倍率限制来约束FOV
			float baseFOV = GetBaseFOV();
			float minFOV = baseFOV / s_MaxMagnification;  // 最大倍率 = 最小FOV
			float maxFOV = baseFOV / s_MinMagnification;  // 最小倍率 = 最大FOV
			s_TargetFOV = std::clamp(fov, minFOV, maxFOV);
		}

		// 倍率系统
		static float GetBaseFOV();  // 获取玩家的场景 FOV (worldFOV)
		static void SetMagnificationRange(float minMag, float maxMag)
		{
			s_MinMagnification = std::max(1.0f, minMag);  // 最小倍率不能低于1x
			s_MaxMagnification = std::max(s_MinMagnification, maxMag);
			// 实时更新当前FOV以符合新范围
			SetTargetFOV(s_TargetFOV);
		}
		static float CalculateFOVFromMagnification(float magnification)
		{
			return GetBaseFOV() / std::max(1.0f, magnification);
		}
		static float GetCurrentMagnification()
		{
			float baseFOV = GetBaseFOV();
			if (s_TargetFOV > 0.1f) {
				return baseFOV / s_TargetFOV;
			}
			return 1.0f;
		}
		static void SetTargetMagnification(float mag)
		{
			float clampedMag = std::clamp(mag, s_MinMagnification, s_MaxMagnification);
			s_TargetFOV = CalculateFOVFromMagnification(clampedMag);
		}
		static float GetMinMagnification() { return s_MinMagnification; }
		static float GetMaxMagnification() { return s_MaxMagnification; }
        
        // Flag indicating if we're in scope render mode
        static bool IsRenderingForScope() { return s_IsRenderingForScope; }
        static void SetRenderingForScope(bool value) { s_IsRenderingForScope = value; }

		static bool IsSideAim();
		static RE::BGSKeyword* IsMagnifier();

		static RE::TESFormID s_EquippedWeaponFormID;
		static RE::NiNode* s_CurrentScopeNode;

		static bool hasFirstSpawnNode;
		static bool isDelayStarted;
		static bool isFirstScopeRender;

		static int s_enableNightVision;

		// ZoomData管理
		static void SetZoomDataUserAdjusting(bool adjusting) { s_IsUserAdjustingZoomData = adjusting; }
		static bool IsUserAdjustingZoomData() { return s_IsUserAdjustingZoomData; }

    private:
		static RE::BGSKeyword* an_45;
		static RE::BGSKeyword* AnimsXM2010_scopeKH45;
		static RE::BGSKeyword* AnimsXM2010_scopeKM;
		static RE::BGSKeyword* AX50_toounScope_K;
		static RE::BGSKeyword* AnimsAX50_scopeKH45;
		static RE::BGSKeyword* QMW_AnimsQBZ191M_on;
		static RE::BGSKeyword* QMW_AnimsQBZ191M_off;
		static RE::BGSKeyword* QMW_AnimsRU556M_on;
		static RE::BGSKeyword* QMW_AnimsRU556M_off;
		static RE::BGSKeyword* AX50_toounScope_L;
		static RE::BGSKeyword* AnimsAX50_scopeK;

        static void ResetCamera();
    };
}
