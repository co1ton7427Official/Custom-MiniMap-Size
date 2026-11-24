# minimap.asi — GTA:San Andreas 雷达（小地图）位置 / 大小 / 形状修改插件

**作者：Colton**

一个从 `minimap_config.ini` 读取参数的 ASI 插件（C++ / Win32），可以任意摆放雷达、
改变它的尺寸，并可选把它从圆形变成方形。适用于 **GTA:SA 1.0 US + SA-MP**。

> 本文是**技术文档**（讲清为什么这么 hook、反汇编证据、自检方法）。
> 面向使用者的完整手册在 **[`使用文档.md`](使用文档.md)** —— 安装、配置参考、
> 配方速查、日志对照表、故障排除都在那里。

> **状态说明（请先读）**
> - **已经编译好了**：`build\minimap.asi`（51,712 字节，PE32 / i386，已用
>   `_tc\pe.py` 校验过头部）。编译用的是 Zig 0.13.0 自带的 clang + lld
>   （`build_zig.bat`），不需要 VS。
> - **已经在真实 exe 上核对过每一个地址**：`E:\GTA San Andreas\gta_sa.exe`
>   （14,383,616 字节 = 1.0 US / Hoodlum）里 0x583480、0x5832F0、0x58585C、
>   0x58A330 以及 4 个圆盘字节全部逐字节比对过，见第 6 节。
> - **坐标数学、跳板、加载流程都跑过自检**（`verify_math.py`、`thunk_test.exe`、
>   `load_test.exe`），全部 `ALL OK`。
> - **已进游戏实测通过**：在 `E:\GTA San Andreas`（SA-MP 客户端）里加载成功，
>   日志见第 5 节，0x583480 的 hook 装上、矩形按 2560×1600 正确计算。

---

## 1. 回答核心问题：该 Hook 哪个函数？

GTA:SA **没有**一个"`DrawRadar()` 一把梭"的函数可以改雷达矩形。左下角小地图是
**9 块流式地图拼图（tile）+ 所有 blip + 玩家箭头 + 北向标记 + 帮派地盘 + 路径点**
拼出来的，但它们的屏幕坐标**全部**经过同一个函数：

```
CRadar::TransformRadarPointToScreenSpace(CVector2D& out, CVector2D const& in)   // 0x583480
```

- 雷达坐标系是 **[-1, +1] × [-1, +1]**（`in` 在这个范围里）。
- 原版实现（gta-reversed / re3 的 `Radar.cpp`）：

```cpp
out.x = SCREEN_STRETCH_X(94.0f) / 2.0f + SCREEN_STRETCH_X(40.0f)
      + SCREEN_STRETCH_X(94.0f * in.x) / 2.0f;
out.y = SCREEN_STRETCH_FROM_BOTTOM(104.0f) + SCREEN_STRETCH_Y(76.0f) / 2.0f
      - SCREEN_STRETCH_Y(76.0f * in.y) / 2.0f;
```

也就是说，`94×76`、左边距 `40`、底边距 `104` 这些"雷达矩形常量"是写死在这个函数
里的。**只要把这个函数整体换成自己的实现，就等于同时改了雷达的位置和大小**——
不需要去改 9 个 tile 的绘制，也不需要动裁剪算法。

反汇编已经证实了这一点（`gta_sa.exe` 0x5834B4 起）：函数体里依次 `fld` 了
`[0xC17044]`（屏宽）、`[0x00859520]`（= 1/640）、`[0x00866b78]`（= 94.0）、
`[0x00858b8c]`（= 0.5）、`[0x00858a10]`（= 40.0），再 `[0xC17048]`（屏高）、
`[0x00859524]`（= 1/448）、`[0x00866b74]`（= 76.0）、`[0x00866b70]`（= 104.0）。
所以原版的缩放是 **X、Y 各自独立按 640 / 448 归一化**，本插件的 `StretchX/StretchY`
就是这个，默认值下和原版**逐像素一致**。

这就是本插件的做法：**不是只 Hook，而是完整替换（detour + 自己的实现）**，
映射公式变成：

```cpp
out.x = left + (in.x + 1.0f) * 0.5f * width;    // 左边界 + 归一化 x
out.y = top  + (1.0f - in.y) * 0.5f * height;   // 上边界 + 翻转的 y（屏幕 y 向下）
```

其中 `left / top / width / height` 由 INI 换算得到（下文 3.2）。

### 形状不是几何，是模板遮罩（stencil mask）

