# 窗口接口拆分（B2 首批）

## 可选的 KAG 按钮方向导航

启动参数 `-controllerfocus=yes` 开启 `LayerManager` 的空间按钮导航，默认关闭。
没有 Shift/Ctrl/Alt 修饰的方向键按当前图层位置选择可见、启用且参与焦点链的
`ButtonLayer`，包括由脚本关闭 focusable 的鼠标按钮；选中时恢复其 focusable，
仍经现有 onFocus 处理高亮。Enter 在按下、抬起时均选中同一有效按钮才通过原鼠标
命中测试与 down/click/up 路径激活，避免只实现鼠标 onExecute 的按钮忽略键盘确认；
此时不再把 Enter 交给按钮键盘回调，避免重复激活。候选集合每次重算，过滤父级裁剪、
透明/隐藏/禁用节点及当前模态层之外的按钮。方向无候选时按屏幕阅读顺序循环。
焦点在其他独立控件上时保留原方向键行为，不抢占文本编辑。

这一兼容选项只依赖 KAG 通用 ButtonLayer 类型和图层状态，不包含游戏名、按钮名、
固定坐标或商业脚本。任意自绘菜单不一定遵循该协议，仍需游戏自身导航或鼠标输入。
`KRKR2_ENABLE_X11_INPUT_SMOKE` 同时注册 `x11-controller-focus-runtime`，
使用独立合成界面和真实 X11 按键覆盖默认关闭、反向创建顺序、隐藏/禁用/越界按钮、
文本控件、模态范围和 Enter 回调；不等于所有 KAG 游戏均已验证。

状态：从 Cocos 窗口实现中拆出通用接口，保留现有后端。
本批不替换窗口、输入、渲染、媒体后端或平台库。

## 边界

- `cpp/core/visual/WindowLayer.h` 承载 `iWindowLayer`、修饰键别名和方向常量，
  包含已有 `WindowIntf.h`，不包含 Cocos 或控制台 UI。
  消息接收器注册枚举移至 `WindowIntf.h`，枚举值保持不变。
- `environ/impl/TVPWindow.h` 保留 `TVPWindowLayer`、Cocos ScrollView、输入适配及窗口管理实现。
  `visual/impl/WindowImpl.cpp` 和 `BasicDrawDevice.cpp` 改为包含通用接口。
- 通用接口不再提供返回 Cocos Node 的 `GetPrimaryArea`。
  仍依赖 Cocos 的视频呈现通过 `environ/cocos2d/CocosWindowAccess.h` 的
  `TVPGetCocosWindowPrimaryArea` 借用节点，所有权保持属于窗口。
  实现在 `TVPWindow.cpp`，不匹配的后端抛出 `std::bad_cast`，不会静默跳过视频。
- 原接口中的空实现及既有限制原样迁移；本批不宣称补齐全屏、位置、触摸查询等功能。
  当前接口仍依赖 TJS/WindowIntf，并非完全独立的宿主 API。

`scripts/check_core_module_deps.py` 额外拒绝上述消费者重新包含 `TVPWindow.h`，
并检查通用头不直接暴露 Cocos 类型；该静态检查不能代替传递包含检查或全模块无环证明。

## 验证方法

真实交互使用自制 [窗口场景](../../tools/game-smoke/interaction/README.md)，
通过原脚本入口、原游戏菜单/文件选择与消息框、原关闭事件运行。
脚本请求文件仅负责打开对话框和发起 close；不能伪造用户点击或成功返回值。
控制器要求实际菜单事件、确认/取消返回值、选中文件名及关闭回调，空运行不能通过。

测量、排序补丁来源、截图与本机运行结果见 [窗口验证记录](../../.agents/notes/implemented/testing/2026-11-03-window-interface-validation.md)。

## Cocos UI 约束

