# 文件上传模块自动化测试全程规划

> 文档版本：0.3  
> 编制日期：2026-09-15  
> 最近更新：2026-09-15（阶段 4 收口：批次 A/B/C 全部完成，15 条用例 5 OK / 10 NG）  
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
| 阶段 4 | 测试畸形 multipart、长度边界和路径安全 | **已完成**（批次 A/B/C 全部完成） | UT-MP-006～UT-MP-020、5 个缺陷 |
| 阶段 5 | 使用 Sanitizer 执行内存安全检查并回归 | 进行中（构建目标已完成，分析待做） | Sanitizer 日志、回归结果 |
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
| UT-MP-019 | `user/md5/size` 字段顺序变化 | 明确拒绝，进程未崩溃，无残留 | OK | — |
| UT-MP-020 | 报文在不同位置被截断 | 开头截断安全拒绝；文件内容处与 `size` 值处截断均 SIGSEGV | NG | DEF-MP-001 |

#### 9.4.3 统计

| 范围 | OK | POK | NG | 合计 |
|---|---|---|---|---|
| **阶段 4（UT-MP-006～020）** | **5** | 0 | **10** | **15** |
| 累计（UT-MP-001～020） | 9 | 1 | 10 | 20 |

测试框架断言（`test_recv_save_file` 普通构建）：PASS 13 / FAIL 11 / TOTAL 24。断言数与业务用例数不等价：UT-MP-015 与 UT-MP-020 各展开为 3 个隔离断言。

阶段 4 的 10 条 NG 归并为 5 个缺陷：DEF-MP-001（7 条）、DEF-MP-002（1 条）、DEF-MP-003（1 条）、DEF-MP-004（1 条）、DEF-MP-005（1 条）。详细定级见 `tests/UPLOAD_DEFECT_LIST.md`。

#### 9.4.4 测试实现自查与临时文件清理

阶段 4 收口时对测试代码做了自查，发现并修正 3 处测试实现自身的问题，**未因被测代码崩溃而放宽任何预期**：

| 问题 | 影响 | 处理 |
|---|---|---|
| `run_valid_case()` 用 `mkdtemp()` 生成随机沙箱，父进程无法预知路径 | 子进程被信号或 Sanitizer 中止时无法回收；Sanitizer 预览运行后 `/tmp` 残留 6 个 `upload_cgi_multipart_*` 目录 | 改为固定路径 `/tmp/upload_cgi_<用例ID>`，并在 `main()` 中增加父进程兜底清理 |
| 测试输出缓冲区写成字面量 `128/256/256` | 若生产宏变化，超长字段用例结论会失真 | 改用 `util_cgi.h` 的 `USER_NAME_LEN`/`FILE_NAME_LEN`/`MD5_LEN`，并加 `_Static_assert` 固定取值 |
| `chdir(sandbox)` 失败时直接返回 | 罕见路径下泄漏沙箱目录 | 失败时先 `rmdir` 再返回 |

修正前后用例判定完全一致（`PASS 13 / FAIL 11 / TOTAL 24`）。

执行后复查容器：`/tmp/upload_cgi_*` 沙箱目录、`/tmp/escaped_by_upload_test.txt`、测试目录下的 `request.bin`／`bad.bin`／`boundary-content.bin` 均无残留；改用固定沙箱后，即使 Sanitizer 中止子进程也不再产生残留。清理仅针对测试自身创建的明确路径，未使用递归删除。即使子进程被信号终止，父进程仍会按固定路径回收沙箱。

## 10. 阶段 5：内存安全与回归

### 10.1 检查目标

- 越界读写；
- use-after-free；
- 栈缓冲区破坏；
- 整数溢出和其他未定义行为；
- 测试进程异常退出或被信号终止。

### 10.2 执行原则

阶段 4 已为 Docker/Linux 编译环境建立**独立的** Sanitizer 构建目标，不改动 `src_cgi/` 下的业务源码：

```bash
cd /app/tests/backend/unit
make sanitize                  # 构建 test_buffer_search_sanitize 与 test_recv_save_file_sanitize
make test-multipart-sanitize   # 单独执行 multipart 的 Sanitizer 版本
make test-sanitize             # 执行两个 Sanitizer 目标
```

编译选项为：

```text
-fsanitize=address,undefined -fno-omit-frame-pointer -g
```

Sanitizer 构建使用独立的 object（`upload_cgi_testable_sanitize.o`）和独立的二进制，与普通构建完全分离，避免插桩污染日常回归使用的测试程序；`make clean` 会同时清理两类产物。

阶段 4 只要求 Sanitizer 目标**能够正确构建**，完整的内部分析、定位和修复后回归属于阶段 5。阶段 4 收口时执行过一次预览运行，结果记录在 `tests/backend/unit/results/raw_sanitize_preview_20260915.log`：24 / 24 个隔离子进程均被 AddressSanitizer 以 `heap-buffer-overflow` 中止，分配点为 `upload_cgi.c:230`、使用点为 `upload_cgi.c:261`，属于业务源码问题（缓冲区缺少 NUL 终止符），已登记为 DEF-MP-006。由于每个子进程在第一处报告后即中止，该预览结果**不能**替代普通构建的用例判定。

### 10.3 五项检查的完整化

阶段 5 需要在普通构建结论的基础上，补充：

- 逐个用例记录 Sanitizer 报告的分类和源码位置；
- 确认 DEF-MP-006 与 DEF-MP-001 的因果关系（修复缓冲区终止符后重新统计崩溃用例）；
- 确认 `strncpy()` 系列无长度上限拷贝（`boundary`／`user`／`md5`／`size_text`）是否触发栈缓冲区溢出；
- 将阶段 4 判为 OK 的用例（UT-MP-012～015、019）在 Sanitizer 下重新判定，防止"普通断言通过但存在内存问题"。

### 10.4 零字节文件决策

在本阶段确认 R-03 的产品规则：

- 若禁止零字节文件：`UT-MP-004` 固定预期为“返回失败且无残留”，状态由 POK 改为 OK；
- 若允许零字节文件：固定预期为成功解析和生成 0 字节文件，当前行为作为候选缺陷进一步复现。

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

阶段 4 结论：**已完成**。15 条用例中 5 条 OK、10 条 NG，10 条 NG 归并为 5 个缺陷；既有 UT-BS-001～006 与 UT-MP-001～005 无回归；未修改任何业务源码。

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

阶段 4（批次 A/B/C）已全部完成。下一步进入**阶段 5：Sanitizer 内存安全分析**：

1. 使用已有的 `make sanitize` 目标，逐用例收集 AddressSanitizer 与 UndefinedBehaviorSanitizer 的完整报告，保存为阶段 5 证据文件；
2. 优先分析 DEF-MP-006（`file_buf` 缺少 NUL 终止符导致 `strstr()` 越界读）。该问题对正常报文同样触发，且很可能是 DEF-MP-001 多个 SIGSEGV 的公共诱因；
3. 验证 DEF-MP-003 所述 `strncpy()` 系列无长度上限拷贝（`boundary`／`user`／`md5`／`size_text`）是否同样触发栈缓冲区溢出；
4. 将阶段 4 判为 OK 的用例（UT-MP-012～015、UT-MP-019）在 Sanitizer 下重新判定；
5. 在阶段 5 结论中确认 DEF-MP-003／004／006 的最终严重程度。

阶段 5 的输入条件：Sanitizer 构建目标已就绪（`Makefile` 的 `sanitize`、`test-sanitize`、`test-multipart-sanitize`），预览运行结果见 `tests/backend/unit/results/raw_sanitize_preview_20260915.log`。
