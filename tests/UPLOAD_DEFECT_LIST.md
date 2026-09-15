# 文件上传模块缺陷清单

> 当前证据基线：`feat/cgi-upload-tests`（工作区基线提交 `99341ab`，本轮测试改动尚未提交）
> 最近更新：2026-09-15（阶段 4 收口）
> 本清单仅记录在当前本地 Docker 环境中复现的问题。
> 阶段 4 完整证据：`tests/backend/unit/results/multipart_stage4_20260915.log`
> 缺陷总览：DEF-MP-001（高）、DEF-MP-002（中）、DEF-MP-003（严重）、DEF-MP-004（严重）、DEF-MP-005（中）、DEF-MP-006（严重，阶段 5 待完整确认）

## 总览

| 缺陷ID | 摘要 | 严重程度 | 优先级 | 关联用例 |
|---|---|---|---|---|
| DEF-MP-001 | 畸形 multipart 多处 SIGSEGV | 高 | P0 | UT-MP-006、008、009、010、011、020 |
| DEF-MP-002 | 缺少最终 boundary 仍生成文件 | 中 | P1 | UT-MP-007 |
| DEF-MP-003 | 超长文件名导致栈缓冲区溢出 | 严重 | P0 | UT-MP-016 |
| DEF-MP-004 | filename 未做路径校验，可写出工作目录 | 严重 | P0 | UT-MP-017 |
| DEF-MP-005 | 文件内容包含 boundary 字节时被误拒/可能静默截断 | 中 | P1 | UT-MP-018 |
| DEF-MP-006 | `file_buf` 缺少 NUL 终止符导致 `strstr()` 越界读 | 严重（待阶段 5 确认） | P0 | UT-MP-001～020（全部触发） |

---

## DEF-MP-001：畸形 multipart 报文导致解析进程崩溃

| 属性 | 内容 |
|---|---|
| 状态 | 已复现，待修复 |
| 严重程度 | 高 |
| 优先级 | P0 |
| 关联用例 | UT-MP-006、UT-MP-008、UT-MP-009、UT-MP-010、UT-MP-011、UT-MP-020b、UT-MP-020c |
| 关联需求/风险 | R-12、RK-04、RK-10 |
| 测试层次 | C 函数级单元测试 |
| 证据 | `tests/backend/unit/results/multipart_stage4_20260915.log` 第 5 节 |

### 复现步骤

1. 在 `tc_fcgi_app` 中进入 `/app/tests/backend/unit`；
2. 执行 `make clean && make test-multipart`；
3. 观察上述用例的隔离子进程状态。

### 预期结果

解析函数返回 `-1`，进程正常退出，不生成文件、不保留残留。

### 实际结果

7 个隔离用例的子进程均收到 signal 11（SIGSEGV）：

| 用例 | 触发报文 |
|---|---|
| UT-MP-006 | 去掉开头的边界行 |
| UT-MP-008 | 文件段去掉 `Content-Disposition` |
| UT-MP-009 | 文件段去掉 `filename` |
| UT-MP-010 | 整条报文缺少 `user` 字段 |
| UT-MP-011 | 整条报文缺少 `md5` 字段 |
| UT-MP-020b | 在文件内容中间截断 |
| UT-MP-020c | 在 `size` 数值中间截断 |

### 初步原因

`recv_save_file()` 对多个 `strstr()` 及 `buffer_search()` 的返回值未判空，随后直接做指针加减或再次解引用：

- `upload_cgi.c:299` `p2 = strstr(p1, "filename=")` 无判空，`upload_cgi.c:300` 直接 `p2 += strlen("filename=\"")`，当返回 `NULL` 时得到地址 9，`upload_cgi.c:302` 的 `strstr(p2, "\"")` 即段错误（UT-MP-006、008、009）；
- `upload_cgi.c:309` `p3 = strstr(..., "name=\"user\"")` 无判空，`upload_cgi.c:311` 同样偏移后再交给 `trim_space_and_around()`（UT-MP-010、020b）；
- `upload_cgi.c:325` `p4 = strstr(end, "name=\"md5\"")` 无判空（UT-MP-011）；
- `upload_cgi.c:289` `file_end -= strlen("\r\n")` 未验证 `file_end` 足够靠后，内容处截断时指针回退到内容起点之前（UT-MP-020b、020c）。
- 对照组：`upload_cgi.c:337` 对 `p5`（`name="size"`）做了判空，因此 UT-MP-012 能安全拒绝——说明判空是缺失而非有意设计。

