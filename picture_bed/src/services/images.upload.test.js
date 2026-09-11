import { uploadImage } from './images';
import { createMockFile } from '../test/helpers/mockFile';
import { testUser } from '../test/helpers/testUser';

// 上传服务只依赖 MD5 的结果；这里隔离第三方哈希实现，专注验证请求编排。
jest.mock('spark-md5', () => ({
  __esModule: true,
  default: {
    ArrayBuffer: class {
      append() {}
      end() { return 'md5-test'; }
    },
  },
}));

describe('uploadImage - ordinary upload', () => {
  beforeEach(() => {
    jest.clearAllMocks();
    global.FileReader = class {
      readAsArrayBuffer() {
        this.onload({ target: { result: new ArrayBuffer(0) } });
      }
    };
  });

  // 验证需求 R-04：MD5 未命中时，请求顺序必须是 md5 检测 → 普通上传，且 FormData 字段完整。
  test('TC-M-001: MD5 未命中时应先检测再上传文件实体', async () => {
    global.fetch = jest.fn()
      .mockResolvedValueOnce({ json: jest.fn().mockResolvedValue({ code: 1 }) })
      .mockResolvedValueOnce({ json: jest.fn().mockResolvedValue({ code: 0 }) });
    const progress = jest.fn();

    const result = await uploadImage(createMockFile(10, 'small.txt'), testUser, progress);

    expect(result).toEqual(expect.objectContaining({ md5: 'md5-test', instant: false }));
    expect(global.fetch).toHaveBeenCalledTimes(2);
    expect(global.fetch.mock.calls[0][0]).toBe('/api/md5');
    expect(global.fetch.mock.calls[1][0]).toBe('/api/upload');
    expect(global.fetch.mock.invocationCallOrder[0]).toBeLessThan(global.fetch.mock.invocationCallOrder[1]);
    const body = global.fetch.mock.calls[1][1].body;
    expect(body).toBeInstanceOf(FormData);
    expect(body.get('user')).toBe(testUser.username);
    expect(body.get('md5')).toBe('md5-test');
    expect(body.get('size')).toBe('10');
    expect(body.get('file')).toBeInstanceOf(File);
    expect(progress).toHaveBeenLastCalledWith(100);
  });

  // 验证需求 R-05：服务端确认文件可复用时，只返回秒传结果，不再发送 /api/upload 文件实体。
  test('TC-M-003: 秒传命中时不应上传文件实体', async () => {
    global.fetch = jest.fn().mockResolvedValue({
      json: jest.fn().mockResolvedValue({ code: 0 }),
    });

    const result = await uploadImage(createMockFile(10, 'duplicate.txt'), testUser);

    expect(result).toEqual(expect.objectContaining({ instant: true, alreadyExists: false, md5: 'md5-test' }));
    expect(global.fetch).toHaveBeenCalledTimes(1);
    expect(global.fetch).toHaveBeenCalledWith('/api/md5', expect.objectContaining({ method: 'POST' }));
  });

  // 验证需求 R-06：同一用户重复上传时返回 alreadyExists，避免重复建立文件关联。
  test('TC-M-005: 已存在于当前用户时返回重复文件结果', async () => {
    global.fetch = jest.fn().mockResolvedValue({
      json: jest.fn().mockResolvedValue({ code: 5 }),
    });

    const result = await uploadImage(createMockFile(10, 'same.txt'), testUser);

    expect(result.alreadyExists).toBe(true);
    expect(result.instant).toBe(true);
    expect(global.fetch).toHaveBeenCalledTimes(1);
  });

  // 验证需求 R-11：后端返回 token 失效码时，前端转换为 tokenExpired 供页面退出登录和提示。
  test('TC-S-002: token 失效时抛出 tokenExpired 错误', async () => {
    global.fetch = jest.fn().mockResolvedValue({
      json: jest.fn().mockResolvedValue({ code: 4 }),
    });

    await expect(uploadImage(createMockFile(10), testUser)).rejects.toMatchObject({ tokenExpired: true });
    expect(global.fetch).toHaveBeenCalledTimes(1);
  });
});
