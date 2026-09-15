# 文件上传模块自动化测试全程规划

> 文档版本：0.4  
> 编制日期：2026-09-15  
> 最近更新：2026-09-15（阶段 5 收口：修正 C 单元测试输入通路，阶段 4 统计修正为 4 OK / 11 NG，新增 4 条 strncpy 家族用例）  
> 当前分支：`feat/cgi-upload-tests`  
> 测试对象：前端上传服务、`buffer_search()`、`recv_save_file()` 及后续上传接口  
> 测试目录：`picture_bed/src/services/`、`picture_bed/src/test/helpers/`、`tests/backend/unit/`

## 1. 文档目的

本文档用于记录并指导文件上传模块从需求分析到最终测试报告的完整自动化测试实践。它既说明此前测试文件为什么编写、如何组织、覆盖哪些需求，也统一后续测试的用例编号、优先级、实现顺序、判定标准和交付规则。

本文档只描述当前团队接下来要执行的工作。协作者文档中记录的历史结果和缺陷不能直接作为本地测试结论；必须在当前分支、当前测试程序和可记录的环境中重新执行后，才能写入正式缺陷清单和测试报告。

## 2. 规划依据

| 文档或代码 | 本规划中的作用 |
|---|---|
| `tests/UPLOAD_REQUIREMENT_RISK_CASE_MATRIX.md` | 需求、风险、业务用例编号和优先级的主要依据 |
| `tests/UPLOAD_CGI_UNIT_TEST_PLAN(1).md` | 异常输入类型、隔离执行方法和服务器测试思路的参考资料 |
| `picture_bed/src/services/images.upload.test.js` | 前端普通上传、秒传、重复文件和认证异常测试基线 |
| `picture_bed/src/services/images.chunk.test.js` | 前端分片路由、分片流程、断点续传和失败中止测试基线 |
| `tests/backend/unit/test_buffer_search.c` | 后端缓冲区搜索测试基线 |
| `tests/backend/unit/test_recv_save_file.c` | 当前已实现用例及实际测试入口的基线 |
| `tests/backend/unit/multipart_fixture.c` | 当前合法 multipart 报文构造能力的基线 |
| `docs/附录1：文件上传模块测试用例清单.xlsx` | 当前执行结果和后续状态更新的交付载体 |

与协作者方案的主要差异：本规划使用 `UT-MP-*` 编号，采用“小阶段提交”的方式推进；先在本地 Docker 中完成无数据库依赖的函数级测试，再进入 CGI/API/数据层测试。

## 3. 全流程路线图

| 阶段 | 主要工作 | 状态 | 主要产出 |
|---|---|---|---|
| 阶段 0 | 建立需求—风险—用例矩阵，确定上传模块范围 | 已完成 | `UPLOAD_REQUIREMENT_RISK_CASE_MATRIX.md` |
| 阶段 1 | 编写前端上传逻辑 Jest 单元测试 | 已完成 | 2 个测试文件、3 个 helper、8 条测试 |
| 阶段 2 | 建立 C 测试基础设施并测试 `buffer_search()` | 已完成 | `mini_test.h`、Makefile、6 条测试 |
| 阶段 3 | 构造合法 multipart 报文并测试正常解析 | 已完成 | fixture、`test_recv_save_file.c`、5 条测试 |
| 阶段 4 | 测试畸形 multipart、长度边界和路径安全 | 已完成（批次 A/B/C 全部完成） | UT-MP-006～UT-MP-020、5 个缺陷 |
| 阶段 5 | 使用 Sanitizer 执行内存安全检查并回归 | **已完成**（发现并修复测试夹具输入通路缺陷，新增 4 条用例） | Sanitizer 逐用例报告、DEF-MP-006/007 |
| 阶段 6 | Docker 中执行 CGI/API 和数据一致性测试 | 环境就绪后实施 | 接口脚本、数据库及文件断言 |
| 阶段 7 | 汇总质量度量并完成课程交付文档 | 持续更新 | 用例清单、缺陷报告、测试报告 |

## 4. 阶段 0：需求、风险与用例设计

首先选择文件上传模块作为实践对象，分析普通上传、MD5 检测、秒传、分片上传、断点续传、认证、参数安全和数据一致性等需求，形成：

- 16 项需求 R-01～R-16；
- 15 项风险 RK-01～RK-15；
- 业务测试用例 TC-N、TC-B、TC-M、TC-C、TC-S、TC-D、TC-F、TC-P；
- 需求、风险、测试用例和自动化层之间的追踪关系。

后续所有测试代码都应能够回溯到该矩阵；新增场景不能只写技术编号，还应在注释或清单中标明对应的业务用例或风险。