`vcpkg/ports/cocos2dx/patch/fix-node-order.patch` 直接比较 localZOrder 与 orderOfArrival，避免打包排序键的字节序依赖；更新 port 后需要重建依赖。
共享 UI 按实际分配的行高限制文件名、标题与底栏字号，嵌入节点使用父级局部原点。
`tests/unit-tests/core/environ/NodeOrderTest.cpp` 在 full 范围验证层级与同层顺序，不创建 GL 窗口。
`CocosPointerCoordinatesTest.cpp` 另验证真实节点变换下的主图层坐标、越界位置及光标父节点坐标。
两者共 5 项，由完整 Windows 工作流实际注册、运行。
焦点订阅另有 4 项 EventDispatcher 用例，提交文本另有 2 项真实 Cocos 转换对照；
该工作流合计发现 11 项，不创建 OS/GL 窗口。

`tools/game-smoke/pointer_windows_repro.py` 通过显式测试驱动创建真实双 Window，
检查切窗、隐藏和失效后的输入归属及局部坐标。正常切窗与隐藏回退会恢复目标 Cocos 节点的原点；
管理 overlay 的动画仍使用原入口。运行方法和软件 OpenGL 边界见[交互场景](../../tools/game-smoke/interaction/README.md#跨帧触摸回调探针)。

## 后续已拆分的边界

共享虚拟按键状态、修饰键快照及指针局部坐标规则迁至无后端依赖的 `core_input`，见[输入状态](input-state.md)。
`SystemImpl.cpp` 不再为查询状态包含具体 Cocos 窗口头。

`event/PostUpdateEvent.h/.cpp` 保存唯一的 post-update 回调，供普通帧和模态绘制共同调用。
原头文件内部链接变量会造成跨编译单元的注册不可见，已有双单元实验及三个回归用例。
`environ/RenderUtils.h` 仅声明模态绘制入口，Cocos 绘制与 swap 位于对应 cpp；
绘制次序仍为 hook → drawScene → swap，计时公式保持不变。

`environ/HostLifecycle.h` 提供前台、后台引擎通知，不含 Cocos/TJS 类型。
Cocos AppDelegate 先通知引擎，再启停动画；引擎的音量、混音锁、compact 与 System 事件顺序未变。
连续激活事件沿用原队列合并与分发时状态核对，不保证快速切换的每一边沿都通知脚本。
此接口不拥有平台事件泵、GL context 或窗口销毁，不改变旧 `OnExit` 的接线与退出次序。

Cocos 桌面已订阅独立焦点事件并取消游戏输入，见[输入状态](input-state.md#cocos-桌面焦点)。
这些子批次未完成 DPI、IME、真实切焦交互、移动端挂起恢复或完整生命周期迁移。
具体证据与剩余工作见[B2 后续记录](../../.agents/notes/proposed/architecture/2026-11-03-platform-contracts.md)。

## B2-2：SDL3 壳接线

`KRKR2_BUILD_SDL3_APP=ON -DKRKR2_BUILD_APP=OFF` 生成 `krkr2_sdl3` 可执行目标，
链接 `krkr2core` 的非 cocos 组合（`core_environ_module` + domain 环，
`core_environ_sdl3` 替换 `core_environ_cocos`）与 SDL3。两套壳各自实现
`TVPCreateAndAddWindow` 等宿主钩子，不能同图链接，故
`KRKR2_BUILD_APP=ON` 且 `KRKR2_BUILD_SDL3_APP=ON` 时只单独编译
`core_environ_sdl3` 库做语法验证，不出可执行；该组合下 `KRKR2_USE_COCOS_UI`
按原逻辑取值（不强制）。仅 SDL3-only 组合（`KRKR2_BUILD_SDL3_APP=ON`
且 `KRKR2_BUILD_APP=OFF`）强制 `KRKR2_USE_COCOS_UI=OFF`，SDL3 壳按
桌面 CLI 启动（无文件选择 UI，`--game PATH` 由
`krkr::launch::PrepareDesktopLaunch` 强制）。

`environ/sdl3/SdlEngineRunner` 是组合根，等价替换
`environ/cocos2d/AppDelegate` + `TVPMainScene` 的启动与帧路径：
`init()` 设置 `TVPMainThreadID`、`TVPInitNativeDialogBackend`、窗口/上下文与引擎钩子；
`startEngine()` 镜像 `MainScene.cpp:700` 调 `Application->StartApplication(DesktopTarget())`；
帧回调镜像 `MainScene.cpp:747-753` 的 `update()`：`Application->Run()` +
`iTVPTexture2D::RecycleProcess()` + `TVPInvokePostUpdateEvent()`。
`Application->Run()` 内的 `SystemWatchTimerTimer` 负责
`TVPDeliverAllEvents`（`SystemControl.cpp:112-119`）与每个窗口的 `TickBeat`，
失焦前台后台经 `HostLifecycle.h` 走 `Application.cpp:749-751`。
模块内还提供原 `core_environ_cocos` 独有的链接钩子：`TVPCreateAndAddWindow`、
`TVPConsoleLog`（两个重载）、`TVPOpenPatchLibUrl`（`SDL_OpenURL`）。

输入/IME/DPI 具体接线：
- `SdlAppDelegate::postVirtualKey` 改为先按 `SdlWindowLayer::InternalKeyDown`/
  `OnKeyUp` 转发（`MainScene.cpp:908-915/977-982`），为鼠标键模拟留出挂载点。
- UseMouseKey 模拟整体移植 `TVPWindow.cpp:698-837`（`GenerateMouseEvent`
  加速度环 + `InternalKeyDown` 方向键按下/`OnKeyUp` 弹起 + `TickBeat` 巡航）；
  与 cocos 差别：模拟按下同时写入 `krkr::input::sharedState`，因为本宿主的
  修饰键/取消路径只认共享状态。
- 失焦取消改走 `SdlWindowLayer::cancelInput` + `krkr::input::planInputCancellation`
  （`TVPWindow.cpp:346-389` 语义）。
- IME：`SdlIMEDelegate` 由 runner 内 `EngineIME` 实现，
  `insertText` 镜像 `MainScene.cpp:1291-1307`（`decodeTextInput` 后按
  UTF-16 单元 `OnKeyPress`），`deleteBackward` 在非 Win32 合成 `VK_BACK`
  down/up（`:1278-1288`）；`SetImeMode`/`SetCurrentWindow` 经
  `SdlWindowLayer::SetImeModeListener` 镜像 `TVPWindow.cpp:423-452` 的
  `UpdateImeMode`，映射到 `SdlAppDelegate::setTextInputEnabled`。
  preedit 为本宿主新能力，目前仅记录、未入引擎事件。
- DPI：`SdlAppDelegate::getDPI()`（`96 * SDL_GetWindowDisplayScale`）经
  `SdlWindowLayer::SetDpiProvider` 注入 `touchMoveThresholdSq`，
  等价 `MainScene.cpp:539-542`。`MainScene.cpp:832` 的 UI 缩放只服务
  管理 UI 表单（`BaseForm` 等），SDL3 配置不带这些 UI，无需等价消费点。

已知缺口（阻断点）：配置级目标已验证，但 `krkr2_sdl3` 尚不能完整链接——
`core_environ_cocos` 独有的 `LocaleConfigManager`/`IndividualConfigManager`
实现被非 cocos 模块（`Application.cpp`、`base/{ScriptMgnIntf,SystemIntf}.cpp`、
`sysinit/impl/SysInitImpl.cpp`、`visual/*` 多处）在链接时引用，
而这两个管理器源码本身包含 `CCFileUtils`/`ui/UIText` 等 Cocos 头，
无法按 B2-0 拆分契约直接移回 `core_environ_module`。
渲染合成（含 `EVENT_RENDERER_RECREATED` 派发与上下文丢失复位）属于 B3。
