# 文件上传模块缺陷清单

> 当前证据基线：`feat/cgi-upload-tests`
> 最近更新：2026-09-15（阶段 5 内存安全分析收口）
> 本清单仅记录在当前本地 Docker 环境中复现的问题。
> 阶段 4 证据：`tests/backend/unit/results/multipart_stage4_20260915.log`
> 阶段 5 证据：`tests/backend/unit/results/multipart_stage5_20260915.log`
> 被测源码 `src_cgi/upload_cgi.c` 全程未修改。

## 总览

| 缺陷ID | 摘要 | 严重程度 | 优先级 | 关联用例 |
|---|---|---|---|---|
| DEF-MP-001 | 畸形 multipart 多处空指针解引用导致 SIGSEGV | 高 | P0 | UT-MP-006、008、009、010、011、019、020b |
| DEF-MP-002 | 缺少最终 boundary 仍生成文件 | 中 | P1 | UT-MP-007 |
| DEF-MP-003 | 字段长度不受限导致栈缓冲区溢出（5 处） | **严重** | P0 | UT-MP-016、021、022、023、024 |
| DEF-MP-004 | filename 未做路径校验，可写出工作目录 | **严重** | P0 | UT-MP-017 |
| DEF-MP-005 | 文件内容包含 boundary 字节时被错误拒绝 | 中 | P1 | UT-MP-018 |
| DEF-MP-006 | `file_buf` 缺少 NUL 终止符导致 `strstr()` 越界读 | **严重** | P0 | UT-MP-006、007、008、009、010、011、012、019、020a、020b、020c |
| DEF-MP-007 | `fread()` 返回值被截断，短读/失败后继续用未初始化缓冲区解析 | 高 | P0 | 全部用例（测试夹具修复后暴露） |
| TEST-MP-001 | **测试夹具缺陷**：C 单元测试的输入通路从未生效 | —（测试侧，已修复） | — | 全部 UT-MP 用例 |

判定口径：非法输入允许返回 `-1`，但不允许进程崩溃、写出沙箱、残留文件或发生未定义行为；Sanitizer 报告内存错误同样判为 NG。

---

## DEF-MP-001：畸形 multipart 报文导致解析进程崩溃

| 属性 | 内容 |
|---|---|
| 状态 | 已复现，待修复 |
| 严重程度 | 高 |
| 优先级 | P0 |
| 关联用例 | UT-MP-006、UT-MP-008、UT-MP-009、UT-MP-010、UT-MP-011、UT-MP-019、UT-MP-020b |
| 关联需求/风险 | R-12、RK-04、RK-10 |
| 测试层次 | C 函数级单元测试 + AddressSanitizer |
| 证据 | `multipart_stage5_20260915.log` 第 5.2 节 |

### 实际结果（ASan 精确崩溃点）

| 用例 | 触发报文 | ASan 报告 | 崩溃位置 |
|---|---|---|---|
| UT-MP-006 | 去掉开头边界行 | SEGV | `upload_cgi.c:302` |
| UT-MP-008 | 文件段去掉 `Content-Disposition` | SEGV | `upload_cgi.c:302` |
| UT-MP-009 | 文件段去掉 `filename` | SEGV | `upload_cgi.c:302` |
| UT-MP-010 | 缺少 `user` 字段 | SEGV | `upload_cgi.c:313` |
| UT-MP-011 | 缺少 `md5` 字段 | SEGV | `upload_cgi.c:328` |
| UT-MP-019 | 字段顺序改为 file→md5→user→size | SEGV | `upload_cgi.c:328` |
| UT-MP-020b | 在文件内容处截断 | SEGV | `upload_cgi.c:309` |

### 原因

`recv_save_file()` 对多个 `strstr()` 的返回值未判空，随后直接做指针偏移并再次解引用：