## 5. 阶段 1：前端上传逻辑单元测试

### 5.1 文件组织

```text
picture_bed/src/
├── services/
│   ├── images.js
│   ├── images.upload.test.js
│   └── images.chunk.test.js
└── test/helpers/
    ├── mockFetch.js
    ├── mockFile.js
    └── testUser.js
```

测试文件与被测服务放在同一目录，便于定位；可复用的请求模拟、文件生成和用户数据放入 `src/test/helpers/`，避免在两个测试文件中重复实现。

### 5.2 已编写测试

| 测试文件 | 用例 | 主要断言 |
|---|---|---|
| `images.upload.test.js` | TC-M-001 | MD5 未命中时先调用 `/api/md5`，再上传文件实体 |
| `images.upload.test.js` | TC-M-003 | 秒传命中后不再调用普通上传接口 |
| `images.upload.test.js` | TC-M-005 | 同一用户重复文件返回已存在结果，不重复上传 |
| `images.upload.test.js` | TC-S-002 | token 失效被转换为 `tokenExpired` 错误 |
| `images.chunk.test.js` | TC-B-002 | 文件恰好 10 MB 时走普通上传路径 |
| `images.chunk.test.js` | TC-B-003/TC-C-001 | 25 MB 文件执行 init、3 次分片上传和 merge |
| `images.chunk.test.js` | TC-C-004 | 根据初始化结果跳过已经上传的分片 |
| `images.chunk.test.js` | TC-C-009 | 任一分片失败后停止后续分片和 merge |

执行命令：

```powershell
cd picture_bed
npm test -- --watchAll=false --runInBand --testPathPattern="images.(upload|chunk).test.js"
```

当前执行基线为 2 个测试套件、8 条测试全部通过。对应历史提交为 `a015d55 test: add upload requirement matrix and frontend unit tests`。

## 6. 阶段 2：C 测试基础设施与 buffer_search 测试

### 6.1 测试基础设施

在 `tests/backend/unit/` 中建立：

- `mini_test.h`：零依赖断言、用例统计和子进程隔离；
- `Makefile`：将真实 `upload_cgi.c` 的 `main` 重命名后参与测试编译，并为阶段 5 预置独立的 Sanitizer 构建目标；
- `test_buffer_search.c`：直接调用真实 `buffer_search()`；
- `multipart_fixture.h/.c`：后续 multipart 测试共用的报文与文件工具。

该方式不复制被测函数实现，避免测试通过但真实代码未被覆盖。对应历史提交为 `f584a48 test: add buffer search C unit tests`。

### 6.2 已编写测试

| 用例ID | 场景 | 主要验证点 | 状态 |
|---|---|---|---|
| UT-BS-001 | 分隔符位于缓冲区开头 | 返回首地址 | OK |
| UT-BS-002 | 分隔符位于缓冲区中间 | 返回正确偏移 | OK |
| UT-BS-003 | 分隔符恰好位于末尾 | 末端匹配且不越界 | OK |
| UT-BS-004 | 分隔符不存在 | 返回 `NULL` | OK |
| UT-BS-005 | 缓冲区短于分隔符 | 安全返回 `NULL` | OK |
| UT-BS-006 | 二进制数据包含零字节 | 按显式长度搜索，不被 `\0` 截断 | OK |

## 7. 阶段 3：multipart 正常解析测试

### 7.1 测试文件编写

`multipart_fixture.h/.c` 负责动态生成合法的 `file -> user -> md5 -> size` multipart 报文、重定向标准输入和逐字节比较文件。`test_recv_save_file.c` 在临时目录和独立子进程中调用真实 `recv_save_file()`，检查：

- 函数返回值；
- 解析得到的 user、filename、md5 和 size；
- 文件是否生成以及内容是否一致；
- 测试结束后是否完成清理。

### 7.2 已编写测试

| 用例ID | 场景 | 关联矩阵 | 状态 |
|---|---|---|---|
| UT-MP-001 | 正常文本报文解析与落盘 | TC-N-001、RK-04 | OK |
| UT-MP-002 | 二进制内容包含零字节 | TC-N-002、RK-02 | OK |
| UT-MP-003 | 中文和空格文件名 | TC-N-003、RK-10 | OK |
| UT-MP-004 | 0 字节文件当前行为 | TC-B-001、R-03 | POK，需求待确认 |
| UT-MP-005 | 声明大小与实际内容一致 | TC-S-005、RK-02 | OK |

测试程序的执行层面为 5 条全部完成；质量判定为 4 条 OK、1 条 POK。对应历史提交为 `99341ab test: add valid multipart parser unit tests`。

