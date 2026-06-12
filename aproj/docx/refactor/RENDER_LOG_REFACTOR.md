# 渲染指令记录重构方案

## 一、问题背景

当前渲染指令记录在 aproj 和 LibreOffice 各自实现，存在两个根本问题：

1. **实现分裂**：aproj 的 `RenderLogger` 通过遍历 Frame 树（`LogFrameTree` → `LogFrameRecursive`）生成指令；LibreOffice 的 `SwPaintEventListener` 在 `PaintSwFrame` 流程中拦截。两者逻辑独立，调用位置不同，容易遗漏渲染指令。
2. **覆盖不全**：当前 frame 层只记录了 PAGE_START/END 和 TEXT_FRAME，TABLE、IMAGE、RECT、LINE 等元素均未实现。frame 层需要逐个元素手动接入，工作量大且容易遗漏。

**目标**：在 VCL 或 Skia 层建立统一的渲染指令记录机制，作为 frame 层记录的"第二道校验"，确保 aproj 和 LibreOffice 输出一致。

## 核心原则：统一渲染框架

> **aproj 必须使用与 VCL 相同的 `OutputDevice` 接口进行渲染。**

这不是可选的优化，而是根本性的架构约束。原因：

1. **消除渲染架构差异**：如果 aproj 和 LibreOffice 使用不同的渲染代码路径，那么两侧的渲染指令差异可能来自两个来源——排版逻辑差异 或 渲染架构差异。这使得比对结果无法定位问题根因。

2. **聚焦排版逻辑**：当两侧使用完全相同的 `OutputDevice::DrawText/DrawRect/DrawLine` 调用路径时，渲染指令的任何差异**必定**来自 sw 模块的排版逻辑差异（Frame 位置、大小、文本内容、字体属性等）。这使得我们可以集中精力检查 sw 实现逻辑，找到问题并加入 aproj。

3. **代码路径对称**：
   ```
   LibreOffice:  SwTextFrame::PaintSwFrame → OutputDevice::DrawText → GDIMetaFile → RenderInstruction
   aproj:        SwTextFrame::PaintSwFrame → OutputDevice::DrawText → RenderInstructionOutputDevice → RenderInstruction
   ```
   两侧的 `PaintSwFrame` 方法调用相同的 `OutputDevice` API，只是 OutputDevice 的实现不同（一个录制到 GDIMetaFile，一个直接输出 RenderInstruction）。

4. **渐进式验证**：随着 aproj 逐步实现更多排版逻辑（表格、图片、FlyFrame 等），每新增一种元素，其 `PaintSwFrame` 实现自然调用 `OutputDevice::DrawRect/DrawBitmap` 等方法，渲染指令自动被记录，无需额外接入工作。

**实施方式**：aproj 不直接链接 VCL 库（依赖链太重），而是在 aproj 内定义与 VCL `OutputDevice` API 完全一致的抽象接口。aproj 的 `PaintSwFrame()` 方法通过该接口绘制，实现类将 Draw* 调用转换为 `RenderInstruction`。这保证了代码路径的对称性。

---

## 二、当前架构分析

### 2.1 已完成的工作（参考 TODO2.md）

| 阶段 | 状态 | 说明 |
|------|------|------|
| Phase 1: SwDoc 管线修复 | ✅ 完成 | InitLayout/MakeFrames 正确存储 RootFrame |
| Phase 2: 共享渲染指令格式 | ✅ 完成 | `render_instruction.h` POD 结构体 + TSV 格式 |
| Phase 3: aproj 侧实现 | ✅ 完成 | RenderLogger + 端到端测试 + 比对工具 |
| Phase 4: LibreOffice 侧植入 | ✅ 编译通过 | SwPaintEventListener 在 PaintSwFrame 中植入 |
| 后续：比对测试 | ⏳ 待执行 | 需构建 aproj 可执行文件并运行比对 |

### 2.2 渲染管线层次

