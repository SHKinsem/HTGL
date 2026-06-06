# HTGL — 设计文档（里程碑 1）

> **HTGL** = HTML + Graphics Library。一个"模仿 LVGL"的嵌入式 UI 框架，
> 但用 **HTML/CSS 子集**当界面描述语言，转译成紧凑二进制喂给一个可移植的纯 C 渲染引擎。
> 目标：开源（MIT），让任何人都能在低端 MCU 上用熟悉的 Web 语法写界面。

- 日期：2026-06-04
- 状态：设计已确认，待写实现计划
- 许可证：MIT

---

## 1. 愿景与定位

LVGL 是优秀的嵌入式 GUI 库，但界面用 C API 一行行搭，门槛高、不可视化。
HTGL 想保留 LVGL 那套被资源约束逼出来的成熟架构（分块渲染、HAL 解耦），
把**前端语言换成人人会写、还能丢进浏览器预览**的 HTML/CSS 子集。

**核心命题：** 界面用 `.html` 写 → PC 端转译成二进制 `.uib` → 设备端一个极小引擎渲染。
设备端**永远不解析 HTML 文本**，只读编译好的二进制，因此能塞进 STM32F1 这种几十 KB RAM 的芯片。

非目标（明确不做）：在设备上跑真浏览器/HTML 文本解析器；支持完整 CSS；竞争桌面 Web 引擎。

---

## 2. 关键架构决策

| 维度 | 决策 | 理由 |
|---|---|---|
| 界面语言 | HTML/CSS **子集** | 生态广、可浏览器预览、零学习成本 |
| 转译时机 | **编译期 + 运行期共用一种二进制格式** | 一份 `.uib`：编译期 `#include` 成 C 数组（零开销）；运行期从 SD/OTA 读（改界面不重烧） |
| 渲染后端 | **自建轻量引擎**（非驱动 LVGL） | 完全掌控、贴近 CSS 语义 |
| 引擎语言 | **C99**，默认无动态内存 | 嵌入式通用、可移植到任意 MCU |
| 转译器语言 | **Python 3** | 跨平台、零编译、贡献门槛最低 |
| 平台解耦 | **HAL 回调**（刷屏 + 计时） | PC 模拟器与 MCU 只是两套 HAL，引擎代码不变 |
| 许可证 | **MIT** | 对标 LVGL，商用/嵌入式零顾虑 |
| 首发硬件 | 低端 MCU（STM32F1 类，几十 KB RAM、无 FPU） | 最严约束 = 最能逼出正确架构 |

### 必须遵守的硬约束（来自 STM32F1）
1. **禁止整屏帧缓冲**（240×320×16bpp=150KB 装不下）→ 必须小行缓冲 + 脏矩形 + 分块刷新。
2. **设备端无文本解析**（HTML 解析器太重）→ 只读二进制 `.uib`。
3. **无 FPU** → 后续布局/动画用定点数；MVP 全整数 px，暂不引入定点。

---

## 3. 里程碑 1 范围（YAGNI 锁死）

**做到的最小竖切：** `hello.html → htgl.py → hello.uib → 模拟器 → hello.png`，效果与 Chrome 预览基本一致。

### 3.1 支持的 HTML/CSS 子集
- 标签：`<div>`（容器/盒子），`<div>` 内的纯文本节点（文字）。
- CSS（`style="..."` 内联）：
  - `position: absolute`、`left`、`top`、`width`、`height`（**纯 px 整数**）
  - `background-color`（#rgb / #rrggbb / 具名基础色）
  - `color`、`font-size`（从预置位图字号里就近选）
- 坐标：子节点相对父节点累加成绝对屏幕坐标。

### 3.2 明确不在里程碑 1
flex/流式布局、百分比/em/rem、动画/过渡、图片、按钮/事件、滚动、圆角/边框/阴影、多字体。
→ 但**节点格式与引擎按可扩展设计**，加这些时不需推翻骨架。

### 3.3 "完成"标准（验收）
1. 手写 `examples/hello.html`（2~3 个彩色 div + 文字）。
2. `python tool/htgl.py examples/hello.html -o build/hello.uib --emit-c build/hello_ui.c`。
3. `port/sim` 加载 blob，渲染输出 `build/hello.png`。
4. 该 PNG 与黄金 PNG 逐像素一致；与 Chrome 打开同文件的效果肉眼基本一致。