当前尚不具备通用的畸形报文构造能力。下一阶段开始前，需要扩展 fixture，使测试能够删除字段、改变字段顺序、截断报文以及写入超长或恶意字段值。

## 8. 后续总体测试策略

测试按以下顺序推进：

1. 函数级异常解析测试：验证畸形输入不会导致崩溃、越界访问或错误落盘；
2. 内存安全测试：使用 Sanitizer 检查普通断言无法发现的内存问题；
3. 需求确认与回归：明确零字节文件规则，固定 `UT-MP-004` 的唯一预期；
4. CGI/API 集成测试：验证真实请求、响应契约和服务存活性；
5. 数据一致性测试：数据库配置完成后验证落库、回滚和存储一致性；
6. 文档收口：更新用例清单、缺陷报告和测试报告。

异常用例统一采用 fail-closed 判定：输入不合法时允许函数返回失败，但不允许进程崩溃、写出工作目录、保留不完整文件或发生未定义行为。

## 9. 阶段 4：multipart 异常解析单元测试

### 9.1 计划用例

| 用例ID | 场景 | 关联需求/风险 | 优先级 | 最小验收标准 |
|---|---|---|---|---|
| UT-MP-006 | 缺少起始 boundary | R-12、RK-04 | P0 | 返回失败；不崩溃；无输出文件 |
| UT-MP-007 | 缺少结束 boundary | R-12、RK-04 | P0 | 返回失败；不保留不完整文件 |
| UT-MP-008 | 文件段缺少 `Content-Disposition` | R-12、RK-04 | P0 | 返回失败；不发生空指针访问 |
| UT-MP-009 | 文件段缺少 `filename` | TC-N-004、RK-10 | P0 | 返回失败；不创建文件；不崩溃 |
| UT-MP-010 | 缺少 `user` 字段 | R-11、RK-09 | P0 | 返回失败；无落盘残留 |
| UT-MP-011 | 缺少 `md5` 字段 | R-12、RK-02 | P0 | 返回失败；无落盘残留 |
| UT-MP-012 | 缺少 `size` 字段 | TC-S-005、RK-02 | P0 | 返回失败；无落盘残留 |
| UT-MP-013 | 声明大小小于实际内容 | TC-S-005、RK-02 | P0 | 拒绝不一致报文；不得静默截断后成功 |
| UT-MP-014 | 声明大小大于实际内容 | TC-S-005、RK-02 | P0 | 返回失败；不越界读取或写入 |
| UT-MP-015 | `size` 为负数、非数字或溢出值 | R-12、RK-10 | P0 | 参数校验失败；无整数溢出副作用 |
| UT-MP-016 | 超长文件名 | TC-N-004、RK-10 | P0 | 安全拒绝；无栈/堆越界 |
| UT-MP-017 | 文件名包含 `../` 或路径分隔符 | TC-S-006、RK-10 | P0 | 不得写出临时测试目录 |
| UT-MP-018 | 文件内容中包含 boundary 相似字节串 | R-01、RK-04 | P1 | 文件内容不被错误截断 |
| UT-MP-019 | `user/md5/size` 字段顺序变化 | R-01、RK-04 | P1 | 按接口契约明确接受或拒绝；不得误解析 |
| UT-MP-020 | 报文在不同位置被截断 | TC-N-004、RK-10 | P0 | 全部安全失败；无崩溃和残留 |

### 9.2 实现分批

为保持 Git 历史清晰，阶段 4 拆成三个可独立验证的批次，**三个批次均已完成**：

#### 批次 A：结构缺失与截断 —— 已完成

- 扩展 `multipart_fixture.h/.c`，增加原始字节报文写入或可选字段构造能力；
- 实现 UT-MP-006～UT-MP-012、UT-MP-020；
- 所有用例使用子进程隔离；父进程同时检查正常退出、返回值和文件残留。

建议提交信息：`test: add malformed multipart structure cases`

#### 批次 B：长度和内存边界 —— 已完成

- 实现 UT-MP-013～UT-MP-016；
- 增加临界长度和超长输入数据生成器（`multipart_body_options.size_text_override` 支持负值、非数字和溢出文本）；
- 在普通构建通过后，用 AddressSanitizer 和 UndefinedBehaviorSanitizer 再执行一次。

建议提交信息：`test: cover multipart length and memory boundaries`

#### 批次 C：路径、内容和字段契约 —— 已完成

- 实现 UT-MP-017～UT-MP-019；
- 对路径穿越用例检查测试沙箱外是否出现文件；
- 对 boundary 内容用例执行逐字节比较；
- 对字段乱序用例记录当前接口契约，不把需求不明确直接判为代码缺陷。

建议提交信息：`test: cover multipart path and contract risks`