初步原因与 DEF-MP-006（缓冲区无 NUL 终止符）耦合：`strstr()` 在未终止缓冲区上的扫描结果本身不可靠。精确崩溃指令地址将在阶段 5 由 Sanitizer 给出。

---

## DEF-MP-002：缺少结束 boundary 的报文被接受并生成文件

| 属性 | 内容 |
|---|---|
| 状态 | 已复现，待修复 |
| 严重程度 | 中 |
| 优先级 | P1 |
| 关联用例 | UT-MP-007 |
| 关联需求/风险 | R-12、RK-04 |
| 测试层次 | C 函数级单元测试 |
| 证据 | `tests/backend/unit/results/multipart_stage4_20260915.log` 第 5 节 |

### 复现步骤

1. 构造包含 file、user、md5、size 字段但没有最终 `boundary--` 的报文；
2. 将报文重定向到标准输入并调用 `recv_save_file()`；
3. 检查函数返回值和当前目录下的 `bad.bin`。

### 预期结果

返回 `-1`，且不生成文件。

### 实际结果

函数返回 `0`（未拒绝），并在当前工作目录生成了包含上传内容的 `bad.bin`。测试判定 `rc=2`，随后由测试清理逻辑删除该文件。

### 初步原因

解析流程只依赖 `buffer_search()` 找到的第一个 boundary 出现位置来计算 `file_end`（`upload_cgi.c:284`～`285`），没有在写文件之前验证结尾存在 `boundary + "--" + CRLF`（`upload_cgi.c:369`～`374` 的 `p6` 计算也未用于任何校验）。因此报文尾部是否是合法结束标志对结果没有影响，只要 `(file_end - file_start) == (*p_size)` 就落盘。

---

## DEF-MP-003：超长文件名导致栈缓冲区溢出

| 属性 | 内容 |
|---|---|
| 状态 | 已复现，待修复 |
| 严重程度 | **严重**（建议） |
| 优先级 | P0 |
| 关联用例 | UT-MP-016 |
| 关联需求/风险 | R-12、TC-N-004、RK-10 |
| 测试层次 | C 函数级单元测试 |
| 证据 | `tests/backend/unit/results/multipart_stage4_20260915.log` 第 5 节 |

### 复现步骤

1. 构造合法 multipart 报文，把 `filename=` 的值替换为 1000 个 `a`；
2. 使用与生产代码一致的 256 字节文件名缓冲区（`char filename[256]`，对应 `include/util_cgi.h:4` 的 `FILE_NAME_LEN`）；
3. 将报文重定向到标准输入并调用 `recv_save_file()`。

### 预期结果

返回 `-1`，进程不崩溃，不生成文件。

### 实际结果

子进程输出 `*** stack smashing detected ***: terminated`，随后收到 signal 6（SIGABRT）。测试判定 NG。本次运行未产生文件残留。

### 原因

`upload_cgi.c:303` 与 `304`：

```c
strncpy(filename, p2, end-p2);
filename[end-p2] = '\0';
```

`end - p2` 完全由请求报文中的 `filename` 字段长度决定，没有任何上限判断，也没有使用调用方缓冲区的容量（`FILE_NAME_LEN`）。当长度超过 256 时，`strncpy()` 与随后的写零都会越界写。生产调用方 `main()`（`upload_cgi.c:1029`）声明的正是 `char filename[FILE_NAME_LEN]`，缓冲区大小与测试一致，因此测试复现的是真实生产缺陷，不是测试自身缓冲区设置过小。

### 为什么定级为"严重"

1. **写越界且长度可控**：越界长度由请求方完全控制，不是固定的小幅溢出，可以精确覆盖保存的寄存器与返回地址。
2. **当前仅被栈保护拦下**：本次看到的是 `-fstack-protector` 生效后的 `stack smashing detected`。若构建未启用栈保护，或攻击者构造不触发 canary 校验的长度，该越界写即为远程代码执行原语。
3. **未认证可达**：`recv_save_file()` 在 `upload_cgi.c:1067` 被直接调用，位于 `main()` 的 `FCGI_Accept()` 循环内，其之前没有任何 token 或用户校验（`upload_cgi.c:1039`～`1067`）。任何能向该 FastCGI 接口发送 POST 报文的一方都可触发。
4. **触发条件平凡**：只需一个普通的 `filename` 字段，不需要构造畸形报文。
5. **同类问题可能不止一处**：`upload_cgi.c:270` 的 `strncpy(boundary, begin, p1-begin)`、`upload_cgi.c:320` 的 `strncpy(user, p3, end-p3)`、`upload_cgi.c:330` 的 `strncpy(md5, p4, end-p4)`、`upload_cgi.c:352` 的 `strncpy(size_text, p5, end-p5)` 采用相同写法，`boundary`（512）、`user`（128）、`md5`（256）、`size_text`（65）四个缓冲区同样缺少长度上限，需在修复时一并处理。

