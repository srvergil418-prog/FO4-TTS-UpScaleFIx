# True Through Scope — 超分（Upscaling）兼容修复

修复现象：**开启 DLSS/FSR/XeSS 超分后，Scope（瞄具）画中画只显示左上角四分之一，其余全黑。**

---

## 根因

TTS 里渲染目标尺寸有**两个不一致的来源**：

| 来源 | 超分关闭时 | 超分开启时 |
|---|---|---|
| `BSGraphics::State::backBufferWidth/Height` | 3840x2160 | **3840x2160**（输出尺寸，不变） |
| `renderTargets[4]`（Main Render_PreUI 目标） | 3840x2160 | **1920x1080**（内部分辨率） |

超分开启后，游戏实际只渲染到 **RT4 的内部尺寸（约输出的一半）**，但 TTS 多处仍在用**输出尺寸**。渲染目标与 viewport 尺寸差 2 倍，于是内容只覆盖左上四分之一。

关键矛盾点（`SecondPassRenderer::DrawScopeContent`）：

- **渲染目标**：超分时绑定 `renderTargets[0]`（输出尺寸 3840x2160）
- **viewport**：超分时却用 `GetFirstPassViewport()`（RT4 尺寸 1920x1080）
- → **在 3840x2160 的目标上，只开了 1920x1080 的 viewport**

作者其实已经实现了正确的动态分辨率机制（`SetFirstPassViewport` / `GetFirstPassViewport`，从 RT4 取真实尺寸），**只是覆盖不全**：初始化路径漏了。

### 完整因果链（定位依据）

经过逐层追踪，从"尺寸取错"到"镜片左上四分之一"的链路是这样的：

```
RenderUtilities::CreateTemporaryTextures()
  取 BSGraphics::State::backBufferWidth
  → 超分开启时这是「输出尺寸」3840x2160，而不是「内部渲染尺寸」1920x1080
        │
        ▼
s_FirstPassColorTexture / s_SecondPassColorTexture 都被建成 3840x2160   ❌
        │
        │  D3DHooks.cpp:942   GetSecondPassColorTexture()->GetDesc(&srcTexDesc)
        ▼
D3DResourceManager::EnsureStagingTexture(device, &srcTexDesc)
  → m_stagingTexture 也是 3840x2160
        │
        │  D3DHooks.cpp:964   SafeCopyTexture(stagingTexture, SecondPassColorTexture)
        ▼
m_scopeTextureView (stagingSRV) → 绑定到镜片模型 TTSEffectShape
        │
        ▼
镜片上这张 3840x2160 的纹理里，只有左上 1920x1080 区域有真实内容
  （因为 second pass 实际只渲染了 RT4 的尺寸）
        │
        ▼
现象：均等四份，只有左上正常 ← 与实测描述完全一致
```

**所以修复的关键是切断链条源头**——让 `CreateTemporaryTextures()` 从一开始就用 RT4 的真实尺寸（改动 1）。改了源头之后，`SecondPassColorTexture` → `stagingTexture` → 镜片纹理全部自动正确。

> 附注：`DrawScopeContent()` 里那个 viewport（改动 3）其实**会被 `D3DHooks::SetScopeTexture()` 内部覆盖**——后者会自己用 `s_ScopeQuadOriginalViewport` 重设 viewport 并重新绑定 RT4。所以改动 3 属于"方向对但实际不生效"，保留它只是为了让两处逻辑一致，**无害**。

---

## 改动清单（共 5 处）

### 1. `src/RenderUtilities.cpp` — `CreateTemporaryTextures()`

临时纹理尺寸从「输出尺寸」改为三级回退：

```cpp
// 改前
unsigned int width  = rendererState.backBufferWidth;    // 超分下 = 3840 ❌
unsigned int height = rendererState.backBufferHeight;

// 改后：①已捕获的 RT4 viewport → ②RT4 纹理 desc → ③才回退到输出尺寸
```

### 2. `src/rendering/SecondPassRenderer.cpp` — `BackupFirstPassTextures()`

`m_rtTexture2D` 的来源优先级重排。原来是**优先取交换链**（输出尺寸），RT4 只当回退：

```cpp
// 改前：RT0 (SwapChain, 3840) 优先 → RT4 (1920) 只是 fallback
// 改后：RT4 (实际渲染目标) 优先 → RT0 才是最后的 fallback
```

