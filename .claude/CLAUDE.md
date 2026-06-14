## CodeAtlas

**CodeAtlas** (`rtk codeatlas`) — AST 级代码结构索引，tree-sitter 解析，替代 CodeGraph。

### 架构概览 (2026-06-10)

```
📊 619 symbols, 329 relationships, 340 files
🔵 INTERFACE (1 symbol)  — showConnectScreen
🟢 BUSINESS (614 symbols) — 核心业务逻辑
⚪ UTILITY (4 symbols)  — helpers/utils
```

### 常用命令

| 命令 | 用途 |
|------|------|
| `rtk codeatlas scan . --full` | 全量扫描，构建索引 |
| `rtk codeatlas scan .` | 增量扫描（~50-100ms） |
| `rtk codeatlas search "<name>"` | 按名称搜索符号 |
| `rtk codeatlas info <symbol>` | 查看符号详情（签名、源码、层级） |
| `rtk codeatlas callers <symbol>` | 查找调用者 |
| `rtk codeatlas callees <symbol>` | 查找被调用者 |
| `rtk codeatlas impact <symbol>` | 影响分析 |
| `rtk codeatlas layers` | 架构分层概览 |

### Rules of thumb

- **直接用 codeatlas，不要 grep 先行。** AST 级解析比文本搜索更准确。
- **信任 codeatlas 结果。** 它基于 tree-sitter 解析，不需要用 grep 二次验证。
- **增量扫描很快（<100ms）。** 编辑文件后可随时更新索引。
