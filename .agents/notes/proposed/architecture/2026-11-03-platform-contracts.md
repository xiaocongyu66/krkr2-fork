# B2 窗口之后的输入、主循环与生命周期契约

Status: proposed

## 问题与当前边界

窗口首批已提取 `visual/WindowLayer.h`，Cocos 窗口实现仍在 environ；
视频通过专用适配器借用 Cocos 节点。当前 dirty 实现和已验证范围见
[窗口接口](../../../../docs/subsystems/window-interface.md)。
这只完成 B2 的窗口接口子批次，输入、主循环及生命周期仍与现有宿主相关。
SDL3 已选作后续平台壳，尚未完成迁移，见[选型](2026-11-03-ui-platform-shell.md)。

## 建议与下一步

已完成[初始化测试链接闭包](../../implemented/testing/2026-11-03-ffmpeg-test-link-closure.md)修复，
继续保留[访问冲突](../bug-fix/2026-11-03-ui-contract-access-violation.md)的诊断能力。
随后从 `TVPWindow.*`、`WindowImpl.*`、`EventIntf.*` 和 `MainScene.*`
列出事件生产者、排队、脚本分发及销毁回调的调用链，确定下一项最小可独立验证的接口。
输入批次先定义坐标空间、DPI、按下/释放及焦点丢失顺序；
再处理主循环的绘制/模态/计时关系，最后处理暂停恢复与窗口销毁。

## 完成条件

每个接口在现有 Cocos 适配器上保持脚本可观察行为，有对应最小场景和验证证据，
通用头不引入新的 Cocos 类型。记录 Windows 实测及其他平台缺口。
基线建立后再按[后端路线](2026-11-03-backend-modernization.md)推进 SDL3 原型。

## 取舍与风险

直接在一次改动中换窗口、输入和渲染会使回归原因难以区分，采用分接口批次。
本提案尚未确定新的函数签名；先依据调用链和测试定义契约，避免为未来后端堆积空接口。

## 输入状态子批次调查

现有 `SystemImpl.cpp` 为读取异步按键而直接包含 Cocos 窗口实现头。
键盘、鼠标和手柄适配器共同维护 `TVPWindowLayer::_scancode[0x200]`：
低位表示当前按下，0x10 表示待查询的按下；释放保留未消费的按下位。
`getcurrent=true` 的查询也清除 0x10，这是现有脚本可见行为，不能顺手改成常见的独立边沿查询。
鼠标短点另有直接写入“已释放但待查询”的状态路径，重复 key-down 会重新置位按下标记。

已接入纯标准 C++ 的 `core_input` 状态模块，System 查询不再包含 `TVPWindow.h`。
六个状态契约测试通过，依赖检查扩至 11 个基础目标。
迁移前 `input-state-baseline-01` 完成一轮交互并正常退出，
opt-in 驱动在 Cocos EventDispatcher 投递分开的 A-key down/up，
真实 Window 回调分别记录 `input:key-down:65:0:1:0`、`input:key-up:65:0:0:0`。
基线应用 SHA256 为 `03253e42a8d0e7a00d0dee35cffee040462f07766ad3a571dc2a259a7a3b2995`。
接入后 `input-frame-migrated-01` 以相同脚本完成三轮交互与正常退出，
A-key 的两条记录与基线完全一致；应用 SHA256 为
`a0a7a510a751fa15ce93ce618ef6ca75ef39c70ebfc403054469c87b237e6824`。
CDB 附加轮退出 0，无访问冲突标记或 dump。full 配置实际注册、运行输入/帧钩子共 9 项，全部通过。
键码翻译、坐标/DPI 变换、TJS 输入事件类和排队/合并策略仍由现有路径承担。