### 3. `src/rendering/SecondPassRenderer.cpp` — `DrawScopeContent()` 的 viewport（**核心**）

```cpp
// 改前：两个分支用不同的尺寸来源
if (IsUpscalingActive()) {
    GetFirstPassViewport(viewport);          // RT4 = 1920x1080 ❌ 与目标不匹配
} else {
    viewport.Width = targetWidth;            // 从实际 RTV 读 ✓
}

// 改后：统一用 targetWidth/targetHeight
//      它们是从上面实际绑定的那个 RTV 读出来的，永远匹配
viewport.Width  = (float)targetWidth;
viewport.Height = (float)targetHeight;
```

### 4. `CMakeLists.txt` — 部署步骤

原来的 POST_BUILD 无条件拷贝到作者本机路径（`G:/...`、`I:/...`），在别的机器和 CI 上必然失败：

```cmake
# 改后：用 COPY_BUILD 开关包裹，并去掉硬编码路径
if (COPY_BUILD AND DEFINED Fallout4Path)
    add_custom_command(...)
endif ()
```

---

### 5. `vcpkg.json` — 补齐缺失依赖

`CMakeLists.txt` 里 `find_package` 要求 `nlohmann_json` 和 `minhook`，但原 `vcpkg.json` 没有声明它们（作者本机全局装过，所以自己编得动，别人不行）：

```json
// 原 dependencies: boost-stl-interfaces, detours, fmt, rsm-mmio, spdlog, imgui
// 补后额外加入: nlohmann-json, minhook
//             + CommonLibF4 自己的依赖（args, catch2, frozen, nowide,
//               robin-hood-hashing, srell, xbyak）
```

> vcpkg 在 manifest 模式下**只读项目根的 `vcpkg.json`**，子目录（CommonLibF4）那份会被忽略，所以两边的依赖必须合并到这一份里。

---

## 怎么编译出 DLL

**你本机没有 MSVC / vcpkg / CMake**，所以用 GitHub Actions 云端编译（免费）：

### 步骤

1. **注册 / 登录 GitHub**（github.com）
2. **新建一个仓库**（Public 或 Private 都行，例如 `tts-fix`）
3. **把本文件夹里的所有内容上传上去**（网页上可以直接拖拽上传整个文件夹，或者用 GitHub Desktop）
   - ⚠️ 注意 `.github/workflows/build.yml` 是**隐藏目录**，网页拖拽可能漏掉它 —— 上传后确认仓库里能看到 `.github/workflows/build.yml`
4. 打开仓库的 **Actions** 标签页
5. 左侧点 **Build TrueThroughScope** → 右侧 **Run workflow** → 绿色按钮
6. 等 5~15 分钟（首次要编译 vcpkg 依赖，比较慢；之后有缓存会快很多）
7. 进那次运行的页面，底部 **Artifacts** 里下载 **TrueThroughScope-fixed.zip**
8. 解压得到 `TrueThroughScope.dll`

### 安装

把编译出的 `TrueThroughScope.dll` 覆盖到：

```
E:\Custom Fallout4\mods\TrueThroughScope\F4SE\Plugins\TrueThroughScope.dll
```

（原文件已经备份在 `D:\dsh\bld\backup\TrueThroughScope\`，随时可以还原）

---

## 验证要点

改完后，**开不开超分都应该正常**：

- 开超分：Scope 画中画应当**完整覆盖整个镜片**，不再是左上四分之一
- 关超分：行为应当与原来一致（因为两个尺寸本来就相等）

日志里可以看这两条确认：

```
Creating temporary textures with size 1920x1080      ← 应该是内部尺寸，不是 3840x2160
Resized FirstPass Color Textures to 1920x1080        ← 若初始化时尺寸就对了，可能不出现
```

---

## 备注

- 本修复**不涉及 FG（帧生成）**。FG 那条链是另一个独立问题（TTS 找不到 `AAAFrameGeneration.dll`），与本次现象无关。
- 改动只针对尺寸来源，**没有改变任何渲染算法**，所以视觉效果不应有其它变化。
- 若编译报错，最可能的是 `CommonLibF4` 版本与 TTS 源码不匹配 —— workflow 里拉的是 CommonLibF4 的**最新 master**，可以改成某个具体 tag/commit 来对齐。