```
┌─────────────────────────────────────────────────────────┐
│  Writer (sw)                                            │
│  SwRootFrame::PaintSwFrame()                            │
│    → 遍历 PageFrame / TextFrame / TabFrame / FlyFrame   │
│    → 调用 OutputDevice::DrawText/DrawRect/DrawLine/...  │
│    → [当前] SwPaintEventListener 在此层拦截              │
├─────────────────────────────────────────────────────────┤
│  VCL (OutputDevice)                                     │
│  DrawRect() / DrawLine() / DrawText() / DrawBitmap()    │
│    → MetaFile 录制（如果启用）                           │
│    → 坐标变换 → AcquireGraphics → InitClip/Fill/Line    │
│    → 委托给 SalGraphics → SalGraphicsImpl               │
│    → [可选方案A] 在此层记录                              │
├─────────────────────────────────────────────────────────┤
│  SalGraphicsImpl (后端抽象)                              │
│  ┌──────────┬──────────┬──────────┐                     │
│  │ Skia     │ Cairo    │ GDI/     │                     │
│  │ Impl     │ Impl     │ DirectFB │                     │
│  └──────────┴──────────┴──────────┘                     │
│  SkiaSalGraphicsImpl:                                    │
│    preDraw() → SkCanvas::drawRect/drawLine/drawText     │
│    → postDraw() → scheduleFlush()                       │
│    → [可选方案B] 在此层记录                              │
└─────────────────────────────────────────────────────────┘
```

### 2.3 关键发现

| 发现 | 说明 |
|------|------|
| **VCL 已有 GDIMetaFile 录制机制** | `OutputDevice` 的每个 Draw* 方法都会检查 `mpMetaFile`，如有则录制对应的 MetaAction。这是 VCL 内建的、成熟的命令录制/回放系统。 |
| **Skia 层无 SkPicture 录制** | LibreOffice 的 Skia 后端没有使用 `SkPictureRecorder` / `SkRecordingCanvas`，所有绘制直接打到 `SkSurface` 的 `SkCanvas` 上。 |
| **Skia 有 SAL_INFO tracing** | `vcl.skia.trace` 日志可以追踪每个绘制操作，但只是文本日志，不是结构化数据。 |
| **SwPaintEventListener 是 frame 语义级** | 它在 `PaintSwFrame` 中拦截，记录的是"哪个 Frame 被绘制"，而非"OutputDevice 发出了什么绘制命令"。 |

---

## 三、方案比较

### 方案 A：VCL OutputDevice 层拦截（推荐）

**原理**：在 `OutputDevice` 的 Draw* 方法中，在 MetaFile 录制的位置旁边，增加 `RenderInstructionSink` 输出。

**实现路径**：

```
OutputDevice::DrawRect(rRect)
  → if (mpMetaFile) mpMetaFile->AddAction(new MetaRectAction(rRect));   // 已有
  → if (mpRenderLog) mpRenderLog->LogRect(rRect);                       // 新增
  → ... 正常绘制流程
```

**需要修改的方法**：

| OutputDevice 方法 | 对应 RenderCmdType | 修改文件 |
|-------------------|-------------------|----------|
| `DrawRect()` | RECT | `vcl/source/outdev/rect.cxx` |
| `DrawLine()` | LINE | `vcl/source/outdev/line.cxx` |
| `DrawText()` | TEXT_RUN | `vcl/source/outdev/text.cxx` |
| `DrawBitmap()` / `DrawBitmapEx()` | IMAGE_FRAME | `vcl/source/outdev/bitmap.cxx` |
| `DrawPolygon()` / `DrawPolyPolygon()` | POLYGON (新增类型) | `vcl/source/outdev/polygon.cxx` |
| `DrawGradient()` | GRADIENT (新增类型) | `vcl/source/outdev/gradient.cxx` |
| `Push()` / `Pop()` | 状态栈操作 | `vcl/source/outdev/mapmod.cxx` |
| `SetClipRegion()` | CLIP_REGION | `vcl/source/outdev/clipregion.cxx` |
| `SetLineColor()` / `SetFillColor()` / `SetTextColor()` | 状态变更 | 各自文件 |

