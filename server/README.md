# Server

英文版: [README.en.md](README.en.md)

`server/` 提供公开包索引服务和索引扫描工具。

它的职责是保持 `packages.json` 可读、可刷新、可直接给客户端使用。

## 公开接口

- `GET /index`
- `GET /packages.json`
- `GET /registry.json`
- `GET /healthz`

## 输入输出

- 读取公开目录中的 `.gpm` 包或现成的 `packages.json`。
- 提供包名、版本、作者、大小、摘要和下载地址等公共字段。
- 可以在同一个 JSON 中提供公钥下载地址或公钥签名地址，但不能提供私钥。
- 不承担在线搜索包内容的职责。
- 最终包验签由客户端下载后在本地完成。

## 相关文档

- `../README.md` - 仓库总览。
- `../build.py` - 根目录公开 `.gpm` 包构建器。
- `../gpm/docs/package.md` - `.gpm` 包格式。
- `../gpm/docs/trust-chain.md` - 密钥信任链与本地验证流程。