- `upload_cgi.c:299` `p2 = strstr(p1, "filename=")` 无判空 → `300` 偏移 → `302` 的 `strstr(p2, "\"")` 解引用近似空地址（UT-MP-006/008/009）；
- `upload_cgi.c:309` `p3 = strstr(..., "name=\"user\"")` 无判空 → `311` 偏移 → `313` 的 `trim_space_and_around(p3, buf_end)`（UT-MP-010、020b 的 `file_end` 为野指针）；
- `upload_cgi.c:325` `p4 = strstr(end, "name=\"md5\"")` 无判空 → `326` 偏移 → `328` 的 `trim_space_and_around(p4, buf_end)`（UT-MP-011、019）；
- 对照组：`upload_cgi.c:337` 对 `n`（`name="size"`）做了判空，因此 UT-MP-012 能安全拒绝——说明判空是缺失而不是有意设计。

该缺陷与 DEF-MP-006 相互独立：即使缓冲区正确 NUL 终止，这些查找在字段确实缺失时仍然返回 `NULL`。

### 影响

`recv_save_file()` 在 `upload_cgi.c:1067` 被直接调用，位于 `main()` 的 `FCGI_Accept()` 循环内，其之前没有任何 token 校验。因此**未认证的攻击者只需构造一段畸形的 multipart 报文即可让上传 CGI 进程崩溃**，属于拒绝服务。

---

## DEF-MP-002：缺少结束 boundary 的报文被接受并生成文件

| 属性 | 内容 |
|---|---|
| 状态 | 已复现，待修复 |
| 严重程度 | 中 |
| 优先级 | P1 |
| 关联用例 | UT-MP-007 |
| 关联需求/风险 | R-12、RK-04 |
| 证据 | `multipart_stage5_20260915.log` 第 5.1 节 |

### 实际结果

函数返回 `0`（未拒绝），并在当前工作目录生成了包含上传内容的 `bad.bin`；测试判定 `rc=2`，随后由测试清理逻辑删除。ASan 另在 `upload_cgi.c:369`（`p6 = strstr(end, boundary)`）报告一处潜在越界读。

### 原因

解析流程只依赖 `buffer_search()` 找到的第一个 boundary 出现位置计算 `file_end`（`upload_cgi.c:284`～`285`），没有在写文件前验证结尾存在 `boundary + "--" + CRLF`。`upload_cgi.c:369`～`374` 计算出的 `p6` 未参与任何校验。只要 `(file_end - file_start) == (*p_size)` 就落盘。

---

## DEF-MP-003：字段长度不受限导致栈缓冲区溢出

| 属性 | 内容 |
|---|---|
| 状态 | 已复现，待修复 |
| 严重程度 | **严重** |
| 优先级 | P0 |
| 关联用例 | UT-MP-016、UT-MP-021、UT-MP-022、UT-MP-023、UT-MP-024 |
| 关联需求/风险 | R-12、TC-N-004、RK-10 |
| 证据 | `multipart_stage5_20260915.log` 第 4、5.2 节 |

### 实际结果（ASan 直接定位）

| 用例 | 超长字段 | 目标缓冲区 | ASan 报告 | 位置 |
|---|---|---|---|---|
| UT-MP-016 | `filename` 1000 字符 | `filename[FILE_NAME_LEN]`(256) | stack-buffer-overflow | `upload_cgi.c:303` |
| UT-MP-021 | boundary 首行 606 字符 | `boundary[TEMP_BUF_MAX_LEN]`(512) | stack-buffer-overflow | `upload_cgi.c:270` |
| UT-MP-022 | `user` 200 字符 | `user[USER_NAME_LEN]`(128) | stack-buffer-overflow | `upload_cgi.c:320` |
| UT-MP-023 | `md5` 400 字符 | `md5[MD5_LEN]`(256) | stack-buffer-overflow | `upload_cgi.c:330` |
| UT-MP-024 | `size` 文本 100 字符 | `size_text[64+1]`(65) | stack-buffer-overflow | `upload_cgi.c:352` |

普通构建下的表现：

- UT-MP-016 / 021 / 023：`*** stack smashing detected ***` 后 signal 6；
- UT-MP-022：**未触发栈保护**，溢出静默改写相邻的 `filename` 缓冲区，被测函数用被改写的名字（72 个 `u`）创建了文件；
- UT-MP-024：普通断言下返回 -1、无残留，**只有 ASan 能发现**它写越界了 `size_text`（多出的字节落进相邻的 `boundary` 缓冲区，没碰到 canary）。