**核心设计**：

```cpp
// include/vcl/outdev.hxx 中 OutputDevice 新增成员
class OutputDevice {
    ...
    RenderInstructionSink* mpRenderLog = nullptr;  // 非拥有，外部管理生命周期
    ...
public:
    void SetRenderLog(RenderInstructionSink* pSink) { mpRenderLog = pSink; }
    RenderInstructionSink* GetRenderLog() const { return mpRenderLog; }
};

// vcl/source/outdev/rect.cxx
void OutputDevice::DrawRect(const tools::Rectangle& rRect, ...)
{
    if (mpMetaFile)
        mpMetaFile->AddAction(MetaRectAction(rRect));

    // ★ 新增：渲染指令记录
    if (mpRenderLog)
    {
        RenderInstruction inst;
        RenderInstruction_clear(&inst);
        inst.type = RenderCmdType::RECT;
        inst.x = rRect.Left();
        inst.y = rRect.Top();
        inst.width = rRect.getWidth();
        inst.height = rRect.getHeight();
        // fillColor, lineColor 等从当前状态获取
        mpRenderLog->OnInstruction(inst);
    }

    if (!IsDeviceOutputNecessary())
        return;
    // ... 原有逻辑
}
```

**优点**：
- ✅ **统一入口**：aproj 和 LibreOffice 共享同一个 `OutputDevice` 层（aproj 已经使用 LibreOffice 的 VCL），天然统一
- ✅ **完整覆盖**：所有绘制操作都经过 OutputDevice，不会遗漏
- ✅ **已有先例**：MetaFile 录制机制已证明此模式可行，代码改动模式清晰
- ✅ **无性能影响**：不设置 sink 时指针为 null，只有一个 if 判断
- ✅ **环境变量控制**：类似 `SW_RENDER_LOG`，通过环境变量决定是否启用

**缺点**：
- ⚠️ **低层级语义**：记录的是 DrawRect/DrawLine 级别，需要从坐标/属性反推"这是表格边框"还是"这是段落底纹"，语义信息不如 frame 层丰富
- ⚠️ **VCL 修改**：需要修改 LibreOffice 的 VCL 代码（跨模块），编译影响范围较大
- ⚠️ **状态依赖**：文字绘制的字体/颜色等信息在 `SetFont()`/`SetTextColor()` 中设置，需要维护状态上下文

### 方案 B：Skia 后端层拦截

**原理**：在 `SkiaSalGraphicsImpl` 的每个绘制方法中，将 Skia 调用转换为 `RenderInstruction` 输出。

**实现路径**：

```
SkiaSalGraphicsImpl::drawRect(nX, nY, nWidth, nHeight)
  → preDraw()
  → canvas->drawRect(SkRect::MakeXYWH(...), paint)   // 原有
  → if (mpRenderLog) LogRect(nX, nY, nWidth, nHeight) // 新增
  → postDraw()
```

**需要修改的 SkiaSalGraphicsImpl 方法**：

| 方法 | 行号 | 对应操作 |
|------|------|----------|
| `drawLine()` | gdiimpl.cxx:775 | LINE |
| `drawRect()` / `privateDrawAlphaRect()` | gdiimpl.cxx:848 | RECT |
| `drawGenericLayout()` | gdiimpl.cxx:2093 | TEXT |
| `drawBitmap()` / `drawAlphaBitmap()` | gdiimpl.cxx | BITMAP |
| `drawPolygon()` / `drawPolyPolygon()` | gdiimpl.cxx | POLYGON |

**优点**：
- ✅ **真正的绘制层**：记录的是实际提交给 GPU 的操作，精确到像素
- ✅ **Skia 统一**：Skia 后端在所有平台（Win/Linux/Mac）行为一致
- ✅ **不影响 VCL 接口**：不修改 OutputDevice 公共接口