"圆形"并不是靠几何裁剪做出来的。`CRadar::DrawRadarMask()`（**0x585700**）会从雷达
四角 `(±1, ±1)` 各甩出一个 7 段的 1/4 圆弧扇形，取并集写成一张模板遮罩；地图 tile
只有在遮罩里才画得出来，于是看起来是圆的。

扇形的角度增量是通过一个**立即数操作数**去读的浮点常量（那个 4 字节操作数在
**0x58585C** 处，存的是常量地址）。反汇编确认它指向 `0x00858F1C`，值 =
**0.2617993950843811 = π/12**（每段 15°，7 段 = 90°）。所以经典做法是：把那个
操作数重定向到一个 ≈0 的 float（`0.000001f`），四个扇形全部退化成零面积，遮罩什么
都挡不住，地图就铺满了矩形区域 —— **方形雷达**。这是社区里 `Rect Radar` /
`radarrect` 一直在用的做法，本插件把它做成了**可开关、可还原**的版本（保存原 4
字节，关掉时写回去）。

### 方形的第二个必要条件：`CRadar::LimitRadarPoint`

`CRadar::LimitRadarPoint(CVector2D& point)`（**0x5832F0**）会把超范围的 blip 拉回
**单位圆**上（`if (mag > 1.0f) point = point.Normalized();`）。遮罩变方之后，blip 仍
会被拉到内切圆上，角上永远不会有图标，所以要再 Hook 一次，改成**方形钳制**：
把点投影到单位**正方形**的边上（`p /= max(|x|, |y|)`）。本插件只在方形模式下装这个
Hook，圆形模式会把它卸掉。

> 注意：这个 exe 里 `0x5832F0` 本身就是一条 `E9 D0 EB E7 FF`（跳到 `0x00401EC5`），
> 属于 Hoodlum 版本的"import 式跳板"。插件覆盖它是正常的，日志里那句
> `already starts with E9 (conflicting patch?)` 是**预期行为**，不是错误。

### 第三个补充：圆盘 HUD 贴图（这是最容易踩坑的地方）

原版在 `CHud::DrawRadar()`（**0x58A330**）里先画一张 `HUD_RADARDISC` 圆盘底图和
描边。**关键点：这个函数用的是它自己写死的 94/76/40/104 常量（同样内联的
1/640、1/448、94.0、76.0、104.0、40.0），完全不经过
`TransformRadarPointToScreenSpace`。**

也就是说：**只要你把雷达挪了位置或改了大小，那个圆环就会留在原来的地方**，叠在
地图上变成"多出来一个圈"。

所以插件的行为是：只要**配置出来的矩形和原版矩形不一样**（或者开了方形），就默认把
4 处画圆盘的 `push 0xFF`（**0x58A789 / 0x58A88F / 0x58A8D9 / 0x58A98F**）的
alpha 立即数改成 0，让这几个 sprite 画成全透明——它们属于同一个 CSprite2d 对象
`0x00BAB208`，不动血条/武器/金钱那些（那些在别的 CSprite2d 对象上，反汇编核对过）。
`KeepRadarDisc=1` 可以强行保留原贴图（比如你只挪了几个像素，想留着圆环）。

### 一图流总结

| 目的 | 手段 | 地址 |
|---|---|---|
| 位置 + 大小 | 完整替换 `TransformRadarPointToScreenSpace` | `0x583480` |
| 方形（去圆弧遮罩） | 改 `DrawRadarMask` 里的圆弧半径常量指针 | `0x58585C` |
| 方形（blip 不打转） | 替换 `LimitRadarPoint` 为方形钳制 | `0x5832F0` |
| 去掉圆形底图 | 把 `CHud::DrawRadar` 里 4 处 `push 0xFF` 改成 `push 0` | `0x58A789/8A88F/8A8D9/8A98F` |

另外注意：暂停菜单里的大地图（`FrontEndMenuManager.m_bDrawingMap`，绝对地址
**0xBA67A1**）用的是另一套坐标（`m_vMapOrigin + m_fMapZoom * in`）。替换函数里必须
**原样保留这个分支**，否则一按 ESC 看大地图就是一片错位。插件已经这么做了。
（这两个偏移也是反汇编证实的：0x583480 开头就是 `A0 A1 67 BA 00`＝`mov al,[0xBA67A1]`，
紧接着 `D9 05 AC 67 BA 00`＝`fld [0xBA67AC]`。）

---

## 2. 目录结构