---

## 4. 二进制格式 `.uib`（v1）

扁平、小端、零拷贝可解析。编译期与运行期共用同一份字节。

```
Header (16 B)
  magic[4]      = "HTGL"
  version u8    = 1
  flags  u8     = 0
  node_count u16
  screen_w  u16
  screen_h  u16
  strtab_off u16   // 距文件头偏移
  reserved   u16

Node (16 B, 紧凑 struct，数组)
  type    u8    // 0=SCREEN(根) 1=BOX 2=TEXT  (枚举，可扩展)
  font    u8    // 字体 id（MVP 仅 0）
  parent  u16   // 父节点下标，根=0xFFFF
  x,y     i16   // 相对父节点，px
  w,h     i16   // px
  bg      u16   // RGB565；TEXT 节点忽略
  fg      u16   // RGB565；文字色
  text_ref u16  // 入 StrTab 的偏移；无文字=0xFFFF

StrTab
  连续若干：len u8 + ASCII 字节（MVP 不做 UTF-8，留版本位升级）
```

- **编译期模式**：同样的字节 emit 成 `const uint8_t hello_ui_blob[] = {…};`，`#include` 即用。
- **运行期模式**：引擎用**同一个 `ui_load()`** 解析文件/SD 里的同样字节。
- 格式靠 `version` 字段演进；解析器拒绝未知大版本。

---

## 5. 引擎 API 与流水线（纯 C99）

### 5.1 公共 API（`engine/htgl.h`）
```c
typedef struct htgl_ctx htgl_ctx;

// HAL：平台相关，仅此两类需各平台实现
typedef struct {
    // 把 area(x,y,w,h) 区域的 buf(RGB565) 推到显示设备
    void (*flush)(int x, int y, int w, int h, const uint16_t *buf);
    uint32_t (*tick_ms)(void);          // 单调毫秒，MVP 可选
} htgl_hal;

htgl_ctx *htgl_init(const htgl_hal *hal, uint16_t *line_buf, int line_buf_px);
int  htgl_load(htgl_ctx *c, const uint8_t *blob, int len); // 校验头、建只读视图
void htgl_layout(htgl_ctx *c);          // 解析相对→绝对坐标
void htgl_render(htgl_ctx *c);          // 分块渲染 + 调 hal->flush
```

### 5.2 渲染流水线（对标 LVGL 分块）
1. `htgl_load`：校验 magic/version，把 Node 数组当**只读视图**指向 blob（零拷贝），StrTab 同理。
2. `htgl_layout`：自根 DFS，子绝对坐标 = 父绝对坐标 + 子相对坐标，写入运行时小数组。
3. `htgl_render`：**按行带分块**——
   - 把屏幕切成若干 `line_buf_px` 行的水平带；
   - 每带：清背景 → 遍历节点，与本带相交的画矩形/字形进 `line_buf`；
   - `hal->flush(x,y,w,h, line_buf)` 推这一带；
   - 全程只占一个小行缓冲（调用方传入），**永不开整屏帧缓冲**。
4. 字体：MVP 内嵌一个等宽 ASCII 位图字体（build 时由 TTF 生成成 C 数组 `engine/font_default.c`）。

### 5.3 可扩展点（为开源协作预留）
- **节点类型表**：`type` → 绘制处理函数指针表；社区加控件 = 注册一项。
- **CSS 属性表**：转译器侧 `属性名 → 解析/落盘` 映射表，加属性不动主流程。
- **HAL**：加新显示屏/平台 = 实现 `flush`。

> **实现现状（截至 2026-06-06）**：上面的"函数指针注册表"是早期设想；当前 ~2300 行
> 规模下刻意用 **switch + 白名单**（3 种节点类型不值得引入注册机）。贡献者实际改动点：
> - **加节点类型**：`engine/htgl.c` `htgl_render` 的 switch + 加载期类型校验(`-14`) +
>   `tool/htgl/html_tree.py` 标签分发 + `tool/htgl/uib.py` 打包。
> - **加可动画属性**：`engine/htgl.c` `htgl_tick` 的 switch + `uib.py._ANIM_PROP` +
>   `html_tree.py._ANIM_PROPS` + `cssanim.py._CSS_PROP_MAP`（漏改任一处，加载期 `-15` 会报错而非静默）。
> - **加 CSS 属性**：`css.py` 白名单 + `html_tree.py` 落到 Node 字段（可能触发格式/版本升级）。
> - **HAL** 仍是唯一真正干净的接缝：只实现一个 `flush(x,y,w,h,rgb565)`，引擎不变。
> 当类型/属性数量增长到值得时，再引入真正的注册表。完整贡献者指南见 `docs/USAGE.md`。