**缺点**：
- ❌ **仅限 Skia 后端**：如果 LibreOffice 使用其他后端（GDI、Cairo），此方案不适用
- ❌ **坐标是设备像素**：经过 DPI 缩放和坐标变换后的值，与 frame 层的 twips 坐标不在同一坐标系，比对困难
- ❌ **信息丢失严重**：Skia 层只有 `drawRect(x,y,w,h)`，不知道这是"页面背景"还是"表格单元格边框"
- ❌ **aproj 不走 Skia**：aproj/docx 的渲染是自己遍历 Frame 树，不经过 VCL/Skia 管线，无法共享此层记录

### 方案 C：复用 GDIMetaFile（最轻量）

**原理**：不新增任何录制基础设施，直接利用 VCL 已有的 `GDIMetaFile` 录制机制。Writer 的 paint 流程结束时，将 MetaFile 中的 MetaAction 序列转换为 RenderInstruction 序列。

**实现路径**：

```cpp
// 在 SwRootFrame::PaintSwFrame 开始前
GDIMetaFile aMtf;
aMtf.Record(&rOutDev);  // 开始录制

// ... 原有 PaintSwFrame 流程 ...

aMtf.Stop();  // 停止录制
aMtf.WindStart();  // 回到起始

// 遍历 MetaAction，转换为 RenderInstruction
for (size_t i = 0; i < aMtf.GetActionSize(); ++i)
{
    const MetaAction* pAction = aMtf.GetAction(i);
    switch (pAction->GetType())
    {
        case META_RECT_ACTION:
        {
            auto* pRect = static_cast<const MetaRectAction*>(pAction);
            // → RenderCmdType::RECT
            break;
        }
        case META_LINE_ACTION:
        {
            auto* pLine = static_cast<const MetaLineAction*>(pAction);
            // → RenderCmdType::LINE
            break;
        }
        case META_TEXT_ACTION:
        {
            auto* pText = static_cast<const MetaTextAction*>(pAction);
            // → RenderCmdType::TEXT_RUN
            break;
        }
        // ... FONT, LINECOLOR, FILLCOLOR 等状态变更需要维护上下文
    }
}
```

**MetaAction 类型映射**（参考 `include/vcl/metaactiontypes.hxx`）：

| MetaAction 类型 | RenderCmdType | 说明 |
|----------------|---------------|------|
| `META_RECT_ACTION` | RECT | 矩形 |
| `META_LINE_ACTION` | LINE | 线段 |
| `META_TEXT_ACTION` / `META_TEXTARRAY_ACTION` | TEXT_RUN | 文本 |
| `META_BMP_ACTION` / `META_BMPEX_ACTION` | IMAGE_FRAME | 图片 |
| `META_POLYGON_ACTION` / `META_POLYPOLYGON_ACTION` | POLYGON (新增) | 多边形 |
| `META_GRADIENT_ACTION` | (新增或合并到 RECT) | 渐变 |
| `META_FONT_ACTION` | 状态上下文 | 设置当前字体 |
| `META_LINECOLOR_ACTION` | 状态上下文 | 设置线颜色 |
| `META_FILLCOLOR_ACTION` | 状态上下文 | 设置填充色 |
| `META_CLIPREGION_ACTION` | 状态上下文 | 设置裁剪区域 |
| `META_PUSH_ACTION` / `META_POP_ACTION` | 状态栈 | 保存/恢复状态 |

**优点**：
- ✅ **零侵入**：不需要修改 OutputDevice 或 Skia 后端任何代码
- ✅ **成熟机制**：GDIMetaFile 已经被广泛使用（WMF/EMF 导出、PDF 导出等），稳定可靠
- ✅ **完整覆盖**：MetaFile 录制了所有 OutputDevice 调用，包括 DrawBitmap、DrawGradient 等
- ✅ **实施快**：只需在 PaintSwFrame 前后加几行代码，再写一个 MetaAction → RenderInstruction 转换器