```
gta_radar_asi/
├─ build/minimap.asi        ← 编好的成品，直接复制进游戏目录
├─ minimap_config.ini       配置（放到游戏目录，和 .asi 同级）
├─ build_zig.bat            ★ 推荐：用 Zig 自带的 clang 编译（本机就是这么编的）
├─ build.bat                用 MSVC（VS2019/2022 x86）编译
├─ CMakeLists.txt           CMake 工程（强制 32 位）
├─ verify_math.py           坐标数学自检
├─ 使用文档.md               面向使用者的完整手册
├─ 开发文档.md               面向后续开发者的架构/修改/测试文档
├─ test/thunk_test.cpp      跳板自检（保护 ECX/EDX、传参、栈平衡）
├─ test/load_test.cpp       加载器自检（真实 LoadLibrary 一遍，含 ini 解析）
└─ src/
   ├─ main.cpp             DllMain + 初始化线程 + 找 ini/log 路径
   ├─ radar.cpp            核心：两个 detour 的 C++ 实现 + 内存补丁 + 热重载
   ├─ config.cpp           INI 读写（GetPrivateProfileInt / String）
   ├─ game.h               GTA:SA 1.0 US 地址表 + 内存访问小工具
   ├─ hook.cpp             可逆的 5 字节 E9 inline hook（含远距离 relay）
   ├─ thunk.cpp            运行时生成"保护 ECX/EDX"的 cdecl 跳板
   ├─ log.cpp              minimap.log，带时间戳
   └─ strutil.h            MSVC/MinGW 双兼容的字符串小工具
```

---

## 3. 实现要点（为什么这么写）

### 3.1 为什么用运行时生成的跳板（thunk），而不是 `__declspec(naked)`

gta-reversed 的 `Radar.cpp` 里有一句关键注释：

```cpp
// CRadar::DrawRadarSection assumes this function won't spoil EDX, but it does,
// so we need to keep it up to date.
_asm { push edx }   ...   _asm { pop edx }
```

原函数**会把 EDX 踩掉**，而调用方（`DrawRadarSection`）恰好依赖 EDX 里的值，
所以官方自己都在里面手工保存/恢复。任何替换实现**必须同样保住 ECX/EDX**，
否则地图 tile 的顶点计算会错乱（表现是地图抖动/错位）。

用 MSVC 的内联汇编（`__declspec(naked)` + `_asm`）当然可以，但它依赖具体的
MSVC 版本怎么解析符号名，而且写出来不好审。`src/thunk.cpp` 的做法是
**在运行时生成机器码**，一条一条写清楚：

```asm
push ecx                        ; 51
push edx                        ; 52
mov  ecx, esp                   ; 89 E1
push dword ptr [ecx+12]         ; FF 71 0C   ; arg0
push dword ptr [ecx+16]         ; FF 71 10   ; arg1
mov  eax, offset core           ; B8 xx xx xx xx
call eax                        ; FF D0
add  esp, 8                     ; 83 C4 08
pop  edx                        ; 5A
pop  ecx                        ; 59
ret                             ; C3
```

寄存器全保住，参数按 cdecl 原样转给 `TransformCore` / `LimitCore`。跳板分配在
`PAGE_EXECUTE_READWRITE` 的页上，两个 thunk 一共 23 + 20 字节。
`test/thunk_test.cpp` 在宿主进程里真的跑了 100000 次，验证 ECX/EDX 原值不变、
参数正确、栈平衡 —— 输出 `ALL OK`。

### 3.2 INI → 像素：两种单位 + 四种锚点

- `Units=STRETCH`（默认）：X/Y/Width/Height 是 **640×448 参考单位**，按游戏自己的
  `SCREEN_STRETCH_X/Y` 缩放，即 **X 和 Y 分别独立缩放**（`v * w/640`、`v * h/448`）。
  这正是原版的缩放方式，所以默认值 `40 / 28 / 94 / 76` **像素级还原原版雷达**。
  我用 `verify_math.py` 在 640×448、800×600、1280×720、1920×1080、2560×1600、
  3840×2160 六种分辨率 × 25 个采样点上比对过：最大误差 **0.000000000 px**。
- `Units=PIXELS`：直接当像素用，简单但换分辨率后需要自己调。
- `Anchor`：`TOPLEFT / TOPRIGHT / BOTTOMLEFT / BOTTOMRIGHT`，决定 X/Y 从哪个角量起。
  底边锚点时 `top = screenH - Y - height`（"距底边 Y 像素"）。

### 3.3 可逆 + 有日志 + 有版本检查

对照组里那个著名的 gist（`4el0ve4ik/radarrect`）：所有写入都在 `DllMain` 里裸干，
没有版本检查、没有还原、写坏了只能重开游戏。本插件的对应做法：

- 每次写内存前先 `VirtualQuery` 确认目标在**游戏主模块镜像**里，否则拒绝；
- 改之前保存原字节，关掉功能/卸载 DLL 时写回（`ApplyArcPatch` / `ApplyDiscPatch`
  都是幂等的）；
