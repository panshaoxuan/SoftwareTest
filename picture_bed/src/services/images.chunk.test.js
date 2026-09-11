import { uploadImage, uploadChunked } from './images';
import { createMockFile, FILE_SIZES } from '../test/helpers/mockFile';
import { testUser } from '../test/helpers/testUser';

jest.mock('spark-md5', () => ({
  __esModule: true,
  default: {
    ArrayBuffer: class {
      append() {}
      end() { return 'md5-large'; }
    },
  },
}));

describe('uploadChunked', () => {
  beforeEach(() => {
    jest.clearAllMocks();
    global.FileReader = class {
      readAsArrayBuffer() {
        this.onload({ target: { result: new ArrayBuffer(0) } });
      }
    };
  });

  // 验证需求 R-03/R-07 的阈值边界：当前实现仅对大于 10 MB 的文件启用分片，恰好 10 MB 仍走普通上传。
  test('TC-B-002: 恰好 10 MB 文件走普通上传', async () => {
    global.fetch = jest.fn()
      .mockResolvedValueOnce({ json: jest.fn().mockResolvedValue({ code: 1 }) })
      .mockResolvedValueOnce({ json: jest.fn().mockResolvedValue({ code: 0 }) });

    await uploadImage(createMockFile(FILE_SIZES.TEN_MB, 'boundary.bin'), testUser);

    expect(global.fetch.mock.calls.map(([url]) => url)).toEqual(['/api/md5', '/api/upload']);
  });

  // 验证需求 R-07：大文件必须按 init → 多个 chunk_upload → chunk_merge 的完整顺序执行，并报告进度。
  test('TC-B-003/TC-C-001: 超过 10 MB 时执行 init、逐片上传和 merge', async () => {
    global.fetch = jest.fn()
      // md5 未命中
      .mockResolvedValueOnce({ json: jest.fn().mockResolvedValue({ code: 1 }) })
      // init
      .mockResolvedValueOnce({ json: jest.fn().mockResolvedValue({ code: 0 }) })
      // 3 个分片
      .mockResolvedValueOnce({ json: jest.fn().mockResolvedValue({ code: 0 }) })
      .mockResolvedValueOnce({ json: jest.fn().mockResolvedValue({ code: 0 }) })
      .mockResolvedValueOnce({ json: jest.fn().mockResolvedValue({ code: 0 }) })
      // merge
      .mockResolvedValueOnce({ json: jest.fn().mockResolvedValue({ code: 0 }) });
    const progress = jest.fn();

    const result = await uploadImage(createMockFile(FILE_SIZES.TWENTY_FIVE_MB, 'large.bin'), testUser, progress);

    expect(result.md5).toBe('md5-large');
    expect(global.fetch).toHaveBeenCalledTimes(6);
    expect(global.fetch.mock.calls[1][0]).toBe('/api/chunk_init');
    expect(global.fetch.mock.calls[1][1].body).toContain('"chunkCount":3');
    expect(global.fetch.mock.calls[2][0]).toContain('/api/chunk_upload?md5=md5-large&index=0');
    expect(global.fetch.mock.calls[3][0]).toContain('index=1');
    expect(global.fetch.mock.calls[4][0]).toContain('index=2');
    expect(global.fetch.mock.calls[5][0]).toBe('/api/chunk_merge');
    expect(progress).toHaveBeenCalledWith(0);
    expect(progress).toHaveBeenCalledWith(90);
    expect(progress).toHaveBeenLastCalledWith(100);
  });

  // 验证需求 R-08：断点续传时解析服务端已上传索引，只补传缺失分片，然后继续合并。
  test('TC-C-004: 初始化返回已上传分片时跳过这些分片', async () => {
    global.fetch = jest.fn()
      .mockResolvedValueOnce({ json: jest.fn().mockResolvedValue({ code: 1 }) })
      .mockResolvedValueOnce({ json: jest.fn().mockResolvedValue({ code: 0, uploadedChunks: '0,2' }) })
      .mockResolvedValueOnce({ json: jest.fn().mockResolvedValue({ code: 0 }) })
      .mockResolvedValueOnce({ json: jest.fn().mockResolvedValue({ code: 0 }) });

    await uploadChunked(createMockFile(FILE_SIZES.TWENTY_FIVE_MB, 'resume.bin'), testUser);

    const urls = global.fetch.mock.calls.map(([url]) => url);
    expect(urls.filter((url) => url.includes('/api/chunk_upload'))).toEqual([
      '/api/chunk_upload?md5=md5-large&index=1',
    ]);
    expect(urls).toContain('/api/chunk_merge');
  });

  // 验证需求 R-10：任一分片失败时立即终止后续流程并抛出可识别错误，不能误调用 merge。
  test('TC-C-009: 分片上传失败时停止流程并抛出错误', async () => {
    global.fetch = jest.fn()
      .mockResolvedValueOnce({ json: jest.fn().mockResolvedValue({ code: 1 }) })
      .mockResolvedValueOnce({ json: jest.fn().mockResolvedValue({ code: 0 }) })
      .mockResolvedValueOnce({ json: jest.fn().mockResolvedValue({ code: 9 }) });

    await expect(uploadChunked(createMockFile(FILE_SIZES.TWENTY_FIVE_MB), testUser))
      .rejects.toThrow('分片 0 上传失败');
    expect(global.fetch).toHaveBeenCalledTimes(3);
  });
});