**缺点**：
- ⚠️ **仅限 LibreOffice**：GDIMetaFile 是 VCL 的机制，aproj/docx 不使用 VCL，无法共享
- ⚠️ **语义需要重建**：MetaAction 只记录了"画了一个矩形(100,200,50,30)"，需要从 FONT/FILLCOLOR 等状态变更中重建完整的渲染上下文
- ⚠️ **性能开销**：录制所有操作到 MetaFile 有额外内存和 CPU 开销（但仅在调试模式启用时生效）

---

## 四、推荐方案：A + C 双层校验

综合考虑，推荐**方案 A（VCL 层拦截）为主，方案 C（GDIMetaFile）为辅**的双层校验策略。

### 4.1 架构设计

```
┌──────────────────────────────────────────────────────────────┐
│  Layer 1: Frame 语义层（已有，保持不变）                       │
│  SwPaintEventListener / RenderLogger                          │
│  → 记录 PAGE_START/END, TEXT_FRAME, TABLE_FRAME, ...         │
│  → 语义丰富：知道"这是第2页的第3个段落"                        │
│  → 输出：render_log_frame.txt                                 │
├──────────────────────────────────────────────────────────────┤
│  Layer 2: VCL 绘制命令层（新增）                               │
│  OutputDevice::mpRenderLog                                    │
│  → 记录 RECT, LINE, TEXT_RUN, BITMAP, POLYGON, ...           │
│  → 精确完整：不遗漏任何绘制操作                                │
│  → 输出：render_log_vcl.txt                                   │
├──────────────────────────────────────────────────────────────┤
│  校验逻辑                                                     │
│  render_diff render_log_frame.txt render_log_vcl.txt          │
│  → Layer 1 的每条指令在 Layer 2 中应能找到对应绘制操作         │
│  → Layer 2 中未被 Layer 1 覆盖的部分 = 遗漏的渲染指令         │
└──────────────────────────────────────────────────────────────┘
```

### 4.2 实施步骤

#### Step 1: 扩展 render_instruction.h（共享层）

新增以下类型以覆盖 VCL 层的完整操作：

```cpp
enum class RenderCmdType : uint8_t
{
    // 已有
    PAGE_START = 1,
    PAGE_END = 2,
    TEXT_FRAME = 10,
    TEXT_LINE = 11,
    TEXT_RUN = 12,
    TABLE_FRAME = 20,
    TABLE_ROW = 21,
    TABLE_CELL = 22,
    IMAGE_FRAME = 30,
    SECTION_FRAME = 40,
    RECT = 50,
    LINE = 51,

    // 新增 — VCL 层绘制操作
    POLYGON = 60,        // DrawPolygon / DrawPolyPolygon
    BITMAP = 61,         // DrawBitmap / DrawBitmapEx
    GRADIENT = 62,       // DrawGradient
    ELLIPSE = 63,        // DrawEllipse
    ARC = 64,            // DrawArc
    POLYLINE = 65,       // DrawPolyLine

    // 新增 — 状态变更（用于重建绘制上下文）
    SET_FONT = 80,       // SetFont()
    SET_LINE_COLOR = 81, // SetLineColor()
    SET_FILL_COLOR = 82, // SetFillColor()
    SET_TEXT_COLOR = 83, // SetTextColor()
    SET_CLIP_REGION = 84,// SetClipRegion()
    PUSH = 85,           // Push() 状态保存
    POP = 86,            // Pop() 状态恢复
    SET_MAPMODE = 87,    // SetMapMode()
};
```

#### Step 2: OutputDevice 层植入（LibreOffice VCL）