### 9.3 批次 A 执行结果（2026-09-15，阶段内中间结果）

> 本节保留批次 A 完成时的中间结果。阶段 4 的最终结论以第 9.4 节为准。

执行环境：本地 Docker 容器 `tc_fcgi_app`，目录 `/app/tests/backend/unit`。执行 `make clean && make test` 后，`buffer_search` 的 6 条既有测试和 multipart 的 5 条正常路径基线均未发生回归。

| 用例ID | 实际结果 | 状态 | 缺陷候选 |
|---|---|---|---|
| UT-MP-006 | 缺少起始 boundary 时子进程收到 SIGSEGV | NG | DEF-MP-001 |
| UT-MP-007 | 缺少结束 boundary 时函数未拒绝并生成 `bad.bin` | NG | DEF-MP-002 |
| UT-MP-008 | 缺少文件段 `Content-Disposition` 时 SIGSEGV | NG | DEF-MP-001 |
| UT-MP-009 | 缺少 `filename` 时 SIGSEGV | NG | DEF-MP-001 |
| UT-MP-010 | 缺少 `user` 时 SIGSEGV | NG | DEF-MP-001 |
| UT-MP-011 | 缺少 `md5` 时 SIGSEGV | NG | DEF-MP-001 |
| UT-MP-012 | 缺少 `size` 时返回失败且无残留 | OK | — |
| UT-MP-020 | 开头截断可安全拒绝；文件内容处及 size 值处截断均 SIGSEGV | NG | DEF-MP-001 |

测试程序输出为 7 个隔离断言 PASS、8 个隔离断言 FAIL；由于 UT-MP-020 包含 3 个独立截断点，该断言数不等于业务用例数。按业务用例统计，本批次为 1 OK、7 NG。

执行证据保存于 `tests/backend/unit/results/multipart_batch_a_20260915.log`。上述结果是当前分支在本地容器中的实际复现，不直接沿用协作者文档中的历史结论。

### 9.4 阶段 4 完整执行结果（2026-09-15）

> 完整证据：`tests/backend/unit/results/multipart_stage4_20260915.log`
> 原始控制台输出：`tests/backend/unit/results/raw_plain_make_test_20260915.log`

执行环境：容器 `tc_fcgi_app`（Ubuntu 20.04.6 + gcc 9.4.0），目录 `/app/tests/backend/unit`。
执行命令：`docker exec tc_fcgi_app sh -lc 'cd /app/tests/backend/unit && make clean && make test'`。

#### 9.4.1 编译与回归

| 项目 | 结果 |
|---|---|
| 全部测试目标编译 | 成功，无编译错误，仅有业务源码既有告警 |
| `test_buffer_search`（UT-BS-001～006） | PASS 6 / FAIL 0 / TOTAL 6，无回归 |
| `test_recv_save_file` 前 5 条（UT-MP-001～005） | 5 个隔离执行全部 PASS，无回归 |

`make` 最终退出码非 0，属于"测试用例发现被测代码缺陷"的预期表现，不代表编译失败或测试框架故障。

#### 9.4.2 阶段 4 用例实际结果

| 用例ID | 场景 | 实际结果 | 状态 | 缺陷 |
|---|---|---|---|---|
| UT-MP-006 | 缺少起始 boundary | 子进程 signal 11（SIGSEGV） | NG | DEF-MP-001 |
| UT-MP-007 | 缺少结束 boundary | 未返回 -1，生成 `bad.bin`（rc=2） | NG | DEF-MP-002 |
| UT-MP-008 | 文件段缺少 `Content-Disposition` | 子进程 signal 11（SIGSEGV） | NG | DEF-MP-001 |
| UT-MP-009 | 文件段缺少 `filename` | 子进程 signal 11（SIGSEGV） | NG | DEF-MP-001 |
| UT-MP-010 | 缺少 `user` 字段 | 子进程 signal 11（SIGSEGV） | NG | DEF-MP-001 |
| UT-MP-011 | 缺少 `md5` 字段 | 子进程 signal 11（SIGSEGV） | NG | DEF-MP-001 |
| UT-MP-012 | 缺少 `size` 字段 | 返回 -1，无残留 | OK | — |
| UT-MP-013 | 声明大小小于实际内容 | 返回 -1，无残留，未静默截断 | OK | — |
| UT-MP-014 | 声明大小大于实际内容 | 返回 -1，无残留，未越界读 | OK | — |
| UT-MP-015 | `size` 为负数 / 非数字 / 溢出值 | 三项均返回 -1，无残留 | OK | — |
| UT-MP-016 | 1000 字符超长文件名 | `stack smashing detected`，signal 6 | NG | DEF-MP-003 |
| UT-MP-017 | 文件名包含 `../` | 未返回 -1，在测试沙箱父目录生成文件（rc=2） | NG | DEF-MP-004 |
| UT-MP-018 | 文件内容包含 boundary 字节 | 合法报文被返回 -1 拒绝（rc=1） | NG | DEF-MP-005 |
| UT-MP-019 | `user/md5/size` 字段顺序变化 | 字段乱序导致 `p4` 查找失败，随后 SIGSEGV（阶段 5 修正，见 10.6） | NG | DEF-MP-001 |
| UT-MP-020 | 报文在不同位置被截断 | 开头截断安全拒绝；文件内容处与 `size` 值处截断均 SIGSEGV | NG | DEF-MP-001 |