### 原因

五处拷贝的长度都直接来自报文，没有任何上限判断：

```c
upload_cgi.c:270  strncpy(boundary,  begin, p1-begin);
upload_cgi.c:303  strncpy(filename,  p2,   end-p2);
upload_cgi.c:320  strncpy(user,      p3,   end-p3);
upload_cgi.c:330  strncpy(md5,       p4,   end-p4);
upload_cgi.c:352  strncpy(size_text, p5,   end-p5);
```

### 为什么定级为"严重"

1. **写越界且长度由请求方完全控制**，可精确覆盖保存的寄存器与返回地址；
2. **当前只是被栈保护偶然拦下**：UT-MP-022 已证明并非每次都能拦下，溢出会静默破坏相邻数据；未启用 `-fstack-protector` 的构建直接是远程代码执行原语；
3. **未认证可达**：`recv_save_file()` 在任何鉴权之前执行；
4. **触发条件平凡**：`user` 字段只需超过 128 字节，`size` 文本只需超过 65 字节；
5. **五处同一模式**，必须一并修复。

---

## DEF-MP-004：filename 未限制路径，允许写出测试沙箱

| 属性 | 内容 |
|---|---|
| 状态 | 已复现，待修复 |
| 严重程度 | **严重** |
| 优先级 | P0 |
| 关联用例 | UT-MP-017 |
| 关联需求/风险 | R-12、TC-S-006、RK-10 |
| 证据 | `multipart_stage5_20260915.log` 第 5.2 节 |

### 实际结果

函数返回 `0`（未拒绝），并在沙箱父目录 `/tmp` 下生成了 `escaped_by_upload_test.txt`，内容为上传的文件内容。测试判定 `rc=2`（越权落盘）。该用例不产生任何内存安全报告，属于**逻辑缺陷**而非内存缺陷。

### 原因

`upload_cgi.c:392`：

```c
fd = open(filename, O_CREAT|O_WRONLY, 0644);
```

`filename` 直接来自报文解析结果（`upload_cgi.c:303`～`304`），既没有过滤 `../`、`/` 等路径分量，也没有限定基准目录。

### 为什么定级为"严重"

1. **任意文件写入**：路径与文件名由请求方控制，可覆盖 FastCGI 进程有权写入的任意文件；
2. **未认证可达**：与 DEF-MP-003 相同；
3. **触发条件平凡**：正常的 `filename` 字段中带上 `../` 即可。

### 复现边界与清理说明

- **本次只在 Docker 容器 `tc_fcgi_app` 的 `/tmp` 测试沙箱中复现**，未在宿主机、未在业务工作目录、未对容器内其他路径做任何写入尝试。
- 测试创建的 `/tmp/escaped_by_upload_test.txt` 已由测试代码显式 `unlink()` 删除；执行后复查容器，`/tmp` 下无该文件、无 `upload_cgi_*` 沙箱目录残留。清理只针对测试自身创建的明确路径。

---

## DEF-MP-005：文件内容包含 boundary 字节时被错误拒绝

| 属性 | 内容 |
|---|---|
| 状态 | 已复现，待修复 |
| 严重程度 | 中 |
| 优先级 | P1 |
| 关联用例 | UT-MP-018 |
| 关联需求/风险 | R-01、RK-04 |
| 证据 | `multipart_stage5_20260915.log` 第 5.2 节 |

### 实际结果

`recv_save_file()` 返回 `-1`，解析失败，文件未生成（`rc=1`）。无任何内存安全报告。

### 原因

`upload_cgi.c:285` 的 `buffer_search()` 在剩余缓冲区中做逐字节 `memcmp`，在**任意位置**匹配 boundary，而不要求它位于行首（MIME 规定分隔符必须在 `CRLF` 之后）。内容中出现相同字节串时 `file_end` 被提前定位；又因为 `upload_cgi.c:289` 无条件执行 `file_end -= strlen("\r\n")`，最终长度与声明 `size` 不一致，触发 `upload_cgi.c:377` 的长度校验失败。

### 影响