**修改文件**：
| 文件 | 修改内容 |
|------|----------|
| `include/vcl/outdev.hxx` | 添加 `mpRenderLog` 成员和 Set/Get 方法 |
| `vcl/source/outdev/rect.cxx` | DrawRect 中记录 RECT |
| `vcl/source/outdev/line.cxx` | DrawLine 中记录 LINE |
| `vcl/source/outdev/text.cxx` | DrawText 中记录 TEXT_RUN（需要从当前字体状态获取属性） |
| `vcl/source/outdev/bitmap.cxx` | DrawBitmap 中记录 BITMAP |
| `vcl/source/outdev/polygon.cxx` | DrawPolygon 中记录 POLYGON |
| `vcl/source/outdev/gradient.cxx` | DrawGradient 中记录 GRADIENT |
| `vcl/source/outdev/font.cxx` | SetFont 中记录 SET_FONT 状态变更 |
| `vcl/source/outdev/linecolo.cxx` | SetLineColor 中记录 SET_LINE_COLOR |
| `vcl/source/outdev/fillcolor.cxx` | SetFillColor 中记录 SET_FILL_COLOR |
| `vcl/source/outdev/textcolo.cxx` | SetTextColor 中记录 SET_TEXT_COLOR |
| `vcl/source/outdev/clipregion.cxx` | SetClipRegion 中记录 SET_CLIP_REGION |
| `vcl/source/outdev/mapmod.cxx` | Push/Pop 中记录状态栈操作 |

**关键设计决策**：

1. **记录坐标系**：VCL 层的坐标是逻辑坐标（经过 MapMode 变换），需要记录 MapMode 状态以便后续转换。
2. **文本属性**：DrawText 时字体/颜色等属性已通过 SetFont/SetTextColor 设置，需要在 RenderInstruction 中携带当前状态快照，或通过 SET_FONT/SET_TEXT_COLOR 状态指令让消费方自行重建。
3. **环境变量控制**：通过 `VCL_RENDER_LOG` 环境变量控制，与 `SW_RENDER_LOG` 独立。

#### Step 3: VCL 层 RenderInstructionSink 实现

```cpp
// vcl/source/outdev/vcl_render_log.hxx
class VclRenderLogger : public RenderInstructionSink
{
public:
    static VclRenderLogger& Get();

    void CheckEnvAndStart();  // 检查 VCL_RENDER_LOG 环境变量
    void StartLog(const OString& filePath);
    void EndLog();
    bool IsLogging() const;

    void OnInstruction(const RenderInstruction& inst) override;

    // 状态上下文 — 跟踪当前 SetFont/SetFillColor 等状态
    void SetCurrentFont(const vcl::Font& rFont);
    void SetCurrentLineColor(const Color& rColor);
    void SetCurrentFillColor(const Color& rColor);
    void SetCurrentTextColor(const Color& rColor);

private:
    // 当前绘制状态（用于填充 RenderInstruction 的属性字段）
    vcl::Font m_aCurrentFont;
    Color m_aCurrentLineColor;
    Color m_aCurrentFillColor;
    Color m_aCurrentTextColor;
    // ...
};
```

#### Step 4: aproj 侧适配

aproj/docx 的渲染流程不经过 VCL OutputDevice，因此无法直接使用方案 A。有两个选择：

**选择 1（推荐）**：让 aproj 的 `RenderLogger::LogFrameTree()` 生成 frame 层日志，作为与 LibreOffice frame 层日志比对的基准。VCL 层日志仅由 LibreOffice 生成，用于发现 frame 层的遗漏。

**选择 2**：如果 aproj 未来要使用 VCL 进行实际渲染，则可以在那时接入 VCL 层记录，实现完全统一。

#### Step 5: 比对工具增强

增强 `render_diff.cpp`，支持：
- **跨层比对**：frame 层日志 vs VCL 层日志
- **指令匹配**：frame 层的 TEXT_FRAME 应能匹配到 VCL 层的 SET_FONT + TEXT_RUN 序列
- **遗漏检测**：VCL 层有但 frame 层没有的指令 = 遗漏

