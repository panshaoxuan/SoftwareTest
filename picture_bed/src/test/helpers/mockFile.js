// 创建测试文件。大文件使用轻量对象，避免测试启动时分配大量内存。
export const createMockFile = (size = 1024, name = 'test.txt', type = 'text/plain') => {
  // 10 MB 边界需要作为真实 File 传入 FormData；更大的样本使用轻量对象。
  if (size <= 10 * 1024 * 1024) {
    return new File([new Uint8Array(size)], name, { type });
  }

  return {
    name,
    size,
    type,
    slice: (start, end) => new Blob([new Uint8Array(Math.max(0, end - start))], { type }),
  };
};

export const FILE_SIZES = {
  TEN_MB: 10 * 1024 * 1024,
  OVER_TEN_MB: 10 * 1024 * 1024 + 1,
  TWENTY_FIVE_MB: 25 * 1024 * 1024,
};