---

## DEF-MP-004：filename 未限制路径，允许写出测试沙箱

| 属性 | 内容 |
|---|---|
| 状态 | 已复现，待修复 |
| 严重程度 | **严重**（建议） |
| 优先级 | P0 |
| 关联用例 | UT-MP-017 |
| 关联需求/风险 | R-12、TC-S-006、RK-10 |
| 测试层次 | C 函数级单元测试 |
| 证据 | `tests/backend/unit/results/multipart_stage4_20260915.log` 第 5 节 |

### 复现步骤

1. 在测试沙箱 `/tmp/upload_cgi_UT-MP-017` 中把工作目录切换过去；
2. 构造合法 multipart 报文，`filename` 取值为 `../escaped_by_upload_test.txt`；
3. 将报文重定向到标准输入并调用 `recv_save_file()`；
4. 检查沙箱**父目录**`/tmp` 下是否出现文件。

### 预期结果

返回 `-1`；文件名中的路径分量被拒绝或规范化；文件不得出现在工作目录之外。

### 实际结果

函数返回 `0`（未拒绝），并在沙箱父目录 `/tmp` 下生成了 `escaped_by_upload_test.txt`，内容为上传的文件内容。测试判定 `rc=2`（越权落盘）。

### 原因

`upload_cgi.c:392`：

```c
fd = open(filename, O_CREAT|O_WRONLY, 0644);
```

`filename` 直接来自报文解析结果（`upload_cgi.c:303`～`304`），既没有做 `../`、`/` 等路径分量的过滤，也没有限定基准目录。`open()` 会原样解释相对路径，因此 `../` 可以把文件写到工作目录的任意上级目录；若请求方给出绝对路径（如 `/tmp/x`），同样会被直接接受。

### 为什么定级为"严重"

1. **任意文件写入**：写入路径与文件名由请求方控制，可覆盖 FastCGI 进程有权写入的任意文件（服务自身的配置、脚本、临时目录等），是典型的路径穿越漏洞。
2. **未认证可达**：与 DEF-MP-003 相同，`recv_save_file()` 在任何鉴权之前执行。
3. **触发条件平凡**：只需在正常的 `filename` 字段中带上 `../`，不需要畸形报文。

### 复现边界与清理说明

- **本次只在 Docker 容器 `tc_fcgi_app` 的 `/tmp` 测试沙箱中复现**，未在宿主机、未在业务工作目录、未对容器内其他路径做任何写入尝试。
- 测试创建的 `/tmp/escaped_by_upload_test.txt` 已由测试代码显式 `unlink()` 删除；执行后复查容器，`/tmp` 下无该文件、无 `upload_cgi_*` 沙箱目录残留。清理只针对测试自身创建的明确路径，未执行递归删除。

---

## DEF-MP-005：文件内容包含 boundary 字节时被错误拒绝

| 属性 | 内容 |
|---|---|
| 状态 | 已复现，待修复 |
| 严重程度 | 中 |
| 优先级 | P1 |
| 关联用例 | UT-MP-018 |
| 关联需求/风险 | R-01、RK-04 |
| 测试层次 | C 函数级单元测试 |
| 证据 | `tests/backend/unit/results/multipart_stage4_20260915.log` 第 5 节 |

### 复现步骤

1. 构造一条完全合法的 multipart 报文，声明 `size` 与实际内容长度一致；
2. 把文件内容设为 `prefix` + 边界字符串 + `suffix`（即内容字节中恰好含有与 boundary 相同的字节串，但**不出现在行首**，因此不构成 MIME 分隔符）；
3. 将报文重定向到标准输入并调用 `recv_save_file()`。

### 预期结果

报文合法，应正常解析并落盘，文件内容与原始字节逐字节一致。

### 实际结果

`recv_save_file()` 返回 `-1`，解析失败，文件未生成（测试判定 `rc=1`）。

### 原因

`upload_cgi.c:285`：

```c
file_end = buffer_search(file_start, buf_end - file_start, boundary, strlen(boundary));
```

`buffer_search()` 在剩余缓冲区中做逐字节 `memcmp`，在**任意位置**匹配 boundary，而不要求它出现在行首（MIME 规定分隔符必须位于 `CRLF` 之后）。内容中出现相同字节串时，`file_end` 被提前定位到内容内部；又因为 `upload_cgi.c:289` 无条件执行 `file_end -= strlen("\r\n")`，最终 `(file_end - file_start)` 与声明 `size` 不一致，触发 `upload_cgi.c:377` 的长度校验失败并返回 `-1`。