---

## 6. 转译器（`tool/htgl.py`，Python 3）

流程：`HTML 文本 → 解析(html.parser) → 节点树 → 解析内联 CSS → IR → 写 .uib(+ 可选 .c)`。

- 用标准库 `html.parser`，无第三方依赖（贡献/安装零门槛）。
- CSS：手写极小子集解析（分号/冒号切分 + 属性白名单表），白名单即 §3.1。
- 颜色：`#rgb`/`#rrggbb`/具名 → RGB565。
- 校验：未知标签/属性 → 警告并跳过（前向兼容，浏览器也宽容）。
- 输出：`-o x.uib`（二进制）、`--emit-c x.c`（C 数组，编译期模式）。

---

## 7. 模拟器后端（`port/sim`）

- **先做"渲染到内存帧缓冲 → 写 PNG"**：Windows 上零依赖，确定性，**同时充当测试预言机**。
  - PNG 编码用极小自带实现或单文件 `stb_image_write.h`（公有领域，符合开源洁净依赖）。
- `flush` 回调把每个带 blit 进一张完整内存图，最后整张存 PNG。
- 实时开窗（SDL/Win32）作为**可选增强**，里程碑 1 之后加，不阻塞主链路。

---

## 8. 验证策略

| 层 | 方法 |
|---|---|
| 转译器 | 样例 `.html` → 比对黄金 `.uib` 字节 |
| 引擎 | 渲染到内存帧缓冲 → 输出 PNG → 与黄金 PNG 逐像素比对（无需开窗，CI 友好） |
| 端到端 | `hello.html` 全链路跑出 PNG，匹配黄金图 |
| 肉眼 | 同一 `.html` 丢 Chrome 并排对照 |

黄金文件存 `tests/`。CI（后续）：Python 跑转译 + 编译引擎 + 跑 sim + 比对。

---

## 9. 仓库结构

```
htgl/
  LICENSE              MIT
  README.md
  tool/
    htgl.py            转译器 (html/css 子集 → .uib + .c)
  engine/
    htgl.h  htgl.c     公共 API + 加载/布局/渲染
    draw.c             矩形/字形光栅化
    font_default.c     内嵌位图字体
  port/
    sim/
      main.c           加载 blob → 渲染 → 写 PNG
      hal_png.c        flush → 内存图 → PNG
    stm32/             (里程碑 2)
  examples/
    hello.html
  tests/
    hello.uib.golden
    hello.png.golden
  docs/superpowers/specs/2026-06-04-htgl-design.md
```

---

## 10. 路线图（里程碑 1 之后，仅占位）

- **M2**：把同一引擎接 STM32F1 + ILI9341（新增 `port/stm32` 的 `flush`），真机点亮 `hello`。
- **M3**：图片、按钮 + 触摸事件（输入子系统 + 事件分发）。
- **M4**：布局升级（flex/百分比，引入定点数）、动画。
- **M5**：运行期从 SD/OTA 加载 `.uib`；多字体/UTF-8；可视化/浏览器实时预览工具。

每个里程碑各自走 spec → plan → 实现 循环。

---

## 11. 开放问题（实现期再定，不阻塞）
- PNG 依赖：自带极小编码器 vs `stb_image_write.h`（倾向后者，单文件公有领域）。
- 字体生成工具：用现成 TTF→C 工具 vs 自写小脚本（倾向自写，纳入 `tool/`，保持依赖洁净）。
- `.uib` 是否预留对齐/校验和字段以便运行期从不可信介质加载（倾向 v1 先加一个 CRC 占位）。
  → **已解决（2026-06-06）**：加了可选 CRC32 尾部（header `flags` bit 0；转译器 `--crc` 开启；
  引擎加载期先于一切校验，失配返回 `-16`），向后兼容（默认关闭，字节与旧格式一致）。