- 当前失败模式是 fail-closed（明确拒绝），不构成安全漏洞；
- 但报文本身合法，拒绝即造成正常上传失败，属于解析正确性缺陷；
- **潜在数据完整性问题**：若请求方构造的内容恰好使"误定位后的长度"等于声明 `size`，长度校验会通过，`upload_cgi.c:403`～`404` 的 `ftruncate()` + `write()` 将只写入被截断的前半段内容，即**静默截断并落盘损坏文件**（对应 RK-02"文件损坏"）。

---

## DEF-MP-006：`file_buf` 缺少 NUL 终止符导致 `strstr()` 越界读

| 属性 | 内容 |
|---|---|
| 状态 | 已复现，待修复 |
| 严重程度 | **严重** |
| 优先级 | P0 |
| 关联用例 | UT-MP-006、007、008、009、010、011、012、019、020a、020b、020c |
| 关联需求/风险 | R-12、RK-04 |
| 测试层次 | C 函数级单元测试 + AddressSanitizer |
| 证据 | `multipart_stage5_20260915.log` 第 5.1、5.2 节 |

### 实际结果

`upload_cgi.c:230` 用 `malloc(len)` 精确分配 `len` 字节，`upload_cgi.c:237` 读入 `len` 字节后**没有写入 `'\0'` 终止符**，紧接着 `upload_cgi.c:261` 起对该缓冲区调用 `strstr()`。

ASan 在 11 条用例上报告 `heap-buffer-overflow`，**分配点全部是 `upload_cgi.c:230`**，使用点各不相同：

| 使用点 | 说明 | 用例 |
|---|---|---|
| `:275` | `strstr(begin, "Content-Type:")` | UT-MP-020a |
| `:284` | `buffer_search(file_start, ..., boundary, ...)` 前的边界定位 | UT-MP-006、020b |
| `:299` | `strstr(p1, "filename=")` | UT-MP-008、009 |
| `:309` | `strstr(file_end + strlen(boundary), "name=\"user\"")` | UT-MP-010 |
| `:325` | `strstr(end, "name=\"md5\"")` | UT-MP-011、019 |
| `:336` | `strstr(end, "name=\"size\"")` | UT-MP-012 |
| `:351` | `strstr(p5, "\r\n")` | UT-MP-020c |
| `:369` | `strstr(end, boundary)` | UT-MP-007 |

### 这些越界是真实的，不是插桩假象

关掉 ASan 的 `strstr` 前置检查（`intercept_strstr=0`，即使用 libc 真正的 `strstr`）后重新执行，仍有 **7 条用例报 SIGSEGV**（UT-MP-006/008/009/010/011/019/020b），另有 UT-MP-020c 在 `:352` 的 `strncpy()` 处真实读越界。说明当查找串在缓冲区内不存在时，真实的 `strstr` 会一路扫描到分配区域之外。

UT-MP-007、UT-MP-012、UT-MP-020a 只在有前置检查时报告：它们的查找串在缓冲区内提前命中，扫描没有跑到边界之外。这仍违反 `strstr` 的 NUL 终止契约，是否越界取决于报文内容，属于**不确定行为**。

### 为什么定级为"严重"

1. 影响**所有请求**（合法报文同样会在 `:261` 处调用 `strstr`），不是边界场景；
2. 越界扫描的终点由堆上相邻内存决定，可能读取敏感内存、也可能触发段错误；
3. 它是 DEF-MP-001 之外的一类根因缺陷，修复优先级不低于逐个补判空。

---

## DEF-MP-007：`fread()` 返回值被截断，短读后继续用未初始化缓冲区解析

| 属性 | 内容 |
|---|---|
| 状态 | 已复现，待修复 |
| 严重程度 | 高 |
| 优先级 | P0 |
| 关联用例 | 影响全部 UT-MP 用例的错误处理路径（由测试夹具修复后暴露） |
| 关联需求/风险 | R-12、RK-04 |
| 测试层次 | C 函数级单元测试（探针观测） |
| 证据 | `multipart_stage5_20260915.log` 第 2 节 |

### 复现步骤

1. 在单元测试进程中重定向标准输入后调用 `recv_save_file()`；
2. 用 `probe_fcgi_stdin.c` 观察 `FCGI_fread()` 的返回值与被测函数的实际读取量。