- 只接受 **1.0 US（Hoodlum，14,383,616 字节）**，`IgnoreVersionCheck=1` 才放行；
- 全部动作写进 `minimap.log`，包括每个 hook 的实际地址和长度。

### 3.4 热重载：线程只举旗，不干活

`ReloadThread` 每 750 ms 比对一次 `minimap_config.ini` 的 `ftLastWriteTime`，
有变化就 `InterlockedExchange(&g_reloadFlag, 1)` 然后继续睡。真正的重读发生在
**游戏线程**里（`TransformCore` 每帧都会调 `ConsumeReloadRequest()`，用一个廉价
的 `if (g_reloadFlag == 0) return;` 快速路径过滤掉 99.99% 的调用）。这样配置对象
永远只有一个线程在改，不需要锁——因为这个函数每帧被调用几百次（9 个 tile +
每个 blip 一次），加锁会直接影响帧率。

改完 `Width/Height/X/Y/ForceSquare` **不用重启游戏**：几何下一帧生效，形状补丁
立刻生效。

### 3.5 安全网

- `Enable=0`：插件加载但什么都不改。
- 版本不符/写入失败：日志里写清楚，游戏保持原样。
- `DllMain` 里只起线程不做重活（`DisableThreadLibraryCalls` + 300 ms 延迟），
  避免 loader lock 死锁。
- 屏幕尺寸还没准备好时（初始化可能比 `RsGlobal` 赋值早），会在拿到有效屏宽后的
  第一次绘制时重新算一遍矩形并重新决定圆盘开关。

---

## 4. 编译

### 方式 A：用已经下好的 Zig（本机可用，推荐）

```bat
cmd /c build_zig.bat
```

它用的就是本机已经放好的 `_tc\py\ziglang\zig.exe`（Zig 0.13.0，自带 clang + lld +
MinGW-w64 头文件），产物 `build\minimap.asi`。**不需要装 VS，也不需要联网**。

### 方式 B：`build.bat`（MSVC）

在装了 **Visual Studio 2019/2022（含 C++ x86 工具集）** 的机器上直接双击，
或从"x86 Native Tools Command Prompt"里运行：

```bat
build.bat
```

关键参数：`/LD`（DLL）、`/MT`（静态 CRT）、`/O2`、`/GS-`、
`/DYNAMICBASE:NO /NXCOMPAT:NO`（和游戏本体保持一致）。

### 方式 C：MinGW-w64（需要 i686 版本）

```bat
build_mingw.bat C:\mingw32\bin
```

### 方式 D：CMake

```bat
cmake -A Win32 -B build
cmake --build build --config Release
```

`CMakeLists.txt` 里有一条硬检查：不是 32 位就直接报错退出——因为源码里所有地址
都是 32 位虚拟地址，编成 64 位 DLL 必崩。

---

## 5. 安装

目标目录已经确认存在：`E:\GTA San Andreas`（`gta_sa.exe` = 14,383,616 字节，
`samp.exe`、`CLEO.asi`、`modloader.asi`、`codsmp.asi` 都在，ASI 加载器是活的）。

1. 复制 `build\minimap.asi` → `E:\GTA San Andreas\minimap.asi`
2. 复制 `minimap_config.ini` → `E:\GTA San Andreas\minimap_config.ini`
3. 进游戏。`minimap.log` 会写在同一目录。

调试：看 `minimap.log`。这是真实运行时抓到的成功日志（2560×1600、默认配置）：

```
[22:41:57.202] === minimap.asi log ===
[22:41:57.202] minimap.asi starting, module=61F50000
[22:41:57.202] plugin folder ini: E:\GTA San Andreas\minimap_config.ini
[22:41:57.203] config: Enable=1 ForceSquare=0 KeepRadarDisc=0 HotReload=1 IgnoreVersionCheck=0
[22:41:57.203] config: Anchor=BottomLeft Units=Stretch X=40.0 Y=28.0 Width=94.0 Height=76.0
[22:41:57.203] version: E:\GTA San Andreas\gta_sa.exe is 14383616 bytes
[22:41:57.203] thunk: 23-byte stub at 0AFF0000 -> 61F51450 (2 args)
[22:41:57.203] thunk: 20-byte stub at 0B000000 -> 61F51980 (1 args)
[22:41:57.203] hook: installed at 0x00583480 -> 0AFF0000 (5 bytes)
[22:41:57.203] minimap: active
[22:42:01.294] rect: screen 2560x1600 -> left=160.00 top=1228.57 width=376.00 height=271.43
```