```
render_diff --mode=cross-layer frame_log.txt vcl_log.txt
→ 输出：
  [MATCH] PAGE_START(1) ↔ PUSH + SET_MAPMODE
  [MATCH] TEXT_FRAME(1,100,200,"Hello") ↔ SET_FONT + SET_TEXT_COLOR + TEXT_RUN(1,100,200,"Hello")
  [MISSING] RECT(1,50,50,400,20) — VCL 层绘制了矩形，但 frame 层未记录（可能是表格边框）
```

---

## 五、约束与注意事项

### 5.1 aproj 约束

> aproj 不再自己实现渲染层，如果有必要也只是包装 API 接口，和 libo-core 使用同一套渲染接口。

当前 aproj 的 `RenderLogger` 使用自己的 Frame 树遍历逻辑。未来应逐步迁移到与 LibreOffice 共享的渲染路径，而非维护独立的遍历逻辑。

### 5.2 性能影响

- **生产环境**：mpRenderLog 指针为 null，仅增加一个 `if` 分支判断，性能影响可忽略
- **调试环境**：通过环境变量启用，录制所有操作有额外内存和 I/O 开销，仅用于测试

### 5.3 坐标系统一

- Frame 层使用 **twips**（1 inch = 1440 twips）
- VCL 层使用 **逻辑坐标**（受 MapMode 影响）
- Skia 层使用 **设备像素**（受 DPI 缩放影响）

VCL 层记录时应输出逻辑坐标（与 frame 层一致），避免坐标系混乱。如果需要设备像素信息，可通过 `LogicToPixel()` 转换后附加。

### 5.4 与 GDIMetaFile 的关系

GDIMetaFile 和 mpRenderLog 是两个独立的录制通道：
- **GDIMetaFile**：用于 WMF/EMF 导出、PDF 导出等生产功能
- **mpRenderLog**：用于渲染指令比对和调试

两者可以同时启用，互不干扰。但 mpRenderLog 输出的是结构化的 `RenderInstruction`（与 frame 层共享格式），而 GDIMetaFile 输出的是 VCL 内部的 `MetaAction` 序列。

---

## 六、风险评估

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| VCL 修改影响范围大 | 所有使用 OutputDevice 的模块都可能受影响 | mpRenderLog 默认为 null，不影响现有逻辑 |
| 文本属性需要维护状态上下文 | SetFont/SetTextColor 与 DrawText 分离 | VclRenderLogger 维护当前状态快照 |
| aproj 不走 VCL 管线 | VCL 层记录对 aproj 无直接帮助 | aproj 使用 frame 层记录作为基准，VCL 层仅用于 LibreOffice 侧的完整性校验 |
| 输出格式需要扩展 | 新增指令类型需要更新 TSV 格式 | render_instruction.h 已预留扩展空间 |

---

## 七、总结

| 维度 | 方案 A (VCL 层) | 方案 B (Skia 层) | 方案 C (GDIMetaFile) |
|------|----------------|-----------------|---------------------|
| 统一性 | ✅ aproj/LibreOffice 共享 | ❌ 仅 Skia 后端 | ❌ 仅 LibreOffice |
| 完整性 | ✅ 覆盖所有绘制操作 | ✅ 覆盖所有绘制操作 | ✅ 覆盖所有绘制操作 |
| 侵入性 | ⚠️ 需修改 VCL | ⚠️ 需修改 Skia 后端 | ✅ 零侵入 |
| 语义信息 | ⚠️ 低层级，需重建 | ❌ 最低层级 | ⚠️ 低层级，需重建 |
| 坐标系 | ✅ 逻辑坐标 | ❌ 设备像素 | ✅ 逻辑坐标 |
| 实施难度 | 中 | 高 | 低 |

**最终推荐**：先实施方案 C（GDIMetaFile 转换）快速验证可行性，再实施方案 A（VCL 层拦截）作为长期方案。两者结合形成完整的双层校验体系。
