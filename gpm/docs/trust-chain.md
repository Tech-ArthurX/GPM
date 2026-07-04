# GPM 信任链与密钥规范

英文版: [trust-chain.en.md](trust-chain.en.md)

本文定义 `.gpm` 包签名、密钥对生成和本地验证流程。
GPM 的服务端只需要分发 JSON 索引和下载地址；包下载后由客户端在本地解析、校验和安装。

## 信任模型

- 官方根密钥是最高信任锚，根私钥只由平台方离线保管。
- 中间密钥是开发者或项目密钥，由官方根私钥签发。
- 开发者用中间私钥签名 `.gpm` 包。
- 客户端内置或预置官方根公钥，并在本地验证 `根公钥 -> 中间公钥 -> 包内容`。
- 中间密钥只允许用于 `usage=code_signing`，不允许继续签发子密钥。

## 根密钥生成

根密钥使用 Ed25519：

```bash
openssl genpkey -algorithm ED25519 -out root_private.pem
openssl pkey -in root_private.pem -pubout -out root_public.pem
```

规则：

- `root_private.pem` 只保存在离线环境，不进入公开仓库和发布包。
- `root_public.pem` 可以内置到客户端，或随公开版本一起发布。
- 根密钥一般只生成一次；轮换根密钥时需要明确版本和迁移策略。

## 中间密钥生成

中间密钥建议按 `开发者/项目` 维度生成：

```bash
openssl genpkey -algorithm ED25519 -out Dism++_private_key.pem
openssl pkey -in Dism++_private_key.pem -pubout -out Dism++_public_key.pem
```

推荐目录结构：

```text
keys/
  ArthurX/
    Dism++/
      Dism++_private_key.pem
      Dism++_public_key.pem
      Dism++_public_key.sig
```

`Dism++_private_key.pem` 交给开发者用于包签名。
`Dism++_public_key.pem` 和 `Dism++_public_key.sig` 可以由索引引用、内嵌到签名尾部，或放到公开公钥目录。

## 中间公钥签发

官方根私钥签发中间公钥。
兼容规则如下：

1. 读取中间公钥 PEM 文本。
2. 将换行统一为 `\n`，并去掉首尾空白。
3. 拼接用途标签：

```text
<normalized_public_key_pem>
usage=code_signing
```

4. 对上述文本计算 SHA256。
5. 使用根私钥对 SHA256 结果做 Ed25519 签名。
6. 将签名结果 Base64 编码，保存为 `*_public_key.sig`。

用途标签是信任边界的一部分。
客户端必须验证 `usage=code_signing`，否则不能把该中间公钥当作包签名密钥使用。

## 包签名尾部

签名后的 `.gpm` 包在原始 zip 内容后追加一个 `GPM2SIG0` 签名尾部。
签名前应先移除旧的 `GPM2SIG0` 尾部和旧版 88 字节 Base64 签名，避免重复签名。

签名步骤：

1. 读取原始 `.gpm` 字节。
2. 计算 `fingerprint = SHA256(original_bytes)`。
3. 使用中间私钥对 `fingerprint` 做 Ed25519 签名。
4. 生成 JSON payload。
5. 写入 `original_bytes + payload + footer`。

payload 字段：

```json
{
  "version": 2,
  "fingerprint": "base64(sha256(original_bytes))",
  "fingerprint_signature": "base64(ed25519_sign(fingerprint))",
  "signer_pubkey": "developer public key PEM",
  "signer_pubkey_signature": "base64(root_sign(sha256(pubkey + usage=code_signing)))"
}
```

footer 为 12 字节：

```text
GPM2SIG0 + uint32_be(payload_length)
```

## 本地验证流程

客户端下载 `.gpm` 后按下面顺序验证：

1. 如果 `packages.json` 提供 `sha256`，先校验下载文件完整性。
2. 解析文件末尾 12 字节 footer，确认 magic 为 `GPM2SIG0`。
3. 按 footer 中的长度读取 payload，并取 payload 前面的内容作为原始包内容。
4. 计算原始包内容 SHA256，并与 payload 的 `fingerprint` 比较。
5. 用官方根公钥验证 `signer_pubkey_signature`。
6. 用 `signer_pubkey` 验证 `fingerprint_signature`。
7. 验证通过后才能解压、读取脚本和安装。

如果发布策略要求强制签名，缺少 `GPM2SIG0` 的包必须拒绝安装。
如果只启用公开索引的完整性校验，则至少必须验证 `sha256`。

## 索引中的公钥信息

服务端可以只维护一个 JSON。
这个 JSON 可以包含包大小、哈希、下载地址和公钥位置，但不能包含任何私钥。

推荐可选字段：

```json
{
  "public_key_url": "https://example.test/keys/ArthurX/Dism++/Dism++_public_key.pem",
  "public_key_signature_url": "https://example.test/keys/ArthurX/Dism++/Dism++_public_key.sig",
  "key_id": "ArthurX/Dism++"
}
```

包尾部已经包含 `signer_pubkey` 和 `signer_pubkey_signature` 时，客户端可以直接本地验证；
索引里的公钥 URL 更适合做审计、展示和密钥轮换。

## 安全要求

- 根私钥和中间私钥都不能提交到公开仓库。
- 私钥不应放进 `.gpm` 包、`packages.json`、GitHub Releases 或服务器静态目录。
- 服务端不需要在线搜索包内容，也不需要替客户端做最终包验签。
- 客户端必须在解压前完成哈希和签名校验。
- `sha256` 只能证明下载完整性，不能替代根密钥信任链。