> ⚠️ 你装了 `codsmp.asi`，它的 `codsmp.ini` 里有 `MinimapZoom = true`，而且它的
> 二进制里确实引用了 `0xBA8314`（`CRadar::m_radarRange`）。如果你发现雷达的
> **缩放比例**被别的东西抢着改，把 `MinimapZoom` 改成 `false` 再看一次。
> 位置/尺寸/形状（本插件改的东西）和它改的半径不是同一个变量，理论上不冲突，
> 但两个一起开不好排查问题。

---

## 6. 自检（都不需要进游戏）

```bat
python verify_math.py          :: 坐标数学
build\thunk_test.exe           :: 跳板（ECX/EDX 保护、传参、栈平衡）
build\load_test.exe build\minimap.asi test\load_test.ini   :: 加载 + ini 解析
```

实测输出：

```
verify_math:
  defaults vs vanilla, worst error = 0.000000000 px
  5 anchor/unit combinations land exactly on their rectangle
  square clamp keeps direction and lets blips reach the corners
  pause-menu map keeps m_vMapOrigin + m_fMapZoom * in
ALL OK

thunk_test: all 12 checks pass -> ALL OK

load_test: pass 1 (no ini) + pass 2 (custom ini) -> ALL OK
```

另外，`_tc\` 里有一组针对真实 exe 的核对脚本（都可以重跑）：

| 脚本 | 作用 |
|---|---|
| `_tc\pe.py` | 解析 minimap.asi 的 PE 头（必须是 i386 / DLL / PE32） |
| `_tc\verify_exe.py` | 在 `gta_sa.exe` 里 dump 指定 VA 的字节 |
| `_tc\callers.py` | 找 `E8 rel32` 调用点，确认没有调用方依赖返回值/寄存器 |
| `_tc\dumpfn.py` | 反汇编窗口式 dump |
| `_tc\hudconsts.py` | 扫描 `D9/D8/DD/DB [abs32]` 浮点常量，核实 94/76/40/104、1/640、1/448 |
| `_tc\discargs.py` / `_tc\discsprite2.py` | 确认 4 个圆盘字节画的是同一个 CSprite2d |

已核对结论：
- `0x583480` 前 4 字节 `A0 A1 67 BA 00`；`0x5832F0` = `E9 D0 EB E7 FF` →
  `0x00401EC5`；`0x58585C` 操作数 = `0x00858F1C`（=π/12）；`0x58A330` 前 8 字节
  `A1 CC A7 96 00 83 EC 34`；4 个圆盘字节全是 `FF`（在 `68 FF 00 00 00` 里）。
- `0x583480` 有 33 个调用点、`0x5832F0` 有 27 个，全部只从栈上取结果，
  **没有一个读 EAX/EDX 作为返回值** ⇒ void 替换是安全的。

---

## 7. 已知限制

- **已在游戏中实测通过**（`E:\GTA San Andreas` + SA-MP）：hook 装上、矩形正确、
  插件未引起崩溃。更广泛的场景（不同服务器、不同 HUD 插件组合）仍建议自己验一次。
- 只针对 **1.0 US**。1.01 / 2.0 / 3.0（Steam 新版）地址不同，需要另做地址表。
- 方形模式用的是社区验证过的"塌陷扇形"技巧；它依赖 `DrawRadarMask` 里那个
  4 字节操作数的位置。版本变了这一处会最先失效——日志里会显示
  "not inside the game image"或 hook 失败，**不会静默改坏**。
- 满屏大地图（暂停菜单）保持原版行为不改，这是故意的。
- **非方形 + 自定义矩形**时，blip 仍被钳制在单位**圆**上（映射后是内切椭圆），
  到不了矩形四角。想贴满四角得开 `ForceSquare=1`，但那样会被强制成正方形。
- 雷达**朝向**（`m_fRadarOrientation`, 0xBA8310）、`m_radarRange`（地图比例尺,
  0xBA8314）没有暴露到 INI；想要"雷达范围"可调，下一步可以加 `Range=` 键直接写
  `0xBA8314` 的 float。
- 运行时依赖 Universal CRT（`api-ms-win-crt-*` → `ucrtbase.dll`）。Win10/11 自带；
  Win7 需要 KB2999226。

---

## 8. 作者

**Colton**

使用手册（安装 / 配置参考 / 配方 / 日志对照 / 故障排除）：**[`使用文档.md`](使用文档.md)**
开发文档（架构 / 四个补丁位点 / 修改指南 / 测试体系 / 逆向脚本）：**[`开发文档.md`](开发文档.md)**