主循环调查另发现 `RenderUtils.h` 的 post-update 回调存储带内部链接，
设置者在 OpenGL 渲染器、读取者在 MainScene 与模态绘制路径。
提取原定义的双编译单元实验实际得到调用 0 次（期望 1 次），
证据在 `out/frame-hook-validation/baseline.log`、`source.json`。
已将唯一存储移至 `event/PostUpdateEvent.cpp`，三个跨单元/替换/注销测试通过；
`RenderUtils.h` 不再包含 Cocos 或平台头，绘制与 swap 实现在 environ 的 cpp 中。
普通更新和模态绘制均经同一调用入口，保持原有相对顺序与计时公式。
上述三轮也覆盖此改动，实际检查文件/菜单截图；这不是原访问冲突的根因结论。

## 生命周期子批次

`environ/HostLifecycle.h` 提供不含 Cocos/TJS 类型的前台/后台通知。
Cocos AppDelegate 在通知引擎后再启停动画，保持原顺序；Application 中音量、混音锁、
compact 和 System 事件排队未改变，没有提前接线旧 `OnExit` 或更改销毁顺序。

`lifecycle-migrated-01` 启用 `--keyboard-probe --lifecycle-probe`，真实 System 回调依次记录
`lifecycle:deactivate`、`lifecycle:activate`，随后一轮完整交互及关闭退出 0，未强停。
应用 SHA256 为 `49bb28ee8aa874d105934ea72f8206f228bfbf7a62a76a4a2298c2864be4f003`。
该测试只发引擎通知，未停止 Cocos 泵，不证明 OS 挂起、移动端 surface 重建或恢复帧行为。
事件生产/分发之间仍按原实现合并同类激活事件，并检查当前激活状态，不能要求每次快速切换都产生回调。

## 剩余工作

指针坐标子批次已抽出 Y 轴翻转/向零截断和 Cocos Node 适配，修复光标缓存混入父节点坐标；
范围及验证见[指针契约](../../implemented/testing/2026-11-04-pointer-coordinates.md)。
独立 SDL3 原型已接入鼠标位置与视口换算、滚轮和共享按键状态，
Linux XTest 验证窗口外拖动、调整大小和失焦恢复，见
[鼠标子批次](../../implemented/testing/2026-11-04-sdl3-pointer-input.md)。
这尚未定义生产 Cocos 的 OS DPI/屏幕坐标到世界坐标转换，也不替代高 DPI 与物理设备验证。

修饰键快照已提到共享输入模块，导出实现移到 System 层，保持旧中键省略及标记不消费规则；
SDL 队列与 X11 的组合键释放/恢复验证见[子批次记录](../../implemented/testing/2026-11-03-shared-input-modifiers.md)。
这不改变 Cocos 激活与失焦路径，也不表示完成 IME 或物理设备验证。

独立 SDL3 原型已使用共享状态的 `releaseAll()`，验证先释放后通知失焦、重复通知、
失焦输入忽略及恢复后的新按键，Linux Xvfb/X11 七帧像素与反向场景通过，见
[失焦子批次](../../implemented/testing/2026-11-03-sdl3-focus-validation.md)。
另有 X11 切焦/XTest 键盘探针，验证了原型的系统事件路径并修复 OS 关闭绕过脚本否决的问题。
上述 SDL 证据不能替代上一节要求的现有后端真实失焦交互。