### 影响

- **当前失败模式是 fail-closed（明确拒绝）**，不会静默落盘错误内容，因此不构成安全漏洞；
- 但报文本身合法，拒绝即造成正常上传失败，属于解析正确性缺陷；
- **潜在的数据完整性问题**：如果请求方构造的内容恰好使"误定位后的长度"等于声明的 `size`，长度校验会通过，`upload_cgi.c:403`～`404` 的 `ftruncate()` + `write()` 将只写入被截断的前半段内容，即**静默截断并落盘损坏文件**（对应 RK-02"文件损坏"）。因此该缺陷不只是"误拒"，需要在修复时按 MIME 规则改为"行首匹配"并显式校验长度。

### 为什么定级为"中"

触发需要在文件内容中出现与 boundary 完全相同的字节串。浏览器/前端通常使用随机 boundary，自然发生的概率低；但该行为不符合 MIME 解析规则，且叠加静默截断的风险，需要修复。

---

## DEF-MP-006：`file_buf` 缺少 NUL 终止符导致 `strstr()` 越界读

| 属性 | 内容 |
|---|---|
| 状态 | 已初步复现，待阶段 5 完整确认 |
| 严重程度 | **严重**（建议，待阶段 5 确认） |
| 优先级 | P0 |
| 关联用例 | UT-MP-001～UT-MP-020（Sanitizer 构建下 24 / 24 全部触发） |
| 关联需求/风险 | R-12、RK-04 |
| 测试层次 | C 函数级单元测试 + AddressSanitizer |
| 证据 | `tests/backend/unit/results/raw_sanitize_preview_20260915.log` |

### 复现步骤

1. 在 `tc_fcgi_app` 中执行 `make sanitize`；
2. 执行 `ASAN_OPTIONS=detect_leaks=0 ./test_recv_save_file_sanitize`；
3. 观察每个隔离子进程的 Sanitizer 报告。

### 预期结果

解析合法报文时不产生任何 Sanitizer 报告。

### 实际结果

24 个隔离子进程**全部**被 AddressSanitizer 中止，错误类型均为 `heap-buffer-overflow`（越界读）。24 次报告的唯一栈帧组合一致：

```text
#2 ... in recv_save_file ../../../src_cgi/upload_cgi.c:261   （使用点）
allocated by thread T0 here:
#1 ... in recv_save_file ../../../src_cgi/upload_cgi.c:230   （分配点）
SUMMARY: AddressSanitizer: heap-buffer-overflow ... in StrstrCheck
```

注意：**正常报文用例 UT-MP-001 同样触发**，说明该问题与报文是否畸形无关。

### 原因

`upload_cgi.c:230`：

```c
file_buf = (char *)malloc(len);
```

`upload_cgi.c:237` 用 `fread(file_buf, 1, len, stdin)` 精确读入 `len` 字节，之后**没有写入 `'\0'` 终止符**；紧接着 `upload_cgi.c:261` 起就对该缓冲区调用 `strstr()`。C 标准要求 `strstr()` 的实参是 NUL 终止字符串，因此该调用属于越界读：当查找串在缓冲区中不存在时，`strstr()` 会持续向后扫描直到偶然遇到 `'\0'`，读取长度不受 `len` 约束。

`upload_cgi.c:261`、`275`、`281`、`284`、`299`、`302`、`309`、`325`、`336`、`351`、`369` 处对 `file_buf` 的全部 `strstr()` 调用都受同一问题影响。这也解释了为什么 DEF-MP-001 的崩溃点集中在这些 `strstr()` 上。

### 为什么建议定级为"严重"

1. 这是**所有请求**（含正常上传）都会进入的路径，不是边界场景；
2. 越界读的终点由堆上相邻内存的内容决定，行为不可预测，可能读取敏感内存或触发段错误；
3. 它是 DEF-MP-001 多个 SIGSEGV 的公共诱因，属根因类缺陷，修复优先级高于逐个补判空。

### 待确认项（阶段 5）

- 该报告的完整分类与源码级定位；
- 在修复 DEF-MP-006 后，DEF-MP-001 的 7 个崩溃用例是否仍然崩溃（用于区分"根因"与"独立缺陷"）；
- `strncpy(boundary, ...)`、`strncpy(user, ...)`、`strncpy(md5, ...)`、`strncpy(size_text, ...)` 等同类无长度上限拷贝的 Sanitizer 确认。