#### 9.4.3 统计

| 范围 | OK | POK | NG | 合计 |
|---|---|---|---|---|
| 阶段 4（UT-MP-006～020） | 4 | 0 | 11 | 15 |
| 累计（UT-MP-001～020） | 8 | 1 | 11 | 20 |

> **阶段 5 修正说明**：上表是修正后的数字。阶段 4 收口时曾记为 5 OK / 10 NG，其中 UT-MP-019 判为 OK；阶段 5 发现测试夹具的输入通路从未生效（见 10.1），修复后 UT-MP-019 实际会 SIGSEGV，故改为 NG。

测试框架断言（`test_recv_save_file` 普通构建）：PASS 13 / FAIL 11 / TOTAL 24（阶段 4 收口时）。断言数与业务用例数不等价：UT-MP-015 与 UT-MP-020 各展开为 3 个隔离断言。

阶段 4 的 11 条 NG 归并为 5 个缺陷：DEF-MP-001（7 条）、DEF-MP-002（1 条）、DEF-MP-003（1 条）、DEF-MP-004（1 条）、DEF-MP-005（1 条）。详细定级见 `tests/UPLOAD_DEFECT_LIST.md`。

#### 9.4.4 测试实现自查与临时文件清理

阶段 4 收口时对测试代码做了自查，发现并修正 3 处测试实现自身的问题，**未因被测代码崩溃而放宽任何预期**：

| 问题 | 影响 | 处理 |
|---|---|---|
| `run_valid_case()` 用 `mkdtemp()` 生成随机沙箱，父进程无法预知路径 | 子进程被信号或 Sanitizer 中止时无法回收；Sanitizer 预览运行后 `/tmp` 残留 6 个 `upload_cgi_multipart_*` 目录 | 改为固定路径 `/tmp/upload_cgi_<用例ID>`，并在 `main()` 中增加父进程兜底清理 |
| 测试输出缓冲区写成字面量 `128/256/256` | 若生产宏变化，超长字段用例结论会失真 | 改用 `util_cgi.h` 的 `USER_NAME_LEN`/`FILE_NAME_LEN`/`MD5_LEN`，并加 `_Static_assert` 固定取值 |
| `chdir(sandbox)` 失败时直接返回 | 罕见路径下泄漏沙箱目录 | 失败时先 `rmdir` 再返回 |

修正前后用例判定完全一致（`PASS 13 / FAIL 11 / TOTAL 24`）。

执行后复查容器：`/tmp/upload_cgi_*` 沙箱目录、`/tmp/escaped_by_upload_test.txt`、测试目录下的 `request.bin`／`bad.bin`／`boundary-content.bin` 均无残留；改用固定沙箱后，即使 Sanitizer 中止子进程也不再产生残留。清理仅针对测试自身创建的明确路径，未使用递归删除。即使子进程被信号终止，父进程仍会按固定路径回收沙箱。

## 10. 阶段 5：内存安全与回归（已完成）

### 10.1 检查目标

- 越界读写；
- use-after-free；
- 栈缓冲区破坏；
- 整数溢出和其他未定义行为；
- 测试进程异常退出或被信号终止。

### 10.2 执行方式

```bash
cd /app/tests/backend/unit
make sanitize                  # 构建 test_buffer_search_sanitize 与 test_recv_save_file_sanitize
make test-sanitize             # 执行两个 Sanitizer 目标
make analyze                   # 观测 recv_save_file 的可观测行为
make probe                     # 观测 libfcgi 输入通路

# 逐用例分析（关掉 ASan 的 SIGSEGV 处理器，避免其自循环刷爆日志）
ASAN_OPTIONS=detect_leaks=0:handle_segv=0 UBSAN_OPTIONS=print_stacktrace=1 \
  ./test_recv_save_file_sanitize
# 只保留真实故障（使用 libc 真正的 strstr）
ASAN_OPTIONS=detect_leaks=0:intercept_strstr=0 ./test_recv_save_file_sanitize
```