### 实际结果

`FCGI_fread()` 返回 `(size_t)-1`（即 `SIZE_MAX`）时，`upload_cgi.c:237` 把它截断成 `int ret2 == -1`，而错误分支条件是 `if (ret2 == 0)`，**永不成立**。函数继续执行，`file_buf` 保持 `malloc()` 出来的未初始化内容，后续所有解析都作用在随机数据上。

### 原因

```c
upload_cgi.c:237   int ret2 = fread(file_buf, 1, len, stdin);
upload_cgi.c:238   if(ret2 == 0) { ret = -1; goto END; }
```

两处问题叠加：

1. `fread()` 的返回值是 `size_t`，被赋给 `int`；失败值 `(size_t)-1` 截断为 `-1`，而判断只覆盖"恰好读到 0 字节"；
2. 只要返回值不是 0（包括失败值的 -1、以及任何**短读**，例如只读到 `len-100` 字节），检查就通过，缓冲区尾部的 `len - ret2` 字节仍是未初始化内存。

在真实 FastCGI 运行环境中 `FCGI_Accept()` 会设置好 `stdio_stream`，但**短读**路径依然存在：客户端声明的 `CONTENT_LENGTH` 大于实际发送的字节数时，函数会用未初始化内存继续解析，并把 `filename` 等字段交给 `open()`。

### 影响

- 未初始化内存被当作报文解析，属于 CWE-457（Use of Uninitialized Variable）；
- 解析出的 `filename` 直接用于 `open(filename, O_CREAT|O_WRONLY, 0644)`，与 DEF-MP-004 叠加可造成**以随机/受控名字创建文件**；
- 正确写法应为 `if (ret2 != len)`，且 `ret2` 应声明为 `size_t`。

---

## TEST-MP-001：C 单元测试的输入通路从未生效（测试夹具缺陷，已修复）

| 属性 | 内容 |
|---|---|
| 状态 | **已修复**（只改测试夹具，未改业务源码） |
| 性质 | 测试侧缺陷，不是产品缺陷 |
| 影响范围 | 阶段 2/3/4 中所有 `recv_save_file()` 用例的结论有效性 |
| 证据 | `multipart_stage5_20260915.log` 第 2、3 节 |

### 现象

`upload_cgi.c` 包含的 `fcgi_stdio.h` 会把 `stdin` 替换为 `FCGI_stdin`（`&_fcgi_sF[0]`）、把 `fread` 替换为 `FCGI_fread`。测试进程从不调用 `FCGI_Accept()`，因此 `_fcgi_sF[0].stdio_stream` 始终是 `NULL`，`FCGI_fread()` 直接返回 `(size_t)-1`，**一个字节都没有读**。

### 为什么会"通过"

`recv_save_file()` 的读失败检查因 DEF-MP-007 永不触发，函数改用 `malloc(len)` 的未初始化内存继续解析。测试夹具在此之前刚用 `fprintf` 写过同一份报文并 `fclose`，其 `FILE*` 缓冲区被释放后很可能正好被这次 `malloc(len)` 复用——解析于是"读到"了正确的报文。Sanitizer 构建使用不同的分配器，复用不成立，同一个用例立刻给出相反结果。

### 修复

在 `multipart_fixture.c` 中新增 `multipart_bind_fcgi_stdin()`，由 `multipart_redirect_stdin()` 自动调用，把 libfcgi 的包装指向真正的 `stdin`：

```c
typedef struct { FILE *stdio_stream; void *fcgx_stream; } fixture_fcgi_file;
extern fixture_fcgi_file _fcgi_sF[];

void multipart_bind_fcgi_stdin(void) { _fcgi_sF[0].stdio_stream = stdin; }
```

### 修复带来的结论修正

- 阶段 4 统计由 5 OK / 10 NG 修正为 **4 OK / 11 NG**；
- **UT-MP-019（字段乱序）由假通过改为 NG**：真实拿到报文后会走到 `p4 = strstr(end, "name=\"md5\"")`，乱序时返回 `NULL`，随后 `:328` 段错误；
- 其余用例判定不变；
- 修复后普通构建与 Sanitizer 构建的逐用例判定完全一致。
