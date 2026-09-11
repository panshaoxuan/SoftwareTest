export const jsonResponse = (body) => ({
  json: jest.fn().mockResolvedValue(body),
});

export const installFetchMock = (...responses) => {
  global.fetch = jest.fn();
  responses.forEach((response) => global.fetch.mockResolvedValueOnce(jsonResponse(response)));
  return global.fetch;
};