编译选项 `-fsanitize=address,undefined -fno-omit-frame-pointer -g`，使用独立的 object 与二进制，与普通构建完全分离。

**运行注意事项（复现时务必遵守）**：ASan 默认的 SIGSEGV 处理器在本项目上会进入 `AddressSanitizer:DEADLYSIGNAL` 自循环，曾把日志瞬时写到 14 GB。运行时应加 `handle_segv=0`，并给输出重定向加长度上限。

### 10.3 完整结果

完整证据见 `tests/backend/unit/results/multipart_stage5_20260915.log`，逐用例结论见下表。

| 用例 | ASan 分类 | 位置 | 判定 |
|---|---|---|---|
| UT-MP-001～005 | 无报告 | — | OK / POK |
| UT-MP-006 | SEGV（真实故障） | `:302` | NG |
| UT-MP-007 | 潜在 heap-buffer-overflow | `:369` | NG |
| UT-MP-008、009 | SEGV（真实故障） | `:302` | NG |
| UT-MP-010 | SEGV（真实故障） | `:313` | NG |
| UT-MP-011 | SEGV（真实故障） | `:328` | NG |
| UT-MP-012 | 潜在 heap-buffer-overflow | `:336` | OK |
| UT-MP-013～015 | 无报告 | — | OK |
| UT-MP-016 | stack-buffer-overflow | `:303` | NG |
| UT-MP-017 | 无报告（逻辑缺陷） | — | NG |
| UT-MP-018 | 无报告（逻辑缺陷） | — | NG |
| UT-MP-019 | SEGV（真实故障） | `:328` | NG |
| UT-MP-020a | 潜在 heap-buffer-overflow | `:275` | OK |
| UT-MP-020b | SEGV（真实故障） | `:309` | NG |
| UT-MP-020c | heap-buffer-overflow | `:352` | NG |
| UT-MP-021 | stack-buffer-overflow | `:270` | NG |
| UT-MP-022 | stack-buffer-overflow | `:320` | NG |
| UT-MP-023 | stack-buffer-overflow | `:330` | NG |
| UT-MP-024 | stack-buffer-overflow | `:352` | NG（普通断言看不出） |

- 越界读写与栈缓冲区破坏均有实证；UBSan 未报告任何整数溢出；未发现 use-after-free。
- 所有报告都指向 `src_cgi/upload_cgi.c`，是业务源码问题。
- 关掉 ASan 的 `strstr` 前置检查后仍有 7 条用例真实 SIGSEGV，证明 DEF-MP-006 的越界读不是插桩假象。
- UT-MP-024 在普通构建下返回 -1、无残留，**只有 Sanitizer 能发现**它写越界了 `size_text`——这正是"普通断言通过但存在内存问题"的实例。

### 10.4 阶段 5 新增用例

为验证 `recv_save_file()` 中四处无长度上限的 `strncpy()`，新增 UT-MP-021～024：

| 用例ID | 场景 | 目标缓冲区 | 关联缺陷 |
|---|---|---|---|
| UT-MP-021 | boundary 首行 606 字符 | `boundary[512]` | DEF-MP-003 |
| UT-MP-022 | `user` 值 200 字符 | `user[128]` | DEF-MP-003 |
| UT-MP-023 | `md5` 值 400 字符 | `md5[256]` | DEF-MP-003 |
| UT-MP-024 | `size` 文本 100 字符 | `size_text[65]` | DEF-MP-003 |

四条全部触发栈缓冲区溢出。其中 UT-MP-022 未触发栈保护，而是静默改写相邻的 `filename` 缓冲区，使被测函数用**被改写的名字**创建文件——说明不能只依赖栈保护发现这类问题。

### 10.5 零字节文件决策

仍待产品侧确认 R-03 规则，`UT-MP-004` 保持 POK：

- 若禁止零字节文件：`UT-MP-004` 固定预期为"返回失败且无残留"，状态由 POK 改为 OK；
- 若允许零字节文件：固定预期为成功解析和生成 0 字节文件，当前行为作为候选缺陷进一步复现。

### 10.6 阶段 5 对阶段 4 结论的修正

阶段 5 发现并修复了一处**测试夹具**缺陷（登记为 TEST-MP-001）：`upload_cgi.c` 经 `fcgi_stdio.h` 的宏替换后调用的是 `FCGI_fread(..., &_fcgi_sF[0])`，而测试进程中该包装未初始化，**被测函数从未真正读取报文**，此前用例是在复用的堆内存上碰巧得到正确报文才通过的。

修复后（只改夹具，不改业务源码）：