本页仍为 proposed：OS 坐标/DPI/IME、真实切焦与后台验收、平台事件泵、完整暂停恢复和销毁路径尚未完成。
桌面 Cocos 3.17.2 的 `cocos/platform/desktop/CCGLViewImpl-desktop.cpp` 将焦点通知
分发为 `EVENT_WINDOW_FOCUSED/UNFOCUSED`；最小化/恢复才调用 AppDelegate 的后台/前台入口。
MainScene 现已订阅这两个焦点事件，使用共享释放及取消分类，抑制失焦期间的游戏输入；
实现及验证边界见[桌面焦点子批次](../../implemented/testing/2026-11-04-cocos-input-focus.md)。
普通焦点不调用 HostLifecycle，最小化/恢复保持原入口；不能把订阅测试当作真实 OS 切焦验收。
Windows 现已另通过 SendInput 点击原生辅助窗口、确认前台 HWND，验证 A 键与 GLView 拖动的
失焦取消、迟到事件抑制与恢复，见[焦点补验](../../implemented/testing/2026-11-04-cocos-input-focus.md#windows-原生焦点探针补验)。
该证据不覆盖原生鼠标捕获、物理触摸屏、最小化、其他平台或多游戏窗口与切焦组合。
后续仍需分别验证普通切焦、最小化和触摸取消，特别是多游戏窗口、移动端与输入法组合场景。
按钮归属缺口已通过双编译单元实验复现并修正：删除头文件静态副本，改为窗口/虚拟手势实例状态，
物理鼠标显式传按钮，切换接收窗口时先取消旧手势，见[触摸按钮记录](../../implemented/testing/2026-11-04-touch-button-ownership.md)。
纯状态与构建结果不等于真实双指、多窗口切换或设备组合交互；后续仍需原生轨迹验收。
后续应补齐最小化/后台与恢复、其他平台失焦场景的证据，避免仅靠接口声明推断跨平台行为。
Windows 单宿主窗口已补原生最小化/恢复：确认 IsIconic、一秒待处理脚本请求、
取消后全释放、迟到抬起隔离、新输入及填充层像素，见
[最小化记录](../../implemented/testing/2026-11-04-windows-minimize-recovery.md)。
英文布局与固定填充层的结果不覆盖 IME、音视频、设备重建或移动端完整暂停恢复。
Windows 新增 GLView/TJS 指针探针，实际复现并修正隐藏窗口丢弃取消抬起，
记录见[触摸按钮补验](../../implemented/testing/2026-11-04-touch-button-ownership.md#windows-真实组件回调补验2026-11-04)。
该早期轨迹验证单窗口隐藏/显示；截图另见黑底差异，需调查绘制/采集时点，不能作为画面验收通过。
双窗口切换/失效的组件探针与可选 Windows CI 入口见
[双窗口记录](../../implemented/testing/2026-11-04-multiwindow-pointer-probe.md)；已有真实回调通过，
另修复正常切窗/隐藏回退未恢复节点原点，双窗口坐标与固定填充层颜色复验通过；
这些组件输入证据仍不能替代 OS 切焦、DPI/IME 或物理设备验收。
独立 SDL3/TJS 壳实验的可用范围另见 `tools/sdl3-prototype/README.md`，不作为 B2 全完成或 KAG 后端迁移验收。

提交文本解码另已移入无 Cocos 依赖的 core_input，保留严格 UTF-8、UTF-16 单元顺序与整段拒绝规则；
独立平台范围增至 31 项，full 另通过 2 项旧 Cocos 转换对照，见
[文本子批次](../../implemented/testing/2026-11-04-committed-text-input.md)。
Windows 默认模式 setter、Layer 优先级、dontCare 和切窗恢复另已完成原生文本验收，见
[模式修复](../../implemented/bug-fix/2026-11-04-window-default-ime-mode.md)。
Android 编码桥的标准 UTF-8 转换、打印键去重与退格接线也已落地，独立 API 34 模拟器
14 项 Framework/ART/JNI 契约通过，共享平台范围随后增至 34 项，见
[验证记录](../../implemented/testing/2026-11-05-android-input-contracts.md)。
提交文本或独立桥测试不等于 IME 组合验收；完整 Android 游戏 APK、TJS 回调、物理设备及生命周期仍保留独立调查。

## B2-2：krkr2_sdl3 目标与壳接线（2026-11-20）

`KRKR2_BUILD_SDL3_APP=ON -DKRKR2_BUILD_APP=OFF` 现在生成 `krkr2_sdl3` 可执行目标，
链接 `krkr2core` 的非 cocos 组合（`core_environ_module` + domain 环 + `core_environ_sdl3`）
与 SDL3。新源：`environ/sdl3/{SdlEngineRunner.h,.cpp,main.cpp}`；`SdlWindowLayer`/
`SdlAppDelegate` 接线补齐（`TVPCreateAndAddWindow` 转发、`postVirtualKey` 改走
`InternalKeyDown`/`OnKeyUp`、UseMouseKey 移植、`cancelInput` 用 `planInputCancellation`、
IME 三方法镜像 MainScene、DPI provider、`SetImeModeListener` 桥）。模块图改动：
`core_environ_cocos` 与 `cpp/core` 的 `find_package(cocos2dx)` 改由 `KRKR2_BUILD_APP` 门控，
`krkr2core` 存档组在非 cocos 配置下把 `core_environ_cocos` 换成 `core_environ_sdl3`。

验证（本机无 vcpkg/SDL3，全部为隔离证据）：
`g++ -fsyntax-only -std=c++17` 对 sdl3 五个源全部通过（`/tmp/sdl3stub` + `/tmp/fmtstub`）；
`python scripts/check_core_module_deps.py` 与 `/tmp/check_environ_split.py` 通过；
用 `/tmp/b22/stubs` 的全套包存根 `cmake -DKRKR2_BUILD_APP=OFF -DKRKR2_BUILD_SDL3_APP=ON
-DENABLE_TESTS=OFF -DBUILD_TOOLS=OFF` 配置成功，`krkr2_sdl3.dir/link.txt` 链接
`core_environ_module`+各 domain+`core_environ_sdl3`，无 `core_environ_cocos`；
默认 full 配置仍只生成 `krkr2`（链 `core_environ_cocos`，无 `core_environ_sdl3`）；
两标志同时开只单独编译 `core_environ_sdl3` 库，不出可执行（避免 `TVPCreateAndAddWindow`
双定义）。裸配置失败点在 `find_package(FFMPEG)`（`cpp/external/libbpg`），属依赖缺失而非
CMake 逻辑错误。

阻断点（链接级，未硬改 B2-0 拆分）：`LocaleConfigManager`/`IndividualConfigManager`/
`PreferenceDefaults` 的实现源包含在 `core_environ_cocos`（其源码含 `CCFileUtils`、
`ui/UIText` 等 Cocos 头，符合 B2-0 拆分契约），但 `Application.cpp`、
`base/{ScriptMgnIntf,SystemIntf}.cpp`、`sysinit/impl/SysInitImpl.cpp` 与 `visual/*` 多处
在链接时引用 `LocaleConfigManager::GetInstance()`/`IndividualConfigManager`。
因此 `krkr2_sdl3` 目前配置通过但完整链接会缺这些符号；`TVPYUVSprite`（仅
`core_movie_module` 的 `KRMoviePlayer` 引用，而 movie 模块成员在非 cocos 图中无引用方）
不构成阻断。下一步建议：把 ConfigManager 拆成无 Cocos 依赖的核心实现（B2-3），
或在 `core_environ_sdl3` 内提供等价实现；随后做真实 vcpkg/SDL3 链接与 X11 冒烟。

## B2-3：ConfigManager 解耦（2026-11-20）

三个 ConfigManager 源已移入 `core_environ_module` 且不再包含任何 Cocos 头：
`CCFileUtils::isFileExist`/`fullPathForFilename`/`getStringFromFile` 换成环境层既有的
`IFileBackend` 链（`TVPGetFileBackend()->Exists/OpenRead`：Android 经 `CocosFileBackend`
回到 `FileUtils` 读 APK 内打包资源，桌面经 `LocalFileBackend` 走真实文件系统）；
`LocaleConfigManager` 的候选路径前缀为 `""`、`"res/"`（保留 AppDelegate:98 的历史搜索
路径）、`"Resources/"`（两套壳的资产同步目录），全部 miss 时回退 `en_us`，连
`en_us` 也缺时返回空——`GetText` 此时退回把 tid 原样显示，不再像旧代码那样无限递归。
`ui::Text*`/`ui::Button*` 参数不能留在模块，`initText` 的四个重载整体改由
`core_environ_cocos` 侧的自由函数 `TVPInitLocaleText(...)`（新文件
`environ/ui/LocaleTextBinding.{h,cpp}`）承担，行为逐字迁移；调用点仅限
`environ/ui/` 的 5 个 Form（FileSelector、MainFileSelector、Preference、SelectList、
XP3Repack）。`PreferenceDefaults.cpp` 的 `#include <cocos2d.h>` 删除，GL 渲染器字符串
改为宿主钩子 `TVPGetGLRendererString()`，coco 宿主实现仍在 `AppDelegate.cpp`
（经 `<cocos2d.h>` 拿到 `glGetString`，与旧 TU 相同的编译期保证）。
`SdlEngineRunner::init()` 在 `TVPInitNativeDialogBackend()` 之后补调
`TVPInitEnvironFileBackends()`，顺序对齐 AppDelegate:48-49。

验证（本机无 vcpkg/SDL3，全部为隔离证据）：
`g++ -fsyntax-only` 对三个 ConfigManager cpp 用 module 的 `-I` 集
（`/tmp/b23-sdl3` 生成的 `core_environ_module` flags.make，外加 `/tmp/b23` 的 tinyxml2
和 `/tmp/fmtstub` 的 fmt 语法存根）全部通过，即验收标准"不再需要 cocos 头"；
三个源实际 `-c` 编译出 `.o` 后 `nm` 确认 `LocaleConfigManager::GetInstance/Initialize/
GetText`、`IndividualConfigManager::*` 与 `TVPInitializeRuntimePreferences` 均定义在
module 归档成员里，其未定义引用全部落在 module 图内（tjs2、core_io、GlobalConfigManager）
或仅经 `TVPInitializeRuntimePreferences` 间接触达的宿主钩子；用 `/tmp/b22/stubs` 的
包存根重新配置 `-DKRKR2_BUILD_APP=OFF -DKRKR2_BUILD_SDL3_APP=ON`，module 目标含 4 个
ConfigManager 源、无任何 ui/cocos2d 源，`krkr2_sdl3` 链接行不含 `core_environ_cocos`；
默认 full 配置（同一套存根）下 `core_environ_module` 增加 3 源、`core_environ_cocos`
减少 3 源并新增 `ui/LocaleTextBinding.cpp`，其余源不变；
`python scripts/check_core_module_deps.py` 通过；改动 C++ 文件经 clang-format-18
确认（AppDelegate.cpp 残留的 `#if (CC_TARGET_PLATFORM...)` 续行差异为 18 与 20
版本差异且原有，未动）。

残留：模块图外仍引用仅存于 `core_environ_cocos` 的宿主钩子
（`TVPGetInternalPreferencePath`——`GlobalConfigManager::GetFilePath` 依赖，
`TVPGetPlatformName`——`Application.cpp`/`base/SystemIntf.cpp` 依赖），SDL3 壳尚未
提供等价实现；`TVPInitializeRuntimePreferences` 在 SDL3 壳尚无调用点（含
`TVPGetGLRendererString` 的 SDL3 实现）。这些属于 B2-4 宿主钩子批次，
本次未改。

## B4：movie 与 visual 的去 cocos 化（2026-11-20）

`-DKRKR2_BUILD_APP=OFF -DKRKR2_BUILD_SDL3_APP=ON` 配置现在可以无 cocos2dx
穿过 movie/visual 两个模块（此前 `movie/CMakeLists.txt:68` 与
`visual/CMakeLists.txt:92` 的 `find_package(cocos2dx CONFIG REQUIRED)` 是 CI
sdl3-shell job 报告的下一处缺口）。文件归属与门控决策：

| 文件 | 归属 | 理由 |
| --- | --- | --- |
| movie/ffmpeg/KRMoviePlayer.cpp | **core_movie_cocos**（KRKR2_BUILD_APP 门控） | 含 Cocos Node/Sprite 场景图呈现（YUVSprite、CocosWindowAccess），仅 cocos 宿主可用 |
| movie/ffmpeg/krffmpeg.cpp | **core_movie_cocos** | 实例化 MoviePlayerOverlay/MoviePlayerLayer，`KRMoviePlayer.h` 仅前向声明 cocos 类型，但构造两侧对象的接缝源必须与实现同侧 |
| movie/ffmpeg/KRMovieLayer.{h,cpp} | 留 core_movie_module | 自身不含 cocos 头；layer 模式经 tTVPBaseTexture 工作，SDL3 图仅因无调用方而不拉取其归档成员 |
| visual/RenderManager.cpp | 留 core_visual_module，门控去 cocos | 软件渲染器之外还承载渲染管理器注册表、`TVPGetRenderManager`、`TVPGetSoftwareRenderManager`（`tTVPBaseBitmap` 每层位图都依赖），外部引用面过大，不能整块挪走；`CCTexture2D` 包含与 3 个 `GetAdapterTexture` override 包在 `#if defined(KRKR2_BUILD_APP)` 内 |
| visual/RenderManager.h | 留 | `GetAdapterTexture` 改为非纯虚 + 默认 `return origTex`；保留 `namespace cocos2d` 前向声明（无需 cocos 头）；cocos 侧 override 不变 |
| visual/ogl/RenderManager_ogl.cpp | 留，门控去 cocos | ogl 渲染器是 SDL3 路径必需品。`AdapterTexture2D` + 其 `GetAdapterTexture` override、Director setViewport、blendResetToCache、.EVENT_RENDERER_RECREATED 监听器均在 `#if defined(KRKR2_BUILD_APP)` 内 |
| visual/ogl/RenderManager_ogl_test.hpp | 留 | `cocos2d::log` 换 `TJS::TVPConsoleLog`（TEST_SHADER_ENABLED 段，默认不编译） |
| visual/FontImpl.cpp | 留，完全去 cocos | 内置回退字体 `FileUtils::getDataFromFile` 换 `TVPGetFileBackend()->OpenRead`（B2-3 先例：移动端逐字等价，桌面 LocalFileBackend） |
| visual/impl/TVPScreen.cpp | 留，门控去 cocos | 6 个静态函数被 base/SystemImpl、WindowImpl、DInputMgn 等 cocos 外代码引用；`GetHeight()` 在 SDL3 分支保持历史回退值 `GetWidth()`（原实现算出 h 后亦返回 w） |
| visual/LoadPVRv3.cpp | 留，门控去 cocos | 去重复的 `<cocos/base/pvr.h>` include；PVRTC 解码（`PVRTDecompressPVRTC` 由 cocos2dx 库提供）cocos 侧保持原调用，SDL3 侧抛 Unsupported 异常 |

ogl 渲染器去 cocos 逐点（`RenderManager_ogl.cpp` 行号为当前文件）：
`cocos2d::GL::activeTexture/bindTexture2D/bindTexture2DN/useProgram/deleteTexture/enableVertexAttribs`
→ 直接 `glActiveTexture/glBindTexture/glUseProgram/glDeleteTextures` 等同符号
（含文件内新增的 `_glBindTexture2D`/`_glEnableVertexAttribs` 助手，等价于
`cocos2d::GL` 无缓存直通语义）；`cocos2d::log` → `TVPConsoleLog`。新增
`#include <cstdarg>`（`va_start` 之前经 cocos 头链获得）与
`ogl_common.h` 内 `#ifndef CHECK_GL_ERROR_DEBUG` 兜底（cocos 侧宏优先）。

宿主钩子 `TVPGetDefaultRendererName()`（`SysInitIntf.h` 声明，
`SysInitImpl.cpp` 与 `RenderManager.cpp` 两处 "renderer" 默认值使用）：
cocos 宿主（`AppDelegate.cpp`）返回 `"software"`，SDL3 壳
（`SdlEngineRunner.cpp`）返回 `"opengl"`——SDL3 壳没有 software renderer
的呈现管线。用户显式配置 `renderer` 时仍以配置为准。

SDL3 壳视频接缝 stub（`SdlEngineRunner.cpp`）：`GetVideoOverlayObject`
/`GetVideoLayerObject`/`GetMixingVideoOverlayObject`/`GetMFVideoOverlayObject`
四个工厂的 cocos 实现（KRMoviePlayer.cpp/krffmpeg.cpp）随 core_movie_cocos
被 SDL3 图排除，壳内提供 stub：`*out = nullptr` 后抛
`TVPThrowExceptionMessage(TVPErrorInKrMovieDLL, "video playback is not
available in the SDL3 shell yet")`，签名与 `VideoOvlImpl.cpp` 的 extern
声明逐字对齐。

验证（本机无 vcpkg/SDL3/GLEW/cocos 真库，全部为隔离证据）：
`g++ -std=c++17 -DLINUX -fsyntax-only`（/tmp/fmtstub + /tmp/sdl3stub +
/tmp/b4test 的 GL/swscale/lz4/xxhash/TVPMessages.decl.h 存根）对 SDL3 分支
的 `RenderManager.cpp`、`ogl/RenderManager_ogl.cpp`、`FontImpl.cpp`、
`impl/TVPScreen.cpp`、`LoadPVRv3.cpp`、`impl/VideoOvlImpl.cpp`、
`environ/sdl3/SdlEngineRunner.cpp` 全部通过；`KRMovieLayer.cpp`、
`KRMoviePlayer.cpp`、`krffmpeg.cpp` 因依赖 ffmpeg 头未本地编译（内容未改，
归属/consume 关系由 CMake 存根配置验证）。沿用 B2-2 存根
（/tmp/b22/stubs + /tmp/b23pkgconfig）的三次配置：
full（`-DKRKR2_BUILD_APP=ON`）生成 `core_movie_cocos`（KRMoviePlayer.cpp +
krffmpeg.cpp，带 visual/environ/base include 集）并链入 `krkr2` 归档组，
`core_visual_module` 带 `-DKRKR2_BUILD_APP=1`；sdl3（APP=OFF、SDL3=ON）
无 `core_movie_cocos`、`krkr2_sdl3` 链
`core_environ_module+各 domain+core_environ_sdl3+core_render_module`、
visual 带 `-DKRKR2_BUILD_APP=0`、无任何 cocos2dx 引用；along（APP=ON、
SDL3=ON）仅额外编译 `core_environ_sdl3` 库，无 `krkr2_sdl3` 可执行。
`python scripts/check_core_module_deps.py` 通过；改动 C++ 文件经
clang-format 20 `--dry-run -Werror -style=file` 干净。
`krkr2` full 配置与改动前相比无源码级变化：cocos 侧路径全部保持原语句
（`cocos2d::GL::blendResetToCache()`、Director setViewport、
AdapterTexture2D、3 个软件纹理 override 均在门控内逐字保留）。

残留 TODO：
1. SDL3 壳视频播放完全不可用（四个工厂 stub 抛异常）；krkr2_sdl3 下
   `MoviePlayerOverlay/MoviePlayerLayer`/`TVPYUVSprite`/`TVPCheckIsVideoFile`
   均不链接——待 SDL3 侧视频呈现方案（如 core_render 的 GL 纹理路径）。
2. `_RestoreGLStatues` 的 SDL3 分支用 `tTVPScreen::GetWidth/Height()`
   （2048 方形逻辑尺寸）重设视口，TODO B4+ 接 SdlAppDelegate 真实窗口
   尺寸（`gl` 上下文重建通知亦待 `setContextLostCallback` 落地，
   SdlAppDelegate::dispatchRendererRecreated 已留 TODO 注释）。
3. PVRTC 纹理在 SDL3 壳抛 Unsupported（解码器在 cocos2dx 库内）；
   ETC/RGBA 等格式经本地 ogl/etcpak 等仍可用。
4. `KRMovieLayer.cpp` 留在 core_movie_module 内仅靠"无引用方不拉取"
   保证不破坏 SDL3 图；若后续 cocos/movie 侧接缝再变，需复核。
5. `RenderManager.h` 仍保留 `namespace cocos2d { class Texture2D; }`
   前向声明与 `GetAdapterTexture` 签名（cocos 侧适配半边）；SDL3 路径
   的纹理适配由 cpp/core/render/KrkrTexture 承担，等 B4+ 渲染集成后
   可考虑把该虚方法彻底撤出接口。