| 项目 | 修复前 | 修复后 |
|---|---|---|
| 阶段 4 统计 | 5 OK / 10 NG | **4 OK / 11 NG** |
| UT-MP-019 | 假通过（OK） | **NG，SEGV** |
| 其余用例 | — | 判定不变 |
| 普通构建与 Sanitizer 构建的关系 | 结论相反（夹具所致） | 不再矛盾：关闭 `strstr` 拦截的一轮逐用例判定与普通构建一致；开启拦截的一轮额外报告 UT-MP-007/012/020a 的潜在越界，并把 UT-MP-024 由 PASS 变 FAIL。**三种运行的 PASS 数（13 / 10 / 12）口径不同，不可合并** |

该缺陷同时暴露了业务源码的 DEF-MP-007（`fread()` 返回值被截断、短读后继续使用未初始化缓冲区）。

## 11. 阶段 6：CGI/API 与数据一致性测试

函数级异常测试稳定后，才进入 Docker 服务级测试。数据库等配置未完成前，可以准备脚本和数据，但依赖真实 MySQL/FastDFS 的用例必须标记为 NT，不得标记为通过。

计划覆盖：

- 正常小文件上传及响应 JSON；
- 无 token、过期 token和用户不匹配；
- 中文文件名、零字节文件和非法文件名；
- 畸形 multipart 请求后的服务存活性；
- 上传成功后的文件可读性；
- 上传失败后的临时文件清理；
- 数据库可用后补充 `file_info`、`user_file_list` 和 FastDFS 一致性断言。

服务级畸形报文可能终止 FastCGI 进程。执行前必须记录端口和进程基线，并准备恢复命令；先在一次性进程中筛选，再决定是否对常驻服务执行。

## 12. 状态与缺陷判定规则

| 状态 | 定义 |
|---|---|
| OK | 实际结果与唯一且明确的预期一致 |
| POK | 核心安全条件通过，但存在未确认需求或部分验收项未完成 |
| NG | 预期明确且实际结果不符合，已保留可重复证据 |
| NT | 因环境、配置或前置条件尚未执行 |

出现以下任一现象时，用例判为 NG，并进入缺陷候选：

- 子进程收到 `SIGSEGV`、`SIGABRT` 等异常信号；
- 非法报文被当作成功请求处理；
- 文件写出测试沙箱；
- 失败后残留文件或脏数据；
- Sanitizer 报告内存错误或未定义行为；
- 相同环境和输入下结果不可重复。

候选问题只有在“预期明确、至少可重复执行、证据完整”后才能写入正式缺陷报告。协作者文档中的既有 BUG 编号只作参考，当前项目重新复现时应避免无证据地直接沿用结论。

## 13. 执行与验收

普通函数级测试在项目的 Docker/Linux 编译环境中执行：

```bash
cd /app/tests/backend/unit
make clean
make test-buffer       # 只执行 buffer_search 六条测试
make test-multipart    # 只执行 multipart 测试
make test              # 执行当前全部 C 单元测试
```

每次新增测试后，至少执行对应目标和一次完整的 `make test`，防止 fixture 或编译选项的修改破坏已有用例。前端测试则继续使用第 5.2 节中的 Jest 命令独立回归。

阶段 4 完成的最低标准：

- UT-MP-006～UT-MP-020 均已实现并可单独定位失败；
- 测试辅助代码可以稳定生成所需畸形输入；
- 每个用例都检查进程安全和文件残留；
- 普通测试与 Sanitizer 测试均有保存的执行输出；
- 新发现问题完成复现、定级和关联矩阵映射；
- `docs/附录1：文件上传模块测试用例清单.xlsx` 同步更新。

### 13.1 阶段 4 验收结论（2026-09-15）

| 验收项 | 结论 | 依据 |
|---|---|---|
| UT-MP-006～020 全部实现且可单独定位 | 满足 | `test_recv_save_file.c` 中 15 条用例各自独立子进程执行 |
| 畸形输入构造能力稳定 | 满足 | `multipart_fixture.c` 的 `multipart_body_options` / `multipart_write_body_with_options` / `multipart_write_bytes` |
| 每条用例检查进程安全与文件残留 | 满足 | 隔离断言检查退出信号、返回值、输出文件存在性；父进程兜底清理 |
| 普通构建与 Sanitizer 构建均有保存输出 | 满足 | `results/multipart_stage4_20260915.log`、`results/raw_sanitize_preview_20260915.log` |
| 新问题完成复现、定级与矩阵映射 | 满足 | `UPLOAD_DEFECT_LIST.md` 中 DEF-MP-003/004/005，并完善 001/002 |
| 用例清单同步更新 | 满足 | `docs/附录1：文件上传模块测试用例清单.xlsx` 已加入 UT-MP-006～020 及当前状态 |

阶段 4 结论：**已完成**。修正后的统计为 15 条用例中 4 条 OK、11 条 NG，11 条 NG 归并为 5 个缺陷；既有 UT-BS-001～006 与 UT-MP-001～005 无回归；未修改任何业务源码。（阶段 4 收口时曾记为 5 OK / 10 NG，阶段 5 修正了 UT-MP-019，详见 10.6。）

### 13.2 阶段 5 验收结论（2026-09-15）

| 验收项 | 结论 | 依据 |
|---|---|---|
| 逐用例记录 Sanitizer 报告的分类和源码位置 | 满足 | `multipart_stage5_20260915.log` 第 5.1、5.2 节，19 条用例逐条给出类型与行号 |
| 区分"真实越界"与"契约违例" | 满足 | 用 `intercept_strstr=0`（libc 真正的 `strstr`）复跑，仍有 7 条真实 SIGSEGV |
| 确认 DEF-MP-006 与 DEF-MP-001 的因果关系 | 满足 | 全部 heap 报告分配点均为 `:230`；SEGV 点为独立的空指针解引用 |
| 验证 `strncpy()` 家族是否溢出 | 满足 | 新增 UT-MP-021～024，四处全部触发 stack-buffer-overflow |
| 阶段 4 判 OK 的用例重新判定 | 满足 | UT-MP-012 记录潜在越界；UT-MP-019 由假通过改为 NG；UT-MP-024 由普通断言漏检改为 NG |
| 测试夹具可信 | 满足 | 修复输入通路后发现并修正测试侧缺陷 TEST-MP-001，修复后普通构建与 Sanitizer 构建逐用例一致 |
| 新增缺陷完成复现与定级 | 满足 | `UPLOAD_DEFECT_LIST.md` 新增 DEF-MP-007、TEST-MP-001，并给出 DEF-MP-001/003/006 的精确位置 |
| 容器无残留 | 满足 | 见 `multipart_stage5_20260915.log` 第 7 节 |

阶段 5 结论：**已完成**。阶段 5 新增 4 条用例全部为 NG；阶段 4 统计修正为 4 OK / 11 NG；被测源码 `src_cgi/upload_cgi.c` 全程未修改。

## 14. 阶段 7：质量度量与交付物更新

每完成一个批次，按以下顺序更新：

1. 自动化测试脚本和 fixture；
2. 测试执行原始输出；
3. 测试用例清单中的实际结果和状态；
4. 需求—风险—用例矩阵中的自动化层和覆盖状态；
5. 可重复问题对应的缺陷报告；
6. 阶段结束后更新模块测试报告和覆盖率指标。

建议持续统计以下质量指标：

| 指标 | 计算方式 |
|---|---|
| 需求覆盖率 | 至少有一条已设计用例的需求数 / 需求总数 |
| 风险覆盖率 | 至少有一条已设计用例的风险数 / 风险总数 |
| 高风险执行覆盖率 | 至少有一条已执行用例的高风险数 / 高风险总数 |
| 自动化率 | 已实现自动化脚本的用例数 / 用例总数 |
| 用例通过率 | OK 数 / 已执行用例数 |
| 缺陷密度 | 有效缺陷数 / 被测函数或模块规模 |
| 缺陷修复回归率 | 已完成回归的修复缺陷数 / 已修复缺陷数 |

## 15. 下一步动作

阶段 4 与阶段 5 均已收口。下一步进入**阶段 6：CGI/API 与数据一致性测试**，同时有两项前置事项需要先确认：

1. **需求确认**：R-03 零字节文件规则（决定 UT-MP-004 由 POK 定为 OK 还是转为缺陷）；
2. **修复后回归**：DEF-MP-001／002／003／004／006／007 修复后，用现有用例集重新执行 `make test` 与 `make test-sanitize`，确认由 NG 转为 OK，并检查是否有新问题暴露（DEF-MP-006 修复后 DEF-MP-001 的崩溃用例是否仍然崩溃，用于验证因果判断）。

阶段 6 的计划（沿用第 11 节）需要真实 Docker 服务与数据库：

- 正常小文件上传及响应 JSON；
- 无 token、过期 token 和用户不匹配；
- 中文文件名、零字节文件和非法文件名；
- 畸形 multipart 请求后的服务存活性；
- 上传成功后的文件可读性、上传失败后的临时文件清理；
- 数据库可用后补充 `file_info`、`user_file_list` 和 FastDFS 一致性断言。

阶段 5 的执行方法（探针、`intercept_strstr=0` 的两轮对比、`handle_segv=0` 与输出上限）在阶段 6 排查服务侧问题时同样适用。
